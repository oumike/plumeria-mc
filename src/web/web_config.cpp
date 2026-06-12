#include "web/web_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {

constexpr uint32_t kStaConnectTimeoutMs = 10000;
constexpr uint32_t kRebootDelayMs = 1500;
constexpr uint32_t kNtpSyncTimeoutMs = 6000;
constexpr time_t kTimeValidEpoch = 1700000000;
constexpr char kPrefsNs[] = "plumeria_web";
constexpr char kDefaultSsid[] = "rhinohome";
constexpr char kDefaultPass[] = "fishfood is smelly";
constexpr char kDefaultNodeName[] = "Plumeria";
constexpr char kDefaultTimezone[] = "UTC0";
constexpr char kDefaultRegion[] = "US";
constexpr float kDefaultBwKhz = 62.5f;
constexpr uint8_t kDefaultSf = 8;
constexpr uint8_t kDefaultCr = 5;
constexpr char kFallbackApSsid[] = "plumeria-mc";

struct RegionPreset {
  const char* id;
  float frequency_mhz;
  float bandwidth_khz;
  uint8_t spreading_factor;
  uint8_t coding_rate;
  int8_t tx_power_dbm;
};

constexpr RegionPreset kRegionPresets[] = {
  {"US", 910.525f, 62.5f, 7, 5, 22},
    {"EU_868", 869.525f, 62.5f, 8, 5, 22},
    {"EU_433", 433.500f, 62.5f, 8, 5, 10},
    {"ANZ", 921.500f, 62.5f, 8, 5, 22},
    {"JP", 922.000f, 62.5f, 8, 5, 13},
    {"KR", 921.500f, 62.5f, 8, 5, 22},
    {"IN", 866.000f, 62.5f, 8, 5, 22},
    {"TH", 922.500f, 62.5f, 8, 5, 16},
    {"BR_902", 904.750f, 62.5f, 8, 5, 22},
};

WebServer g_server(80);
bool g_running = false;
char g_mode[8] = "off";
char g_ip[20] = "";
plumeria::mesh::MeshAdapter* g_mesh = nullptr;
plumeria::web::WebSettings g_settings{};
bool g_reboot_pending = false;
uint32_t g_reboot_at_ms = 0;
char g_channels_web_buf[40][32]{};

void applyTimezoneOffsetFromSettings() {
  // POSIX TZ uses reversed sign: local = UTC - value.
  const int local_offset_min = static_cast<int>(g_settings.timezone_offset_minutes);
  int posix_value_min = -local_offset_min;
  bool neg = posix_value_min < 0;
  int abs_min = neg ? -posix_value_min : posix_value_min;
  int hours = abs_min / 60;
  int mins = abs_min % 60;

  char tz_buf[24] = {};
  if (mins == 0) {
    snprintf(tz_buf, sizeof(tz_buf), "UTC%s%d", neg ? "-" : "", hours);
  } else {
    snprintf(tz_buf, sizeof(tz_buf), "UTC%s%d:%02d", neg ? "-" : "", hours, mins);
  }

  setenv("TZ", tz_buf, 1);
  tzset();
}

void copyString(char* dst, size_t dst_size, const char* src) {
  if (!dst || dst_size == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

const RegionPreset* findRegion(const char* id) {
  if (!id || id[0] == '\0') {
    return nullptr;
  }

  for (size_t i = 0; i < (sizeof(kRegionPresets) / sizeof(kRegionPresets[0])); i++) {
    if (strcmp(kRegionPresets[i].id, id) == 0) {
      return &kRegionPresets[i];
    }
  }
  return nullptr;
}

void saveSettings(const plumeria::web::WebSettings& settings) {
  Preferences prefs;
  prefs.begin(kPrefsNs, false);
  prefs.putString("node_name", settings.node_name);
  prefs.putString("wifi_ssid", settings.wifi_ssid);
  prefs.putString("wifi_pass", settings.wifi_pass);
  prefs.putString("timezone", settings.timezone);
  prefs.putInt("tz_offset", static_cast<int>(settings.timezone_offset_minutes));
  prefs.putString("region", settings.region);
  prefs.putFloat("lora_freq", settings.lora_freq_mhz);
  prefs.putFloat("lora_bw", settings.lora_bw_khz);
  prefs.putUChar("lora_sf", settings.lora_sf);
  prefs.putUChar("lora_cr", settings.lora_cr);
  prefs.putChar("lora_pwr", settings.lora_tx_power_dbm);
  prefs.end();
}

void setIpFrom(const IPAddress& address) {
  String ip = address.toString();
  copyString(g_ip, sizeof(g_ip), ip.c_str());
}

bool syncTimeFromNtp() {
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov", "time.google.com");

  const uint32_t start = millis();
  while (millis() - start < kNtpSyncTimeoutMs) {
    time_t now = time(nullptr);
    if (now >= kTimeValidEpoch) {
      applyTimezoneOffsetFromSettings();
      return true;
    }
    delay(100);
  }

  return false;
}

bool connectSta(const char* ssid, const char* pass) {
  if (!ssid || ssid[0] == '\0') {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass ? pass : "");

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= kStaConnectTimeoutMs) {
      return false;
    }
    delay(100);
  }

  copyString(g_mode, sizeof(g_mode), "sta");
  setIpFrom(WiFi.localIP());

  if (syncTimeFromNtp()) {
    Serial.println("[WEB] NTP time synced");
  } else {
    Serial.println("[WEB] NTP time sync timed out");
  }

  return true;
}

void startFallbackAp() {
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(kFallbackApSsid);
  delay(100);

  copyString(g_mode, sizeof(g_mode), "ap");
  setIpFrom(WiFi.softAPIP());
}

void bringupNetwork() {
  if (!connectSta(g_settings.wifi_ssid, g_settings.wifi_pass)) {
    startFallbackAp();
  }
}

String jsonString(const char* raw) {
  if (!raw) {
    return String("\"\"");
  }

  String escaped = "\"";
  for (const char* p = raw; *p; ++p) {
    if (*p == '"') {
      escaped += "\\\"";
    } else if (*p == '\\') {
      escaped += "\\\\";
    } else {
      escaped += *p;
    }
  }
  escaped += "\"";
  return escaped;
}

void sendJsonOk(const String& payload) {
  g_server.send(200, "application/json", payload);
}

void sendJsonError(const char* message, int status = 400) {
  String payload = "{\"ok\":false,\"error\":";
  payload += jsonString(message);
  payload += "}";
  g_server.send(status, "application/json", payload);
}

void handleRoot() {
  String region_options;
  String region_defaults_js = "{";
  for (size_t i = 0; i < (sizeof(kRegionPresets) / sizeof(kRegionPresets[0])); i++) {
    if (i != 0) {
      region_defaults_js += ",";
    }

    region_defaults_js += "\"";
    region_defaults_js += kRegionPresets[i].id;
    region_defaults_js += "\":{";
    region_defaults_js += "\"freq\":";
    region_defaults_js += String(kRegionPresets[i].frequency_mhz, 3);
    region_defaults_js += ",\"bw\":";
    region_defaults_js += String(kRegionPresets[i].bandwidth_khz, 1);
    region_defaults_js += ",\"sf\":";
    region_defaults_js += String(kRegionPresets[i].spreading_factor);
    region_defaults_js += ",\"cr\":";
    region_defaults_js += String(kRegionPresets[i].coding_rate);
    region_defaults_js += ",\"pwr\":";
    region_defaults_js += String(kRegionPresets[i].tx_power_dbm);
    region_defaults_js += "}";

    region_options += "<option value='";
    region_options += kRegionPresets[i].id;
    region_options += "'";
    if (strcmp(kRegionPresets[i].id, g_settings.region) == 0) {
      region_options += " selected";
    }
    region_options += ">";
    region_options += kRegionPresets[i].id;
    region_options += "</option>";
  }
  region_defaults_js += "}";

  String html =
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Plumeria Config</title>"
      "<style>"
      "body{font-family:system-ui,Segoe UI,Arial,sans-serif;background:#0e1622;color:#e7eef6;margin:0;padding:14px;}"
      ".wrap{max-width:620px;margin:0 auto;}"
      "h1{margin:0 0 10px;color:#9fe7ff;font-size:1.25rem;}"
      "section{background:#162333;border:1px solid #2a435d;border-radius:8px;padding:10px;margin:0 0 10px;}"
      "label{display:block;font-size:.85rem;color:#b7cadd;margin-top:6px;}"
      "input,select,button{width:100%;box-sizing:border-box;padding:8px;border-radius:6px;border:1px solid #355674;background:#0f1a28;color:#e7eef6;}"
      "button{margin-top:8px;background:#2c9bc8;border-color:#2c9bc8;font-weight:700;}"
      "small{color:#9bb1c5;}"
      ".row{display:grid;grid-template-columns:1fr 1fr;gap:8px;}"
      "@media(max-width:520px){.row{grid-template-columns:1fr;}}"
      "ul{margin:8px 0 0;padding-left:18px;}"
      "li{margin:4px 0;}"
      ".meta{font-size:.82rem;color:#9bb1c5;}"
      "</style></head><body><div class='wrap'>"
      "<h1>Plumeria Web Config</h1>"
      "<section><div id='status' class='meta'>Loading status...</div></section>"
      "<section><h3>Identity</h3>"
      "<label>Node Name<input id='node_name' maxlength='31' value=''></label>"
      "<small>Used for node adverts and sender name in channel chats.</small>"
      "</section>"
      "<section><h3>Radio (Sigurd/MeshCore-style)</h3>"
      "<div class='row'><label>Region<select id='region' onchange='applyRegionPreset();markRadioDirty();'>" +
      region_options +
      "</select></label>"
      "<label>&nbsp;<button type='button' onclick='applyRegionPreset()' style='margin-top:22px'>Apply Region Defaults</button></label></div>"
      "<div class='row'><label>Frequency MHz<input id='freq' type='number' step='0.001'></label>"
      "<label>Bandwidth kHz<input id='bw' type='number' step='0.1'></label></div>"
      "<div class='row'><label>Spreading Factor<input id='sf' type='number' min='5' max='12' step='1'></label>"
      "<label>Coding Rate (4/x)<input id='cr' type='number' min='5' max='8' step='1'></label></div>"
      "<label>TX Power dBm<input id='pwr' type='number' min='1' max='30' step='1'></label>"
      "<small>This firmware uses explicit LoRa values (freq, bw, sf, cr, tx power), not modem profile names.</small>"
      "</section>"
      "<section><h3>Channels</h3><div class='row'>"
      "<label>Name<input id='ch_name' placeholder='Public or #rhino'></label>"
      "<label>PSK Base64 (optional for #channels)<input id='ch_psk' placeholder='izOH6cXN6mrJ5e26oRXNcg=='></label>"
      "</div><button onclick='addChannel()'>Add Channel</button>"
      "<small>#channels derive encryption key from SHA-256(channel name); PSK is ignored for #channels.</small>"
      "<ul id='channels'></ul></section>"
      "<section><h3>Timezone</h3>"
      "<label>Timezone<select id='timezone'></select></label>"
      "<small>Timezone list is populated from browser-supported IANA zones.</small>"
      "</section>"
      "<section><h3>Wi-Fi</h3>"
      "<label>SSID<input id='wifi_ssid' value=''></label>"
      "<label>Password<input id='wifi_pass' value=''></label>"
      "<small>Wi-Fi settings are applied after saving and rebooting.</small>"
      "</section>"
      "<section><button onclick='saveAll()'>Save Settings</button>"
      "<small>Reboots only when Wi-Fi or radio settings change.</small></section>"
      "<script>"
      "const regionDefaultsBootstrap=" +
      region_defaults_js +
      ";let regionDefaults=regionDefaultsBootstrap;let radioDirty=false;let timezoneDirty=false;let channelMutationInFlight=false;"
      "function ensureTimezoneOptions(selected){const tzSel=document.getElementById('timezone');if(!tzSel)return;const current=tzSel.value;const hasOptions=tzSel.options&&tzSel.options.length>0;if(hasOptions){if(selected&&selected.length>0){let found=false;for(let i=0;i<tzSel.options.length;i++){if(tzSel.options[i].value===selected){found=true;break;}}if(!found){const opt=document.createElement('option');opt.value=selected;opt.textContent=selected;tzSel.insertBefore(opt,tzSel.firstChild);}if(current!==selected){tzSel.value=selected;}}return;}let zones=[];if(typeof Intl!=='undefined'&&typeof Intl.supportedValuesOf==='function'){try{zones=Intl.supportedValuesOf('timeZone');}catch(_e){zones=[];}}if(!Array.isArray(zones)||zones.length===0){zones=['UTC0'];}if(!zones.includes('UTC0')){zones.unshift('UTC0');}if(selected&&selected.length>0&&!zones.includes(selected)){zones.unshift(selected);}tzSel.innerHTML='';zones.forEach(z=>{const opt=document.createElement('option');opt.value=z;opt.textContent=z;if(z===selected)opt.selected=true;tzSel.appendChild(opt);});if(!selected&&tzSel.options.length>0){tzSel.selectedIndex=0;}}"
      "function markRadioDirty(){radioDirty=true;}"
      "function markTimezoneDirty(){timezoneDirty=true;}"
      "function bindRadioInputs(){['region','freq','bw','sf','cr','pwr'].forEach(id=>{const el=document.getElementById(id);if(!el)return;el.addEventListener('input',markRadioDirty);el.addEventListener('change',markRadioDirty);});}"
      "function bindTimezoneInput(){const el=document.getElementById('timezone');if(!el)return;el.addEventListener('input',markTimezoneDirty);el.addEventListener('change',markTimezoneDirty);}"
      "function calcTimezoneOffsetMinutes(tz){try{const parts=new Intl.DateTimeFormat('en-US',{timeZone:tz,timeZoneName:'longOffset'}).formatToParts(new Date());const zone=(parts.find(p=>p.type==='timeZoneName')||{}).value||'';const m=zone.match(/([+-])(\\d{1,2})(?::?(\\d{2}))?/);if(m){const sign=m[1]==='-'?-1:1;const hh=parseInt(m[2],10)||0;const mm=parseInt(m[3]||'0',10)||0;return sign*(hh*60+mm);}}catch(_e){}return 0;}"
      "async function jget(u){const r=await fetch(u);return r.json();}"
      "async function jpost(u,b){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b)});return r.json();}"
      "async function loadStatus(forceRadio=false){const s=await jget('/api/status');const tz=s.timezone||'UTC0';const tzOff=(typeof s.tz_offset==='number')?s.tz_offset:0;document.getElementById('status').textContent=`mode=${s.mode} ip=${s.ip} node=${s.node_name||''} wifi=${s.wifi_ssid} tz=${tz} offset=${tzOff}m f=${s.freq} bw=${s.bw} sf=${s.sf} cr=4/${s.cr} rxRaw=${s.rx_raw||0} rxPkt=${s.rx_pkt||0}`;document.getElementById('node_name').value=s.node_name||'';document.getElementById('wifi_ssid').value=s.wifi_ssid||'';document.getElementById('wifi_pass').value=s.wifi_pass||'';const tzSel=document.getElementById('timezone');const tzNeedsInit=!(tzSel&&tzSel.options&&tzSel.options.length>0);const tzFocused=(tzSel&&document.activeElement===tzSel);if(tzNeedsInit||forceRadio||(!timezoneDirty&&!tzFocused)){ensureTimezoneOptions(tz);}if(forceRadio||!radioDirty){document.getElementById('region').value=s.region||'US';document.getElementById('freq').value=s.freq;document.getElementById('bw').value=s.bw;document.getElementById('sf').value=s.sf;document.getElementById('cr').value=s.cr;document.getElementById('pwr').value=s.pwr;}}"
      "async function loadPresets(){try{const p=await jget('/api/presets');regionDefaults=(p.region_defaults&&Object.keys(p.region_defaults).length>0)?p.region_defaults:regionDefaultsBootstrap;const r=document.getElementById('region');if(Array.isArray(p.regions)&&p.regions.length>0){r.innerHTML='';p.regions.forEach(v=>{const o=document.createElement('option');o.value=v;o.textContent=v;if(v===p.selected_region)o.selected=true;r.appendChild(o);});}if(p.selected_region){r.value=p.selected_region;}}catch(_e){regionDefaults=regionDefaultsBootstrap;}}"
      "function applyRegionPreset(){const r=document.getElementById('region').value;const d=regionDefaults[r]||regionDefaultsBootstrap[r];if(!d)return;document.getElementById('freq').value=d.freq;document.getElementById('bw').value=d.bw;document.getElementById('sf').value=d.sf;document.getElementById('cr').value=d.cr;document.getElementById('pwr').value=d.pwr;radioDirty=true;}"
      "async function loadChannels(){const c=await jget('/api/channels');const ul=document.getElementById('channels');ul.innerHTML='';c.channels.forEach(n=>{const li=document.createElement('li');li.textContent=n;if(n==='Public'){const ro=document.createElement('small');ro.textContent=' (read-only)';li.appendChild(ro);}else{const b=document.createElement('button');b.textContent='Remove';b.style.width='auto';b.style.display='inline-block';b.style.marginLeft='8px';b.onclick=async()=>{await jpost('/api/channels/remove',{name:n});await loadChannels();};li.appendChild(b);}ul.appendChild(li);});}"
      "async function saveAll(){if(channelMutationInFlight){alert('Channel update in progress, please wait');return;}const pendingName=(document.getElementById('ch_name').value||'').trim();const pendingPsk=(document.getElementById('ch_psk').value||'').trim();if(pendingName){if(pendingName[0]!=='#'&&!pendingPsk){alert('PSK is required for non-# channels');return;}let addRes=null;channelMutationInFlight=true;try{addRes=await jpost('/api/channels/add',{name:pendingName,psk:pendingPsk});}finally{channelMutationInFlight=false;}if(!addRes||!addRes.ok){alert((addRes&&addRes.error)||'failed to add channel');return;}document.getElementById('ch_name').value='';document.getElementById('ch_psk').value='';await loadChannels();}const tzVal=document.getElementById('timezone').value;const r=await jpost('/api/save',{node_name:document.getElementById('node_name').value,ssid:document.getElementById('wifi_ssid').value,pass:document.getElementById('wifi_pass').value,timezone:tzVal,tz_offset:String(calcTimezoneOffsetMinutes(tzVal)),region:document.getElementById('region').value,freq:document.getElementById('freq').value,bw:document.getElementById('bw').value,sf:document.getElementById('sf').value,cr:document.getElementById('cr').value,pwr:document.getElementById('pwr').value});alert(r.message||r.error||'done');if(r.ok){timezoneDirty=false;document.getElementById('status').textContent=r.needs_reboot?'Settings saved. Rebooting firmware...':'Settings saved.';}}"
      "async function addChannel(){if(channelMutationInFlight){return;}const name=(document.getElementById('ch_name').value||'').trim();const psk=(document.getElementById('ch_psk').value||'').trim();if(!name){alert('Channel name is required');return;}if(name[0]!=='#'&&!psk){alert('PSK is required for non-# channels');return;}let r=null;channelMutationInFlight=true;try{r=await jpost('/api/channels/add',{name,psk});}finally{channelMutationInFlight=false;}if(!r||!r.ok){alert((r&&r.error)||'failed');return;}document.getElementById('ch_name').value='';document.getElementById('ch_psk').value='';await loadChannels();}"
      "async function boot(){bindRadioInputs();bindTimezoneInput();ensureTimezoneOptions('UTC0');await loadPresets();await loadStatus(true);await loadChannels();setInterval(()=>loadStatus(false),4000);}boot();"
      "</script></div></body></html>";

  g_server.send(200, "text/html", html);
}

void handleStatus() {
  plumeria::mesh::MeshRadioStats radio_stats{};
  if (g_mesh) {
    g_mesh->getRadioStats(&radio_stats);
  }

  char node_name[32] = {};
  if (g_mesh) {
    g_mesh->getNodeName(node_name, sizeof(node_name));
  }
  if (node_name[0] == '\0') {
    copyString(node_name, sizeof(node_name), g_settings.node_name);
  }

  String payload = "{\"ok\":true,\"mode\":";
  payload += jsonString(g_mode);
  payload += ",\"ip\":";
  payload += jsonString(g_ip);
  payload += ",\"node_name\":";
  payload += jsonString(node_name);
  payload += ",\"wifi_ssid\":";
  payload += jsonString(g_settings.wifi_ssid);
  payload += ",\"wifi_pass\":";
  payload += jsonString(g_settings.wifi_pass);
  payload += ",\"timezone\":";
  payload += jsonString(g_settings.timezone);
  payload += ",\"tz_offset\":";
  payload += String(static_cast<int>(g_settings.timezone_offset_minutes));
  payload += ",\"region\":";
  payload += jsonString(g_settings.region);
  payload += ",\"freq\":";
  payload += String(g_settings.lora_freq_mhz, 3);
  payload += ",\"bw\":";
  payload += String(g_settings.lora_bw_khz, 1);
  payload += ",\"sf\":";
  payload += String(g_settings.lora_sf);
  payload += ",\"cr\":";
  payload += String(g_settings.lora_cr);
  payload += ",\"pwr\":";
  payload += String(g_settings.lora_tx_power_dbm);
  payload += ",\"rx_raw\":";
  payload += String(radio_stats.rx_raw_count);
  payload += ",\"rx_pkt\":";
  payload += String(radio_stats.rx_packet_count);
  payload += ",\"last_rx_ms\":";
  payload += String(radio_stats.last_rx_ms);
  payload += "}";
  sendJsonOk(payload);
}

void handlePresets() {
  String payload = "{\"ok\":true,\"selected_region\":";
  payload += jsonString(g_settings.region);
  payload += ",\"regions\":[";

  for (size_t i = 0; i < (sizeof(kRegionPresets) / sizeof(kRegionPresets[0])); i++) {
    if (i != 0) {
      payload += ",";
    }
    payload += jsonString(kRegionPresets[i].id);
  }

  payload += "],\"region_defaults\":{";
  for (size_t i = 0; i < (sizeof(kRegionPresets) / sizeof(kRegionPresets[0])); i++) {
    if (i != 0) {
      payload += ",";
    }
    payload += jsonString(kRegionPresets[i].id);
    payload += ":{\"freq\":";
    payload += String(kRegionPresets[i].frequency_mhz, 3);
    payload += ",\"bw\":";
    payload += String(kRegionPresets[i].bandwidth_khz, 1);
    payload += ",\"sf\":";
    payload += String(kRegionPresets[i].spreading_factor);
    payload += ",\"cr\":";
    payload += String(kRegionPresets[i].coding_rate);
    payload += ",\"pwr\":";
    payload += String(kRegionPresets[i].tx_power_dbm);
    payload += "}";
  }
  payload += "]}";
  sendJsonOk(payload);
}

void handleChannels() {
  String payload = "{\"ok\":true,\"channels\":[";
  if (g_mesh) {
    memset(g_channels_web_buf, 0, sizeof(g_channels_web_buf));
    int count = g_mesh->exportChannels(g_channels_web_buf, 40);
    for (int i = 0; i < count; i++) {
      if (i != 0) {
        payload += ",";
      }
      payload += jsonString(g_channels_web_buf[i]);
    }
  }
  payload += "]}";
  sendJsonOk(payload);
}

void handleSaveAll() {
  String node_name = g_server.arg("node_name");
  String ssid = g_server.arg("ssid");
  String pass = g_server.arg("pass");
  String timezone = g_server.arg("timezone");
  String tz_offset = g_server.arg("tz_offset");
  String region = g_server.arg("region");
  String freq = g_server.arg("freq");
  String bw = g_server.arg("bw");
  String sf = g_server.arg("sf");
  String cr = g_server.arg("cr");
  String pwr = g_server.arg("pwr");

  node_name.trim();
  ssid.trim();
    if (node_name.length() == 0) {
      node_name = kDefaultNodeName;
    }
    if (node_name.length() > 31) {
      sendJsonError("Node name too long");
      return;
    }

  pass.trim();
  timezone.trim();
  tz_offset.trim();
  region.trim();
  freq.trim();
  bw.trim();
  sf.trim();
  cr.trim();
  pwr.trim();

  if (ssid.length() == 0) {
    sendJsonError("SSID is required");
    return;
  }

  if (timezone.length() == 0) {
    timezone = kDefaultTimezone;
  }

  int tz_offset_min = static_cast<int>(g_settings.timezone_offset_minutes);
  if (tz_offset.length() > 0) {
    tz_offset_min = tz_offset.toInt();
  }
  if (tz_offset_min < -840 || tz_offset_min > 840) {
    sendJsonError("Timezone offset out of range");
    return;
  }

  if (!findRegion(region.c_str())) {
    sendJsonError("Unknown region preset");
    return;
  }

  float freq_mhz = freq.toFloat();
  float bw_khz = bw.toFloat();
  int sf_value = sf.toInt();
  int cr_value = cr.toInt();
  int pwr_value = pwr.toInt();

  if (freq_mhz < 100.0f || freq_mhz > 2500.0f) {
    sendJsonError("Frequency out of range");
    return;
  }
  if (bw_khz < 7.0f || bw_khz > 500.0f) {
    sendJsonError("Bandwidth out of range");
    return;
  }
  if (sf_value < 5 || sf_value > 12) {
    sendJsonError("SF out of range");
    return;
  }
  if (cr_value < 5 || cr_value > 8) {
    sendJsonError("CR out of range");
    return;
  }
  if (pwr_value < 1 || pwr_value > 30) {
    sendJsonError("TX power out of range");
    return;
  }

  const bool wifi_changed =
      (strcmp(g_settings.wifi_ssid, ssid.c_str()) != 0) || (strcmp(g_settings.wifi_pass, pass.c_str()) != 0);
  const bool radio_changed =
      (strcmp(g_settings.region, region.c_str()) != 0) ||
      (fabsf(g_settings.lora_freq_mhz - freq_mhz) > 0.0005f) ||
      (fabsf(g_settings.lora_bw_khz - bw_khz) > 0.05f) ||
      (g_settings.lora_sf != static_cast<uint8_t>(sf_value)) ||
      (g_settings.lora_cr != static_cast<uint8_t>(cr_value)) ||
      (g_settings.lora_tx_power_dbm != static_cast<int8_t>(pwr_value));
  const bool reboot_required = wifi_changed || radio_changed;

  copyString(g_settings.region, sizeof(g_settings.region), region.c_str());
  g_settings.lora_freq_mhz = freq_mhz;
  g_settings.lora_bw_khz = bw_khz;
  g_settings.lora_sf = static_cast<uint8_t>(sf_value);
  g_settings.lora_cr = static_cast<uint8_t>(cr_value);
  g_settings.lora_tx_power_dbm = static_cast<int8_t>(pwr_value);
  copyString(g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), ssid.c_str());
  copyString(g_settings.wifi_pass, sizeof(g_settings.wifi_pass), pass.c_str());
  copyString(g_settings.node_name, sizeof(g_settings.node_name), node_name.c_str());
  copyString(g_settings.timezone, sizeof(g_settings.timezone), timezone.c_str());
  g_settings.timezone_offset_minutes = static_cast<int16_t>(tz_offset_min);

  if (g_mesh) {
    g_mesh->setNodeName(g_settings.node_name);
  }

  applyTimezoneOffsetFromSettings();
  saveSettings(g_settings);

  if (reboot_required) {
    sendJsonOk("{\"ok\":true,\"needs_reboot\":true,\"message\":\"Settings saved. Rebooting firmware...\"}");
    g_reboot_pending = true;
    g_reboot_at_ms = millis() + kRebootDelayMs;
  } else {
    sendJsonOk("{\"ok\":true,\"needs_reboot\":false,\"message\":\"Settings saved.\"}");
  }
}

void handleChannelAdd() {
  if (!g_mesh) {
    sendJsonError("Mesh adapter unavailable", 503);
    return;
  }

  String name = g_server.arg("name");
  String psk = g_server.arg("psk");
  name.trim();
  psk.trim();

  if (name.length() == 0) {
    sendJsonError("Channel name is required");
    return;
  }

  if (name[0] != '#' && psk.length() == 0) {
    sendJsonError("PSK is required for non-# channels");
    return;
  }

  const char* psk_ptr = psk.length() > 0 ? psk.c_str() : nullptr;
  if (!g_mesh->addChannel(name.c_str(), psk_ptr)) {
    sendJsonError("Failed to add channel");
    return;
  }

  sendJsonOk("{\"ok\":true}");
}

void handleChannelRemove() {
  if (!g_mesh) {
    sendJsonError("Mesh adapter unavailable", 503);
    return;
  }

  String name = g_server.arg("name");
  name.trim();
  if (name.length() == 0) {
    sendJsonError("Channel name is required");
    return;
  }

  if (name.equals("Public")) {
    sendJsonError("Public is read-only");
    return;
  }

  if (!g_mesh->removeChannel(name.c_str())) {
    sendJsonError("Failed to remove channel");
    return;
  }

  sendJsonOk("{\"ok\":true}");
}

void registerRoutes() {
  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/api/status", HTTP_GET, handleStatus);
  g_server.on("/api/presets", HTTP_GET, handlePresets);
  g_server.on("/api/channels", HTTP_GET, handleChannels);

  g_server.on("/api/save", HTTP_POST, handleSaveAll);
  g_server.on("/api/channels/add", HTTP_POST, handleChannelAdd);
  g_server.on("/api/channels/remove", HTTP_POST, handleChannelRemove);

  g_server.onNotFound([]() {
    g_server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
  });
}

}  // namespace

namespace plumeria {
namespace web {

void loadSettings(WebSettings* out_settings) {
  if (!out_settings) {
    return;
  }

  memset(out_settings, 0, sizeof(WebSettings));

  Preferences prefs;
  prefs.begin(kPrefsNs, true);

  String node_name = prefs.getString("node_name", kDefaultNodeName);
  String ssid = prefs.getString("wifi_ssid", kDefaultSsid);
  String pass = prefs.getString("wifi_pass", kDefaultPass);
  String timezone = prefs.getString("timezone", kDefaultTimezone);
  int tz_offset = prefs.getInt("tz_offset", 0);
  String region = prefs.getString("region", kDefaultRegion);

  const bool has_explicit_radio =
      prefs.isKey("lora_freq") || prefs.isKey("lora_bw") || prefs.isKey("lora_sf") ||
      prefs.isKey("lora_cr") || prefs.isKey("lora_pwr");

  // Migration rule: old web-config versions stored region/preset only.
  // For radio-debug parity, pin legacy installs to US defaults unless explicit radio params exist.
  if (!has_explicit_radio) {
    region = kDefaultRegion;
  }

  const RegionPreset* region_defaults = findRegion(region.c_str());
  if (!region_defaults) {
    region = kDefaultRegion;
    region_defaults = findRegion(region.c_str());
  }

  float freq_mhz = region_defaults ? region_defaults->frequency_mhz : 915.000f;
  float bw_khz = region_defaults ? region_defaults->bandwidth_khz : kDefaultBwKhz;
  uint8_t sf = region_defaults ? region_defaults->spreading_factor : kDefaultSf;
  uint8_t cr = region_defaults ? region_defaults->coding_rate : kDefaultCr;
  int8_t pwr = region_defaults ? region_defaults->tx_power_dbm : 22;

  if (prefs.isKey("lora_freq")) {
    freq_mhz = prefs.getFloat("lora_freq", freq_mhz);
  }
  if (prefs.isKey("lora_bw")) {
    bw_khz = prefs.getFloat("lora_bw", bw_khz);
  }
  if (prefs.isKey("lora_sf")) {
    sf = prefs.getUChar("lora_sf", sf);
  }
  if (prefs.isKey("lora_cr")) {
    cr = prefs.getUChar("lora_cr", cr);
  }
  if (prefs.isKey("lora_pwr")) {
    pwr = prefs.getChar("lora_pwr", pwr);
  }
  prefs.end();

  copyString(out_settings->node_name, sizeof(out_settings->node_name), node_name.c_str());
  copyString(out_settings->wifi_ssid, sizeof(out_settings->wifi_ssid), ssid.c_str());
  copyString(out_settings->wifi_pass, sizeof(out_settings->wifi_pass), pass.c_str());
  copyString(out_settings->timezone, sizeof(out_settings->timezone), timezone.c_str());
  out_settings->timezone_offset_minutes = static_cast<int16_t>(tz_offset);
  copyString(out_settings->region, sizeof(out_settings->region), region.c_str());
  out_settings->lora_freq_mhz = freq_mhz;
  out_settings->lora_bw_khz = bw_khz;
  out_settings->lora_sf = sf;
  out_settings->lora_cr = cr;
  out_settings->lora_tx_power_dbm = pwr;
}

void applyRadioProfile(hal::RadioConfig* radio_config, const WebSettings& settings) {
  if (!radio_config) {
    return;
  }

  radio_config->frequency_mhz = settings.lora_freq_mhz;
  radio_config->bandwidth_khz = settings.lora_bw_khz;
  radio_config->spreading_factor = settings.lora_sf;
  radio_config->coding_rate = settings.lora_cr;
  radio_config->tx_power_dbm = settings.lora_tx_power_dbm;
}

bool begin(mesh::MeshAdapter* mesh_adapter, const WebSettings& initial_settings) {
  if (g_running) {
    return true;
  }

  g_mesh = mesh_adapter;
  g_settings = initial_settings;
  if (g_mesh && g_settings.node_name[0] != '\0') {
    g_mesh->setNodeName(g_settings.node_name);
  }
  applyTimezoneOffsetFromSettings();
  g_reboot_pending = false;
  g_reboot_at_ms = 0;

  registerRoutes();
  bringupNetwork();

  g_server.begin();
  g_running = true;

  Serial.printf("[WEB] Config server running in %s mode at http://%s/\n", g_mode, g_ip);
  return true;
}

void loop() {
  if (!g_running) {
    return;
  }
  g_server.handleClient();

  if (g_reboot_pending && static_cast<int32_t>(millis() - g_reboot_at_ms) >= 0) {
    delay(50);
    ESP.restart();
  }
}

void end() {
  if (!g_running) {
    return;
  }
  g_server.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  g_mode[0] = '\0';
  g_ip[0] = '\0';
  g_running = false;
  g_mesh = nullptr;
  g_reboot_pending = false;
  g_reboot_at_ms = 0;
}

bool running() {
  return g_running;
}

const char* mode() {
  return g_mode;
}

const char* ip() {
  return g_ip;
}

}  // namespace web
}  // namespace plumeria
