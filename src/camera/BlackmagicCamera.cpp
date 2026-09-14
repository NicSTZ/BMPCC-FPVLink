#include "BlackmagicCamera.h"

BlackmagicCamera* BlackmagicCamera::instance = nullptr;

static const NimBLEUUID BMD_SERVICE("291D567A-6D75-11E6-8B77-86F30CA893D3");
static const NimBLEUUID OUTGOING_UUID("5DD3465F-1AEE-4299-8493-D2ECA2F8E1BB");
static const NimBLEUUID INCOMING_UUID("B864E140-76A0-416A-BF30-5876504537D9");
static const NimBLEUUID TIMECODE_UUID("6D8F2110-86F1-41BF-9AFB-451D87E976C8");
static const NimBLEUUID STATUS_UUID("7FE8691D-95DC-4FC5-8ABD-CA74339B51B9");
static const NimBLEUUID PROTOCOL_UUID("8F1FD018-B508-456F-8F82-3D392BEE2706");
static const NimBLEUUID DEVICE_INFO("180A");
static const NimBLEUUID MODEL_UUID("2A24");

BlackmagicCamera::BlackmagicCamera() : callbacks(this) { instance = this; }

void BlackmagicCamera::begin() {
    NimBLEDevice::init("FPVCineCam32");
    NimBLEDevice::setPower(3);
    // Bonding + MITM. The BMPCC shows a 6-digit PIN and our web UI injects it.
    NimBLEDevice::setSecurityAuth(true, true, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
    camState.status = "BMD IDLE";
}

bool BlackmagicCamera::startScan(String& jsonOut) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(60);
    scan->setWindow(45);
    NimBLEScanResults results = scan->getResults(3500, false);
    jsonOut = "[";
    bool first = true;
    for (int i = 0; i < results.getCount(); i++) {
        const NimBLEAdvertisedDevice* d = results.getDevice(i);
        if (!d->isAdvertisingService(BMD_SERVICE)) continue;
        if (!first) jsonOut += ',';
        first = false;
        String name = d->getName().c_str();
        String addr = d->getAddress().toString().c_str();
        uint8_t type = d->getAddress().getType();
        jsonOut += "{\"name\":\"" + name + "\",\"address\":\"" + addr + "\",\"type\":" + String(type) + "}";
    }
    jsonOut += "]";
    scan->clearResults();
    return true;
}

bool BlackmagicCamera::connectTo(const String& address, uint8_t addressType) {
    if (!address.length()) return false;
    requestedAddress = address;
    requestedAddressType = addressType;
    connectRequested = true;
    camState.status = "CONNECT QUEUED";
    return true;
}

void BlackmagicCamera::connectTaskThunk(void* arg) {
    BlackmagicCamera* self = static_cast<BlackmagicCamera*>(arg);
    String addr = self->requestedAddress;
    uint8_t type = self->requestedAddressType;
    self->connectRequested = false;
    self->performConnect(addr, type);
    self->connectTaskRunning = false;
    vTaskDelete(nullptr);
}

void BlackmagicCamera::performConnect(const String& address, uint8_t addressType) {
    if (client && client->isConnected()) client->disconnect();

    serviceReady = false;
    subscriptionsReady = false;
    outgoing = incoming = timecode = statusChar = modelChar = protocolChar = nullptr;
    passkeyPending = false;
    pendingConnHandle = BLE_HS_CONN_HANDLE_NONE;
    camState.connected = false;
    camState.paired = false;
    camState.ready = false;
    camState.model = "";
    camState.protocolVersion = "";
    camState.lastCommand = "";
    camState.lastWrite = "";
    camState.controlReady = false;
    camState.mediaRemaining = "--";
    camState.activeMediaSlot = 0;
    for (int i = 0; i < 3; i++) camState.mediaSlotRemaining[i] = "--";
    mediaSeconds[0] = mediaSeconds[1] = mediaSeconds[2] = 0;
    mediaSecondsSeen = false;
    activeMediaSeen = false;
    camState.incomingSubscription = "none";
    camState.incomingPackets = 0;
    camState.lastIncoming = "";
    incomingSubscribeOk = false;
    incomingPacketCount = 0;
    postAuthRequested = false;
    camState.status = "CONNECTING BLE";

    if (!client) {
        client = NimBLEDevice::createClient();
        client->setClientCallbacks(&callbacks, false);
        client->setConnectTimeout(8000);
    }

    NimBLEAddress addr(address.c_str(), addressType);
    if (!client->connect(addr, true, false, false)) {
        camState.status = "BLE CONNECT FAIL";
        return;
    }

    connectedAddress = address;
    connectedAddressType = addressType;
    camState.connected = true;
    camState.status = "BLE CONNECTED";

    NimBLERemoteService* svc = client->getService(BMD_SERVICE);
    if (!svc) {
        camState.status = "NO BMD SERVICE";
        client->disconnect();
        return;
    }

    outgoing = svc->getCharacteristic(OUTGOING_UUID);
    incoming = svc->getCharacteristic(INCOMING_UUID);
    timecode = svc->getCharacteristic(TIMECODE_UUID);
    statusChar = svc->getCharacteristic(STATUS_UUID);
    protocolChar = svc->getCharacteristic(PROTOCOL_UUID);

    if (auto* info = client->getService(DEVICE_INFO)) {
        modelChar = info->getCharacteristic(MODEL_UUID);
    }
    readIdentity();

    serviceReady = outgoing && statusChar;
    if (!serviceReady) {
        camState.status = "BMD CHAR MISSING";
        client->disconnect();
        return;
    }

    NimBLEConnInfo ci = client->getConnInfo();
    if (ci.isEncrypted() || ci.isBonded()) {
        camState.paired = true;
        camState.status = "ALREADY BONDED";
        discoverAndSubscribe();
        return;
    }

    // Blackmagic's documented pairing trigger is an attempted write to an encrypted
    // characteristic. Camera Status 0x01 = Camera Power On. The write may block until
    // the PIN exchange completes, so this entire connection routine runs in a FreeRTOS
    // task while the main loop keeps the web UI responsive for PIN entry.
    camState.status = "TRIGGERING CAMERA PIN";
    const bool wrote = triggerPairingByEncryptedWrite();

    if (!client || !client->isConnected()) {
        if (camState.status != "PAIR FAILED") camState.status = "DISCONNECTED DURING PAIR";
        return;
    }

    ci = client->getConnInfo();
    if (ci.isEncrypted() || ci.isBonded()) {
        camState.paired = true;
        camState.status = "PAIRED - SUBSCRIBING";
        discoverAndSubscribe();
    } else if (!passkeyPending) {
        camState.status = wrote ? "PAIR NOT ENCRYPTED" : "PAIR TRIGGER FAILED";
    }
}

void BlackmagicCamera::readIdentity() {
    if (modelChar && modelChar->canRead()) {
        NimBLEAttValue v = modelChar->readValue();
        if (v.size()) camState.model = String(v.c_str());
    }

    if (protocolChar && protocolChar->canRead()) {
        NimBLEAttValue v = protocolChar->readValue();
        if (v.size()) {
            bool printable = true;
            for (size_t i = 0; i < v.size(); i++) {
                uint8_t b = v[i];
                if (b < 32 || b > 126) { printable = false; break; }
            }
            if (printable) {
                camState.protocolVersion = String(v.c_str());
            } else {
                // Pocket 4K firmware 8.1 reports this as a NUL-padded text value.
                // Decode printable bytes first; fall back to hex only if that fails.
                String compact;
                for (size_t i = 0; i < v.size(); i++) {
                    uint8_t b = v[i];
                    if (b >= 32 && b <= 126) compact += (char)b;
                }
                if (compact.length()) {
                    camState.protocolVersion = compact;
                } else {
                    String hex;
                    for (size_t i = 0; i < v.size(); i++) {
                        char tmp[4];
                        snprintf(tmp, sizeof(tmp), "%02X", (unsigned)v[i]);
                        if (i) hex += ':';
                        hex += tmp;
                    }
                    camState.protocolVersion = hex;
                }
            }
        }
    }
}

bool BlackmagicCamera::triggerPairingByEncryptedWrite() {
    if (!statusChar || !client || !client->isConnected()) return false;
    uint8_t powerOn = 0x01;
    return statusChar->writeValue(&powerOn, 1, true);
}

bool BlackmagicCamera::discoverAndSubscribe() {
    if (!client || !client->isConnected() || !serviceReady) return false;

    // Preserve the v0.10.1 control path. Status/timecode subscriptions still
    // determine the normal subscription state exactly as before. The Incoming
    // Camera Control characteristic is telemetry-only in this build: failure to
    // subscribe to it must not take REC/STOP offline.
    bool coreOk = true;
    if (timecode && timecode->canNotify()) coreOk &= timecode->subscribe(true, timecodeNotify);
    if (statusChar && statusChar->canNotify()) coreOk &= statusChar->subscribe(true, statusNotify);

    incomingSubscribeOk = false;
    camState.incomingSubscription = "none";
    if (incoming) {
        if (incoming->canNotify()) {
            camState.incomingSubscription = "notify";
            incomingSubscribeOk = incoming->subscribe(true, incomingNotify);
        }
        if (!incomingSubscribeOk && incoming->canIndicate()) {
            camState.incomingSubscription = "indicate";
            incomingSubscribeOk = incoming->subscribe(false, incomingNotify);
        }
        if (!incomingSubscribeOk) camState.incomingSubscription = "failed";
    }

    subscriptionsReady = coreOk;
    camState.controlReady = coreOk && outgoing != nullptr;
    if (coreOk) {
        camState.paired = true;
        camState.status = "BMD CONTROL READY";
    } else {
        camState.controlReady = false;
        camState.status = "SUBSCRIBE FAIL";
    }
    return coreOk;
}

void BlackmagicCamera::loop() {
    // NimBLE authentication callbacks run in the BLE host context. Do the service
    // subscription work here instead of inside the callback so we do not block it.
    if (postAuthRequested && !passkeyPending && client && client->isConnected() && millis() >= postAuthAtMs) {
        postAuthRequested = false;
        camState.status = "AUTH OK - SETTING CONTROL";
        discoverAndSubscribe();
    }

    if (connectRequested && !connectTaskRunning) {
        connectTaskRunning = true;
        if (xTaskCreate(connectTaskThunk, "bmd-connect", 8192, this, 1, nullptr) != pdPASS) {
            connectTaskRunning = false;
            connectRequested = false;
            camState.status = "CONNECT TASK FAIL";
        }
    }

    if (reconnectWanted && !connectTaskRunning && !connectRequested && millis() >= nextReconnectMs && savedAddress.length()) {
        reconnectWanted = false;
        connectTo(savedAddress, savedAddressType);
    }
}

bool BlackmagicCamera::submitPasskey(uint32_t pin) {
    if (!passkeyPending || pin > 999999 || !client || !client->isConnected()) return false;
    NimBLEConnInfo ci = client->getConnInfo();
    if (ci.getConnHandle() != pendingConnHandle) return false;
    const bool ok = NimBLEDevice::injectPassKey(ci, pin);
    if (ok) camState.status = "PIN SUBMITTED";
    else camState.status = "PIN INJECT FAIL";
    return ok;
}

void BlackmagicCamera::ClientCallbacks::onConnect(NimBLEClient*) {
    o->camState.connected = true;
    o->camState.status = "BLE LINK UP";
}

void BlackmagicCamera::ClientCallbacks::onDisconnect(NimBLEClient*, int reason) {
    o->camState.connected = false;
    o->camState.ready = false;
    o->camState.recording = false;
    o->serviceReady = false;
    o->subscriptionsReady = false;
    o->camState.controlReady = false;
    o->postAuthRequested = false;
    o->passkeyPending = false;
    o->pendingConnHandle = BLE_HS_CONN_HANDLE_NONE;
    o->camState.status = "BMD OFFLINE (" + String(reason) + ")";
    if (o->savedAddress.length()) {
        o->reconnectWanted = true;
        o->nextReconnectMs = millis() + 2500;
    }
}

void BlackmagicCamera::ClientCallbacks::onPassKeyEntry(NimBLEConnInfo& connInfo) {
    o->pendingConnHandle = connInfo.getConnHandle();
    o->passkeyPending = true;
    o->camState.status = "ENTER CAMERA PIN";
}

void BlackmagicCamera::ClientCallbacks::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
    o->passkeyPending = false;
    o->pendingConnHandle = BLE_HS_CONN_HANDLE_NONE;
    if (!connInfo.isEncrypted()) {
        o->camState.status = "PAIR FAILED";
        o->camState.paired = false;
        return;
    }
    o->camState.paired = true;
    o->camState.status = "PAIR AUTH OK";
    o->postAuthAtMs = millis() + 150;
    o->postAuthRequested = true;
}

bool BlackmagicCamera::writeControlPacket(const uint8_t* data, size_t len) {
    if (!outgoing || !client || !client->isConnected()) {
        camState.lastWrite = "NO CONTROL LINK";
        camState.controlReady = false;
        return false;
    }

    NimBLEConnInfo ci = client->getConnInfo();
    if (!ci.isEncrypted()) {
        camState.lastWrite = "LINK NOT ENCRYPTED";
        camState.controlReady = false;
        return false;
    }

    bool ok = false;
    // Blackmagic's Outgoing Camera Control characteristic is a normal GATT write.
    // Prefer write-with-response, but fall back to write-without-response if that
    // is the property exposed by this camera/firmware revision.
    if (outgoing->canWrite()) {
        ok = outgoing->writeValue(data, len, true);
        camState.lastWrite = ok ? "WRITE RESPONSE OK" : "WRITE RESPONSE FAIL";
    }
    if (!ok && outgoing->canWriteNoResponse()) {
        ok = outgoing->writeValue(data, len, false);
        camState.lastWrite = ok ? "WRITE NO-RSP OK" : "WRITE NO-RSP FAIL";
    }
    if (!outgoing->canWrite() && !outgoing->canWriteNoResponse()) {
        camState.lastWrite = "OUTGOING NOT WRITABLE";
    }
    camState.controlReady = ok || subscriptionsReady;
    return ok;
}

bool BlackmagicCamera::setRecording(bool on) {
    // Blackmagic example packet: destination 255, command length 5, command id 0,
    // category Media(10), parameter Transport Mode(1), int8, assign, mode 2=Record / 0=Preview.
    static const uint8_t recPacket[12]  = {255, 5, 0, 0, 10, 1, 1, 0, 2, 0, 0, 0};
    static const uint8_t stopPacket[12] = {255, 5, 0, 0, 10, 1, 1, 0, 0, 0, 0, 0};
    const uint8_t* packet = on ? recPacket : stopPacket;
    camState.lastCommand = on ? "REC" : "STOP";
    bool ok = writeControlPacket(packet, 12);
    if (ok) {
        // Make the OSD react immediately to the command we just successfully sent.
        // Incoming camera notifications can subsequently confirm/correct this state.
        camState.recording = on;
        camState.status = on ? "REC" : "BMD READY";
    } else {
        camState.status = on ? "REC WRITE FAIL" : "STOP WRITE FAIL";
    }
    return ok;
}

bool BlackmagicCamera::toggleRecording() { return setRecording(!camState.recording); }
void BlackmagicCamera::disconnect() { if (client && client->isConnected()) client->disconnect(); }

void BlackmagicCamera::forgetPairing() {
    disconnect();
    NimBLEDevice::deleteAllBonds();
    savedAddress = "";
    connectedAddress = "";
    requestedAddress = "";
    connectRequested = false;
    reconnectWanted = false;
    camState = CameraState{};
    camState.status = "PAIRING CLEARED";
}

void BlackmagicCamera::incomingNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (!instance) return;
    instance->incomingPacketCount++;
    instance->camState.incomingPackets = (uint32_t)instance->incomingPacketCount;
    instance->parseIncoming(data, len);
}
void BlackmagicCamera::timecodeNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (instance) instance->parseTimecode(data, len);
}
void BlackmagicCamera::statusNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (instance) instance->parseStatus(data, len);
}

void BlackmagicCamera::parseStatus(const uint8_t* data, size_t len) {
    if (!len) return;
    uint8_t f = data[0];
    camState.connected = f & 0x02;
    camState.paired = f & 0x04;
    camState.ready = f & 0x20;
    if (camState.ready) {
        camState.controlReady = true;
        camState.status = camState.recording ? "REC" : "BMD READY";
    }
}



void BlackmagicCamera::refreshMediaRemaining() {
    for (int i = 0; i < 3; i++) {
        if (!mediaSecondsSeen || mediaSeconds[i] == 0) {
            camState.mediaSlotRemaining[i] = "--";
            continue;
        }
        const uint16_t seconds = mediaSeconds[i];
        char remaining[12];
        const unsigned hours = seconds / 3600u;
        const unsigned minutes = (seconds % 3600u) / 60u;
        const unsigned secs = seconds % 60u;
        snprintf(remaining, sizeof(remaining), "%02u:%02u:%02u", hours, minutes, secs);
        camState.mediaSlotRemaining[i] = remaining;
    }

    // Do not guess when multiple media devices are installed. Once 10:1 has
    // told us which slot is active, use only that slot's 9:2 remaining time.
    if (activeMediaSeen) {
        const int slot = camState.activeMediaSlot;
        if (slot >= 1 && slot <= 3) camState.mediaRemaining = camState.mediaSlotRemaining[slot - 1];
        else camState.mediaRemaining = "--";
        return;
    }

    // Before the first 10:1 update arrives, a single populated 9:2 slot is
    // unambiguous and preserves useful startup telemetry without guessing.
    int onlySlot = 0;
    int populated = 0;
    for (int i = 0; i < 3; i++) {
        if (mediaSeconds[i] != 0) {
            onlySlot = i + 1;
            populated++;
        }
    }
    camState.mediaRemaining = (populated == 1) ? camState.mediaSlotRemaining[onlySlot - 1] : "--";
}

void BlackmagicCamera::parseIncoming(const uint8_t* data, size_t len) {
    // Keep a short raw snapshot in the web diagnostics. This is invaluable when a
    // camera firmware revision sends a packet we have not decoded yet.
    String hex;
    const size_t dumpLen = len > 48 ? 48 : len;
    for (size_t i = 0; i < dumpLen; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X", (unsigned)data[i]);
        if (i) hex += ' ';
        hex += b;
    }
    camState.lastIncoming = hex;

    size_t p = 0;
    while (p + 4 <= len) {
        const uint8_t cmdLen = data[p + 1];
        const size_t raw = 4u + (size_t)cmdLen;
        const size_t padded = (raw + 3u) & ~((size_t)3u);
        if (cmdLen < 4 || p + raw > len) break;

        const uint8_t cmd = data[p + 2];
        if (cmd == 0) { // Change Configuration
            const uint8_t category = data[p + 4];
            const uint8_t parameter = data[p + 5];
            const uint8_t dataType = data[p + 6];
            const uint8_t operation = data[p + 7];
            const size_t valueLen = cmdLen - 4;
            const uint8_t* value = &data[p + 8];

            // Pocket 4K firmware 8.1 category 9 / parameter 2 telemetry. Hardware
            // captures proved this is an array of little-endian uint16 remaining-time
            // counters in seconds. Slots 1..3 map to byte pairs 0..1, 2..3 and 4..5.
            if (category == 9 && parameter == 2 && dataType == 2 && operation == 2 && valueLen >= 2) {
                const size_t slots = min((size_t)3, valueLen / 2u);
                for (size_t i = 0; i < slots; i++) {
                    mediaSeconds[i] = (uint16_t)value[i * 2u] | ((uint16_t)value[i * 2u + 1u] << 8);
                }
                for (size_t i = slots; i < 3; i++) mediaSeconds[i] = 0;
                mediaSecondsSeen = true;
                refreshMediaRemaining();
            }

            // Media / Transport Mode. The documented flags byte uses bit 5 for
            // disk 1 and bit 6 for disk 2. Pocket 4K hardware capture shows bit 4
            // for its third media slot (USB), yielding 0x10 when slot 3 is active.
            // A zero active-slot mask is treated as no active media and clears OSD.
            // Camera-originated transport telemetry on the Pocket 4K arrives with
            // operation 2, while control writes use operation 0. Accept both; operation
            // 1 is an offset/toggle command and is deliberately ignored here.
            if (category == 10 && parameter == 1 && dataType == 1 &&
                (operation == 0 || operation == 2) && valueLen >= 1) {
                const uint8_t mode = value[0];
                camState.recording = (mode == 2);
                camState.status = camState.recording ? "REC" : "BMD READY";

                if (valueLen >= 3) {
                    const uint8_t flags = value[2];
                    int slot = 0;
                    if (flags & 0x20) slot = 1;
                    else if (flags & 0x40) slot = 2;
                    else if (flags & 0x10) slot = 3;
                    camState.activeMediaSlot = slot;
                    activeMediaSeen = true;
                    refreshMediaRemaining();
                }
            }
        }

        if (padded == 0) break;
        p += padded;
    }
}

void BlackmagicCamera::parseTimecode(const uint8_t* data, size_t len) {
    if (len < 4) return;
    uint32_t v = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    uint8_t ff = ((v >> 4) & 0x0f) * 10 + (v & 0x0f);
    uint8_t ss = ((v >> 12) & 0x0f) * 10 + ((v >> 8) & 0x0f);
    uint8_t mm = ((v >> 20) & 0x0f) * 10 + ((v >> 16) & 0x0f);
    uint8_t hh = ((v >> 28) & 0x0f) * 10 + ((v >> 24) & 0x0f);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u:%02u", hh, mm, ss, ff);
    camState.timecode = buf;
}
