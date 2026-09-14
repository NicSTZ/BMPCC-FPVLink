#include <Arduino.h>
#include <WiFi.h>
#include "config/Settings.h"
#include "msp/MspClient.h"
#include "camera/BlackmagicCamera.h"
#include "web/WebUi.h"

HardwareSerial FcSerial(1);
SettingsStore settingsStore;
AppSettings settings;
MspClient msp(FcSerial);
BlackmagicCamera camera;
WebUi* web = nullptr;

static constexpr int ESP_RX_PIN = 6;
static constexpr int ESP_TX_PIN = 7;
static constexpr uint32_t MSP_BAUD = 115200;

static uint32_t lastRcRequest = 0, lastApiRequest = 0, lastOsdUpdate = 0, lastRecordAttempt = 0;
static bool recordMapInitialized = false;
static bool lastAppliedRecordState = false;
static bool lastControlReady = false;
static uint32_t wifiStartedAt = 0;
static constexpr uint32_t WIFI_SETUP_WINDOW_MS = 90000;

static bool recordSwitchState(bool& valid) {
    const int idx = settings.recordChannel - 1;
    valid = idx >= 0 && (size_t)idx < msp.rcCount() && msp.rcFresh();
    if (!valid) return false;
    const uint16_t value = msp.rcValue((size_t)idx);
    return settings.recordActiveHigh ? value > settings.recordThreshold : value < settings.recordThreshold;
}

static String osdStatusText() {
    const CameraState& c = camera.state();
    if (!c.connected) return "CAM OFFLINE";
    if (camera.waitingForPasskey()) return "CAM ENTER PIN";
    if (c.recording) return "REC";
    if (c.controlReady || c.ready || c.paired) return "STBY";
    return "CAM WAIT";
}

static String osdMediaText() {
    const CameraState& c = camera.state();
    if (!c.connected) return "MEDIA --";
    if (c.mediaRemaining.length() && c.mediaRemaining != "--") return "MEDIA " + c.mediaRemaining;
    return "MEDIA --";
}

void setup() {
    Serial.begin(115200);
    delay(250);
    settingsStore.begin();
    settings = settingsStore.load();

    // ESP32-C3 SuperMini hardware profile. Keep these fixed so wiring is predictable.
    msp.begin(ESP_RX_PIN, ESP_TX_PIN, MSP_BAUD);

    // Bring the setup AP up before starting BLE. Wi-Fi and BLE share the C3's
    // 2.4 GHz radio, so giving SoftAP a clean head start makes setup discovery
    // more predictable without changing the proven Blackmagic BLE control path.
    uint64_t mac = ESP.getEfuseMac();
    char ap[32]; snprintf(ap,sizeof(ap),"BMPCC-FPVLink-%04X",(uint16_t)(mac&0xffff));
    web = new WebUi(settings,settingsStore,camera,msp);
    web->begin(ap);
    wifiStartedAt = millis();
    delay(750);

    camera.begin();
    camera.setSavedTarget(settings.cameraAddress, settings.cameraAddressType);

    if (settings.autoConnect && settings.cameraAddress.length()) {
        camera.connectTo(settings.cameraAddress,settings.cameraAddressType);
    } else {
    }
    msp.requestApiVersion();
}

void loop() {
    msp.loop();
    camera.loop();
    if(web) web->loop();

    const uint32_t now=millis();

    if(now-lastRcRequest>=100){ lastRcRequest=now; msp.requestRc(); }
    if(now-lastApiRequest>=5000){ lastApiRequest=now; msp.requestApiVersion(); }

    bool mappingValid = false;
    const bool desiredRecordState = recordSwitchState(mappingValid);
    const CameraState& c = camera.state();

    // When the camera control link comes back, re-apply the physical switch position once.
    // This avoids losing a REC/STOP state across camera reconnect/authentication.
    if (c.controlReady && !lastControlReady) recordMapInitialized = false;
    lastControlReady = c.controlReady;

    if (mappingValid && c.controlReady) {
        const bool changed = !recordMapInitialized || desiredRecordState != lastAppliedRecordState;
        if (changed && now-lastRecordAttempt >= 300) {
            lastRecordAttempt = now;
            if (camera.setRecording(desiredRecordState)) {
                lastAppliedRecordState = desiredRecordState;
                recordMapInitialized = true;
            }
        }
    }

    if(now-lastOsdUpdate>=500){
        lastOsdUpdate=now;
        msp.setCustomText(settings.osdSlot, osdStatusText());
        // The next Custom Message slot carries decoded Pocket 4K remaining
        // record duration from category 9 / parameter 2 telemetry.
        if (settings.osdSlot < 3) msp.setCustomText(settings.osdSlot + 1, osdMediaText());
    }

    // Setup Wi-Fi is temporary. If nobody joins the AP within 90 seconds,
    // shut Wi-Fi down and leave BLE + MSP + OSD running. If a phone/laptop is
    // connected, keep setup alive until it disconnects or the user presses
    // "Disable Wi-Fi now" in the configurator. Wi-Fi returns on every reboot.
    if (web && web->active() && (now - wifiStartedAt >= WIFI_SETUP_WINDOW_MS) && WiFi.softAPgetStationNum() == 0) {
        web->stopWifi();
    }

    delay(2);
}
