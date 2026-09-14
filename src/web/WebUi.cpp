#include "WebUi.h"
#include <WiFi.h>

const char WebUi::PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name=viewport content="width=device-width,initial-scale=1"><title>FPVCineCam32</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif;max-width:820px;margin:24px auto;padding:0 16px;background:#111;color:#eee}
h1{margin-bottom:4px}.sub{color:#aaa;margin-bottom:16px}.card{background:#1c1c1e;border-radius:14px;padding:16px;margin:14px 0}h3{margin-top:0}
button,input,select{font-size:16px;padding:10px;margin:5px 5px 5px 0;border-radius:8px;border:1px solid #555;background:#29292c;color:#fff}button{cursor:pointer}.ok{color:#6ee787}.warn{color:#ffd866}.muted{color:#aaa}.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.channels{display:grid;grid-template-columns:repeat(4,1fr);gap:7px}.ch{background:#252528;border-radius:8px;padding:8px;text-align:center}.ch b{display:block;font-size:13px;color:#aaa}.ch span{font-size:18px}.selected{outline:2px solid #6ee787}.statusBadge{display:inline-flex;align-items:center;gap:7px;padding:6px 10px;border-radius:999px;font-weight:600;margin-bottom:8px}.statusBadge::before{content:"";width:10px;height:10px;border-radius:50%;background:currentColor}.statusOnline{color:#6ee787;background:#17351f}.statusOffline{color:#ff6b6b;background:#3a1b1b}pre{white-space:pre-wrap;word-break:break-word}@media(max-width:600px){.grid{grid-template-columns:1fr}.channels{grid-template-columns:repeat(2,1fr)}}
</style></head><body>
<h1>FPVCineCam32 <small>v0.10.10 ACTIVE MEDIA FIX</small></h1><div class=sub>Blackmagic + Betaflight MSP | active media remaining time</div>

<div class=card><h3>Blackmagic Pocket Cinema Camera</h3>
<div id=camBadge class="statusBadge statusOffline">Camera disconnected</div><div id=camSummary class=muted>Loading...</div>
<div id=pin style="display:none"><p class=warn>Enter the 6-digit PIN shown on the BMPCC 4K:</p><input id=pinval inputmode=numeric maxlength=6 placeholder=123456><button onclick=sendPin()>Submit PIN</button></div>
<p><button onclick=scan()>Scan for cameras</button><span id=cams></span></p>
<p><button onclick="rec(1)">REC test</button><button onclick="rec(0)">STOP test</button><button onclick=forget()>Forget pairing</button></p>
<hr style="border-color:#333">
<h4>REC / STOP switch mapping</h4>
<div class=grid>
<label>RC channel<select id=ch></select></label>
<label>Threshold<input id=thr type=number min=800 max=2200></label>
<label>Record when<select id=high><option value=1>Above threshold</option><option value=0>Below threshold</option></select></label>
<label>Live selected channel<input id=selectedValue readonly></label>
</div>
<button onclick=saveMapping()>Save mapping</button><span id=saveMsg class=muted></span>
</div>

<div class=card><h3>Betaflight / MSP</h3>
<p>ESP32-C3 SuperMini wiring is fixed: <b>FC TX -> GPIO6 (ESP RX)</b>, <b>FC RX -> GPIO7 (ESP TX)</b>, <b>GND -> GND</b>. Enable <b>MSP at 115200</b> on that Betaflight UART.</p>
<div id=mspSummary class=muted>Waiting for FC...</div>
<h4>Live RC channels</h4><div id=channels class=channels></div>
<p class=muted id=mspStats></p>
<label>OSD Custom Message slot <select id=slot><option>0</option><option>1</option><option>2</option><option>3</option></select></label>
<button onclick=saveOsd()>Save OSD slot</button><button onclick=testosd()>Send OSD test</button>
<p><b>Status message:</b> <span id=osdLive class=muted>Waiting...</span></p><p class=muted>REC/STBY remains unchanged. The next Custom Message slot now shows decoded media remaining from the Pocket 4K.</p>
</div>

<div class=card><h3>Setup Wi-Fi</h3>
<p class=muted>Wi-Fi is only for configuration. If no phone/computer joins within 90 seconds of boot, it switches off automatically. BLE camera control, MSP and OSD continue normally. Wi-Fi comes back on every reboot.</p>
<button onclick=wifiOff()>Disable Wi-Fi now</button>
</div>

<div class=card><h3>Diagnostics</h3><pre id=status>Loading...</pre><button onclick=refresh()>Refresh</button></div>

<script>
const el=id=>document.getElementById(id);
for(let i=1;i<=16;i++){const o=document.createElement('option');o.value=i;o.textContent='CH'+i;el('ch').appendChild(o)}
async function api(url,opt){const r=await fetch(url,opt);if(!r.ok)throw new Error(`HTTP ${r.status}`);return await r.json()}
function cameraLine(c){
  const link=c.connected?'Connected':'Offline';
  const ready=c.controlReady?'Control ready':'Control not ready';
  const rec=c.recording?'RECORDING':'Standby';
  return `${link} | ${ready} | ${rec} | ${c.timecode}`;
}
function osdPreview(c){
  if(!c.connected) return 'BMD OFFLINE';
  if(c.waitingPin) return 'BMD ENTER PIN';
  if(c.recording) return `REC ${c.timecode}`;
  if(c.ready || c.paired) return `BMD STBY ${c.timecode}`;
  return c.status || 'BMD';
}
function drawChannels(s){
  const box=el('channels');box.innerHTML='';
  for(let i=0;i<16;i++){
    const d=document.createElement('div');d.className='ch'+((i+1)==s.settings.channel?' selected':'');
    const v=(s.msp.channels&&i<s.msp.channels.length)?s.msp.channels[i]:0;
    d.innerHTML=`<b>CH${i+1}</b><span>${v||'--'}</span>`;box.appendChild(d);
  }
  const idx=s.settings.channel-1;
  el('selectedValue').value=(s.msp.channels&&idx>=0&&idx<s.msp.channels.length)?s.msp.channels[idx]:'--';
}
async function refresh(){
  try{
    const s=await api('/api/status');
    el('status').textContent=JSON.stringify(s,null,2);
    el('pin').style.display=s.camera.waitingPin?'block':'none';
    el('camSummary').textContent=cameraLine(s.camera)+(s.camera.model?` | ${s.camera.model}`:'');
    const linked=s.camera.connected && s.camera.controlReady;
    el('camBadge').className='statusBadge '+(linked?'statusOnline':'statusOffline');
    el('camBadge').textContent=linked?'Camera connected':'Camera disconnected';
    el('osdLive').textContent=osdPreview(s.camera);
    el('mspSummary').textContent=s.msp.connected?`MSP connected | API ${s.msp.api} | last RC response ${s.msp.responseMs} ms`:'MSP offline - check UART wiring and Betaflight Ports';
    el('mspStats').textContent=`Responses: ${s.msp.responses} | Timeouts: ${s.msp.timeouts} | Invalid frames: ${s.msp.invalidFrames}`;
    el('ch').value=s.settings.channel;el('thr').value=s.settings.threshold;el('high').value=s.settings.high?1:0;el('slot').value=s.settings.slot;
    drawChannels(s);
  }catch(e){el('status').textContent='Status error: '+e.message;el('mspSummary').textContent='ESP web API unavailable'}
}
async function scan(){
  el('cams').textContent='Scanning...';
  try{const x=await api('/api/scan');el('cams').innerHTML='';if(!x.length){el('cams').textContent=' No Blackmagic cameras found';return}x.forEach(c=>{const b=document.createElement('button');b.textContent=(c.name||'Blackmagic')+' '+c.address;b.onclick=()=>connect(c.address,c.type);el('cams').appendChild(b)})}catch(e){el('cams').textContent='Scan error: '+e.message}
}
async function connect(a,t){try{await api('/api/connect?address='+encodeURIComponent(a)+'&type='+t)}catch(e){alert('Connect error: '+e.message)}setTimeout(refresh,250)}
async function sendPin(){const v=el('pinval').value.trim();if(!/^\d{6}$/.test(v)){alert('Enter the 6-digit PIN shown on the camera');return}await api('/api/pin?value='+v);setTimeout(refresh,400)}
async function forget(){await api('/api/forget');refresh()}
async function rec(v){await api('/api/record?on='+v);setTimeout(refresh,250)}
async function saveMapping(){await api(`/api/saveMapping?ch=${el('ch').value}&thr=${el('thr').value}&high=${el('high').value}`);el('saveMsg').textContent='Saved';setTimeout(()=>el('saveMsg').textContent='',1200);refresh()}
async function saveOsd(){await api('/api/saveOsd?slot='+el('slot').value);refresh()}
async function testosd(){await api('/api/osdtest')}
async function wifiOff(){try{await api('/api/wifioff');}catch(e){} }

setInterval(refresh,1500);refresh();
</script></body></html>)HTML";

void WebUi::begin(const String& apName) {
    const uint32_t t0 = millis();
    WiFi.mode(WIFI_AP);
    const bool apOk = WiFi.softAP(apName.c_str(), "fpvcinecam32");
    Serial.printf("[%8lu ms] WIFI: softAP() returned %s in %lu ms, mode=%d, ip=%s\n",
                  (unsigned long)millis(), apOk ? "TRUE" : "FALSE",
                  (unsigned long)(millis() - t0), (int)WiFi.getMode(),
                  WiFi.softAPIP().toString().c_str());
    routes();
    server.begin();
    running=true;
    Serial.printf("[%8lu ms] WIFI: web server started\n", (unsigned long)millis());
}
void WebUi::loop(){ if(running) server.handleClient(); if(stopRequested && millis() >= stopAtMs){ stopRequested=false; stopWifi(); } }
void WebUi::stopWifi(){
    if(!running)return;
    Serial.printf("[%8lu ms] WIFI: stopping AP, stations=%u\n", (unsigned long)millis(), (unsigned)WiFi.softAPgetStationNum());
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    running=false;
    Serial.printf("[%8lu ms] WIFI: stopped, mode=%d\n", (unsigned long)millis(), (int)WiFi.getMode());
}

String WebUi::statusJson(){
    const CameraState& c=cam.state();
    String j="{\"camera\":{";
    j += "\"status\":\""+c.status+"\",\"model\":\""+c.model+"\",\"protocol\":\""+c.protocolVersion+"\",\"connected\":"+String(c.connected?"true":"false")+",\"paired\":"+String(c.paired?"true":"false")+",\"ready\":"+String(c.ready?"true":"false")+",\"controlReady\":"+String(c.controlReady?"true":"false")+",\"recording\":"+String(c.recording?"true":"false")+",\"timecode\":\""+c.timecode+"\",\"mediaRemaining\":\""+c.mediaRemaining+"\",\"activeMediaSlot\":"+String(c.activeMediaSlot)+",\"mediaSlots\":[\""+c.mediaSlotRemaining[0]+"\",\""+c.mediaSlotRemaining[1]+"\",\""+c.mediaSlotRemaining[2]+"\"],\"incomingSubscription\":\""+c.incomingSubscription+"\",\"incomingPackets\":"+String(c.incomingPackets)+",\"lastIncoming\":\""+c.lastIncoming+"\",\"waitingPin\":"+String(cam.waitingForPasskey()?"true":"false")+",\"lastCommand\":\""+c.lastCommand+"\",\"lastWrite\":\""+c.lastWrite+"\"},";
    j += "\"msp\":{\"connected\":"+String(mspClient.connected()?"true":"false")+",\"rcFresh\":"+String(mspClient.rcFresh()?"true":"false")+",\"api\":\""+String(mspClient.apiMajor())+"."+String(mspClient.apiMinor())+"\",\"responseMs\":"+String(mspClient.lastResponseMs())+",\"responses\":"+String(mspClient.responses())+",\"timeouts\":"+String(mspClient.timeouts())+",\"invalidFrames\":"+String(mspClient.invalidFrames())+",\"channels\":[";
    const size_t count = min(mspClient.rcCount(), (size_t)16);
    for(size_t i=0;i<count;i++){ if(i)j+=','; j+=String(mspClient.rcValue(i)); }
    j += "]},";
    j += "\"settings\":{\"rx\":6,\"tx\":7,\"baud\":115200,\"channel\":"+String(s.recordChannel)+",\"threshold\":"+String(s.recordThreshold)+",\"high\":"+String(s.recordActiveHigh?"true":"false")+",\"slot\":"+String(s.osdSlot)+"}}";
    return j;
}

void WebUi::routes(){
    server.on("/",HTTP_GET,[this](){server.send_P(200,"text/html",PAGE);});
    server.on("/api/wifioff",HTTP_GET,[this](){ server.send(200,"application/json","{\"ok\":true}"); stopRequested=true; stopAtMs=millis()+250; });
    server.on("/api/status",HTTP_GET,[this](){server.send(200,"application/json",statusJson());});

    server.on("/api/scan",HTTP_GET,[this](){String j;cam.startScan(j);server.send(200,"application/json",j);});
    server.on("/api/connect",HTTP_GET,[this](){String a=server.arg("address");uint8_t t=(uint8_t)server.arg("type").toInt();bool ok=cam.connectTo(a,t);if(ok){s.cameraAddress=a;s.cameraAddressType=t;prefs.save(s);cam.setSavedTarget(a,t);}server.send(200,"application/json",String("{\"ok\":")+(ok?"true":"false")+"}");});
    server.on("/api/pin",HTTP_GET,[this](){uint32_t p=(uint32_t)server.arg("value").toInt();bool ok=cam.submitPasskey(p);server.send(200,"application/json",String("{\"ok\":")+(ok?"true":"false")+"}");});
    server.on("/api/forget",HTTP_GET,[this](){cam.forgetPairing();prefs.clearCamera();s.cameraAddress="";server.send(200,"application/json","{\"ok\":true}");});
    server.on("/api/record",HTTP_GET,[this](){bool on=server.arg("on").toInt()!=0;bool ok=cam.setRecording(on);server.send(200,"application/json",String("{\"ok\":")+(ok?"true":"false")+"}");});
    server.on("/api/osdtest",HTTP_GET,[this](){mspClient.setCustomText(s.osdSlot,"REC TEST"); if(s.osdSlot<3)mspClient.setCustomText(s.osdSlot+1,"MEDIA TEST"); server.send(200,"application/json","{\"ok\":true}");});
    server.on("/api/saveMapping",HTTP_GET,[this](){s.recordChannel=constrain(server.arg("ch").toInt(),1,16);s.recordThreshold=constrain(server.arg("thr").toInt(),800,2200);s.recordActiveHigh=server.arg("high").toInt()!=0;prefs.save(s);server.send(200,"application/json","{\"ok\":true}");});
    server.on("/api/saveOsd",HTTP_GET,[this](){s.osdSlot=constrain(server.arg("slot").toInt(),0,3);prefs.save(s);server.send(200,"application/json","{\"ok\":true}");});
}
