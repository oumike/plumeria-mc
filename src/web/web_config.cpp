#include "web/web_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t kStaConnectTimeoutMs = 10000;
constexpr char kPrefsNs[] = "plumeria_web";
constexpr char kDefaultSsid[] = "rhinohome";
constexpr char kDefaultPass[] = "fishfood is smelly";
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
  {"US", 915.000f, 62.5f, 8, 5, 22},
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
  prefs.putString("wifi_ssid", settings.wifi_ssid);
  prefs.putString("wifi_pass", settings.wifi_pass);
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
      "</style></head><body>"
      "<h1>Plumeria Web Config</h1>"
      "<section><div id='status' class='meta'>Loading status...</div></section>"
      "<section><h3>Wi-Fi</h3>"
      "<label>SSID<input id='wifi_ssid' value=''></label>"
      "<label>Password<input id='wifi_pass' value=''></label>"
      "<small>Device tries STA first, falls back to AP if connect fails.</small>"
      "<button onclick='saveWifi()'>Save Wi-Fi + Reconnect</button></section>"
      "<section><h3>Radio (Sigurd/MeshCore-style)</h3>"
      "<div class='row'><label>Region<select id='region'>" +
      region_options +
      "</select></label>"
      "<label>&nbsp;<button type='button' onclick='applyRegionPreset()' style='margin-top:22px'>Apply Region Defaults</button></label></div>"
      "<div class='row'><label>Frequency MHz<input id='freq' type='number' step='0.001'></label>"
      "<label>Bandwidth kHz<input id='bw' type='number' step='0.1'></label></div>"
      "<div class='row'><label>Spreading Factor<input id='sf' type='number' min='5' max='12' step='1'></label>"
      "<label>Coding Rate (4/x)<input id='cr' type='number' min='5' max='8' step='1'></label></div>"
      "<label>TX Power dBm<input id='pwr' type='number' min='1' max='30' step='1'></label>"
      "<small>This firmware uses explicit LoRa values (freq, bw, sf, cr, tx power), not modem profile names. Radio changes apply after reboot.</small>"
      "<button onclick='saveRadio()'>Save Radio (Reboot Required)</button></section>"
      "<section><h3>Channels</h3><div class='row'>"
      "<label>Name<input id='ch_name' placeholder='Public or #rhino'></label>"
      "<label>PSK Base64 (optional for #channels)<input id='ch_psk' placeholder='izOH6cXN6mrJ5e26oRXNcg=='></label>"
      "</div><button onclick='addChannel()'>Add Channel</button>"
      "<ul id='channels'></ul></section>"
      "<script>"
      "const regionDefaultsBootstrap=" +
      region_defaults_js +
      ";let regionDefaults=regionDefaultsBootstrap;"
      "async function jget(u){const r=await fetch(u);return r.json();}"
      "async function jpost(u,b){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b)});return r.json();}"
      "async function loadStatus(){const s=await jget('/api/status');document.getElementById('status').textContent=`mode=${s.mode} ip=${s.ip} wifi=${s.wifi_ssid} f=${s.freq} bw=${s.bw} sf=${s.sf} cr=4/${s.cr} rxRaw=${s.rx_raw||0} rxPkt=${s.rx_pkt||0}`;document.getElementById('wifi_ssid').value=s.wifi_ssid||'';document.getElementById('wifi_pass').value=s.wifi_pass||'';document.getElementById('region').value=s.region||'US';document.getElementById('freq').value=s.freq;document.getElementById('bw').value=s.bw;document.getElementById('sf').value=s.sf;document.getElementById('cr').value=s.cr;document.getElementById('pwr').value=s.pwr;}"
      "async function loadPresets(){try{const p=await jget('/api/presets');regionDefaults=(p.region_defaults&&Object.keys(p.region_defaults).length>0)?p.region_defaults:regionDefaultsBootstrap;const r=document.getElementById('region');if(Array.isArray(p.regions)&&p.regions.length>0){r.innerHTML='';p.regions.forEach(v=>{const o=document.createElement('option');o.value=v;o.textContent=v;if(v===p.selected_region)o.selected=true;r.appendChild(o);});}if(p.selected_region){r.value=p.selected_region;}}catch(_e){regionDefaults=regionDefaultsBootstrap;}}"
      "function applyRegionPreset(){const r=document.getElementById('region').value;const d=regionDefaults[r]||regionDefaultsBootstrap[r];if(!d)return;document.getElementById('freq').value=d.freq;document.getElementById('bw').value=d.bw;document.getElementById('sf').value=d.sf;document.getElementById('cr').value=d.cr;document.getElementById('pwr').value=d.pwr;}"
      "async function loadChannels(){const c=await jget('/api/channels');const ul=document.getElementById('channels');ul.innerHTML='';c.channels.forEach(n=>{const li=document.createElement('li');const b=document.createElement('button');b.textContent='Remove';b.style.width='auto';b.style.display='inline-block';b.style.marginLeft='8px';b.onclick=async()=>{await jpost('/api/channels/remove',{name:n});await loadChannels();};li.textContent=n;li.appendChild(b);ul.appendChild(li);});}"
      "async function saveWifi(){const r=await jpost('/api/wifi',{ssid:document.getElementById('wifi_ssid').value,pass:document.getElementById('wifi_pass').value});alert(r.message||r.error||'done');await loadStatus();}"
      "async function saveRadio(){const r=await jpost('/api/radio',{region:document.getElementById('region').value,freq:document.getElementById('freq').value,bw:document.getElementById('bw').value,sf:document.getElementById('sf').value,cr:document.getElementById('cr').value,pwr:document.getElementById('pwr').value});alert(r.message||r.error||'done');await loadStatus();}"
      "async function addChannel(){const r=await jpost('/api/channels/add',{name:document.getElementById('ch_name').value,psk:document.getElementById('ch_psk').value});if(!r.ok){alert(r.error||'failed');return;}document.getElementById('ch_name').value='';document.getElementById('ch_psk').value='';await loadChannels();}"
      "async function boot(){await loadPresets();await loadStatus();await loadChannels();setInterval(loadStatus,4000);}boot();"
      "</script></body></html>";

  g_server.send(200, "text/html", html);
}

void handleStatus() {
  plumeria::mesh::MeshRadioStats radio_stats{};
  if (g_mesh) {
    g_mesh->getRadioStats(&radio_stats);
  }

  String payload = "{\"ok\":true,\"mode\":";
  payload += jsonString(g_mode);
  payload += ",\"ip\":";
  payload += jsonString(g_ip);
  payload += ",\"wifi_ssid\":";
  payload += jsonString(g_settings.wifi_ssid);
  payload += ",\"wifi_pass\":";
  payload += jsonString(g_settings.wifi_pass);
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
    char names[8][32]{};
    int count = g_mesh->exportChannels(names, 8);
    for (int i = 0; i < count; i++) {
      if (i != 0) {
        payload += ",";
      }
      payload += jsonString(names[i]);
    }
  }
  payload += "]}";
  sendJsonOk(payload);
}

void handleWifiSave() {
  String ssid = g_server.arg("ssid");
  String pass = g_server.arg("pass");

  ssid.trim();
  if (ssid.length() == 0) {
    sendJsonError("SSID is required");
    return;
  }

  copyString(g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), ssid.c_str());
  copyString(g_settings.wifi_pass, sizeof(g_settings.wifi_pass), pass.c_str());
  saveSettings(g_settings);

  bringupNetwork();

  String payload = "{\"ok\":true,\"message\":\"Wi-Fi settings saved\",\"mode\":";
  payload += jsonString(g_mode);
  payload += ",\"ip\":";
  payload += jsonString(g_ip);
  payload += "}";
  sendJsonOk(payload);
}

void handleRadioSave() {
  String region = g_server.arg("region");
  String freq = g_server.arg("freq");
  String bw = g_server.arg("bw");
  String sf = g_server.arg("sf");
  String cr = g_server.arg("cr");
  String pwr = g_server.arg("pwr");

  region.trim();
  freq.trim();
  bw.trim();
  sf.trim();
  cr.trim();
  pwr.trim();

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

  copyString(g_settings.region, sizeof(g_settings.region), region.c_str());
  g_settings.lora_freq_mhz = freq_mhz;
  g_settings.lora_bw_khz = bw_khz;
  g_settings.lora_sf = static_cast<uint8_t>(sf_value);
  g_settings.lora_cr = static_cast<uint8_t>(cr_value);
  g_settings.lora_tx_power_dbm = static_cast<int8_t>(pwr_value);
  saveSettings(g_settings);

  sendJsonOk("{\"ok\":true,\"message\":\"Radio settings saved. Reboot required to apply.\"}");
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

  g_server.on("/api/wifi", HTTP_POST, handleWifiSave);
  g_server.on("/api/radio", HTTP_POST, handleRadioSave);
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

  String ssid = prefs.getString("wifi_ssid", kDefaultSsid);
  String pass = prefs.getString("wifi_pass", kDefaultPass);
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

  copyString(out_settings->wifi_ssid, sizeof(out_settings->wifi_ssid), ssid.c_str());
  copyString(out_settings->wifi_pass, sizeof(out_settings->wifi_pass), pass.c_str());
  copyString(out_settings->region, sizeof(out_settings->region), region.c_str());
  out_settings->lora_freq_mhz = freq_mhz;
  out_settings->lora_bw_khz = bw_khz;
  out_settings->lora_sf = sf;
  out_settings->lora_cr = cr;
  out_settings->lora_tx_power_dbm = pwr;
}

void applyRadioProfile(hal::TloraPagerRadioConfig* radio_config, const WebSettings& settings) {
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
