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
constexpr double kDefaultNodeLatitude = 0.0;
constexpr double kDefaultNodeLongitude = 0.0;
constexpr bool kDefaultSendLocationInAdvert = false;
constexpr char kDefaultTimezone[] = "UTC0";
constexpr char kDefaultRegion[] = "US";
constexpr float kDefaultBwKhz = 62.5f;
constexpr uint8_t kDefaultSf = 8;
constexpr uint8_t kDefaultCr = 5;
constexpr char kFallbackApSsid[] = "plumeria-mc";

struct RegionPreset {
  const char* id;
  const char* label;
  float frequency_mhz;
  float bandwidth_khz;
  uint8_t spreading_factor;
  uint8_t coding_rate;
  int8_t tx_power_dbm;
};

constexpr RegionPreset kRegionPresets[] = {
  {"US", "US", 910.525f, 62.5f, 7, 5, 22},
    {"EU_868", "EU_868", 869.525f, 62.5f, 8, 5, 22},
    {"EU_433", "EU_433", 433.500f, 62.5f, 8, 5, 10},
    {"ANZ", "ANZ", 921.500f, 62.5f, 8, 5, 22},
    {"JP", "JP", 922.000f, 62.5f, 8, 5, 13},
    {"KR", "KR", 921.500f, 62.5f, 8, 5, 22},
    {"IN", "IN", 866.000f, 62.5f, 8, 5, 22},
    {"TH", "TH", 922.500f, 62.5f, 8, 5, 16},
    {"BR_902", "BR_902", 904.750f, 62.5f, 8, 5, 22},
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
plumeria::mesh::MeshContactSummary g_contacts_web_buf[160]{};
plumeria::mesh::MeshChannelConfig g_imported_channels_buf[40]{};
char g_existing_channels_buf[40][32]{};

bool contactSortBefore(const plumeria::mesh::MeshContactSummary& a,
                       const plumeria::mesh::MeshContactSummary& b) {
  if (a.favorite != b.favorite) {
    return a.favorite && !b.favorite;
  }
  if (a.lastmod != b.lastmod) {
    return a.lastmod > b.lastmod;
  }
  return strcmp(a.name, b.name) < 0;
}

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
  prefs.putDouble("node_lat", settings.node_latitude);
  prefs.putDouble("node_lon", settings.node_longitude);
  prefs.putBool("send_loc_adv", settings.send_location_in_advert);
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

bool parseBoolArg(const String& raw, bool default_value) {
  if (raw.length() == 0) {
    return default_value;
  }

  if (raw == "1" || raw.equalsIgnoreCase("true") || raw.equalsIgnoreCase("on") || raw.equalsIgnoreCase("yes")) {
    return true;
  }
  if (raw == "0" || raw.equalsIgnoreCase("false") || raw.equalsIgnoreCase("off") || raw.equalsIgnoreCase("no")) {
    return false;
  }

  return default_value;
}

String configSafeValue(const char* raw) {
  if (!raw) {
    return String("");
  }
  String out(raw);
  out.replace("\n", " ");
  out.replace("\r", " ");
  return out;
}

String buildConfigText() {
  String out;
  out.reserve(4096);
  out += "plumeria_config_version: 1\n";
  out += "node_name: ";
  out += configSafeValue(g_settings.node_name);
  out += "\n";
  out += "node_lat: ";
  out += String(g_settings.node_latitude, 6);
  out += "\n";
  out += "node_lon: ";
  out += String(g_settings.node_longitude, 6);
  out += "\n";
  out += "send_loc_adv: ";
  out += g_settings.send_location_in_advert ? "true" : "false";
  out += "\n";
  out += "wifi_ssid: ";
  out += configSafeValue(g_settings.wifi_ssid);
  out += "\n";
  out += "wifi_pass: ";
  out += configSafeValue(g_settings.wifi_pass);
  out += "\n";
  out += "timezone: ";
  out += configSafeValue(g_settings.timezone);
  out += "\n";
  out += "tz_offset: ";
  out += String(static_cast<int>(g_settings.timezone_offset_minutes));
  out += "\n";
  out += "region: ";
  out += configSafeValue(g_settings.region);
  out += "\n";
  out += "freq: ";
  out += String(g_settings.lora_freq_mhz, 3);
  out += "\n";
  out += "bw: ";
  out += String(g_settings.lora_bw_khz, 1);
  out += "\n";
  out += "sf: ";
  out += String(g_settings.lora_sf);
  out += "\n";
  out += "cr: ";
  out += String(g_settings.lora_cr);
  out += "\n";
  out += "pwr: ";
  out += String(g_settings.lora_tx_power_dbm);
  out += "\n";

  out += "channels:\n";
  if (g_mesh) {
    plumeria::mesh::MeshChannelConfig channels[40]{};
    const int count = g_mesh->exportChannelConfigs(channels, 40);
    for (int i = 0; i < count; i++) {
      out += "channel: ";
      out += configSafeValue(channels[i].name);
      out += "|";
      out += configSafeValue(channels[i].psk_base64);
      out += "\n";
    }
  }
  return out;
}

void setImportError(char* err, size_t err_size, const char* message) {
  if (!err || err_size == 0) {
    return;
  }
  if (!message) {
    err[0] = '\0';
    return;
  }
  strncpy(err, message, err_size - 1);
  err[err_size - 1] = '\0';
}

bool applyConfigTextInternal(const char* text, bool queue_reboot, char* err, size_t err_size) {
  if (!text || text[0] == '\0') {
    setImportError(err, err_size, "Config content is empty");
    return false;
  }

  plumeria::web::WebSettings imported = g_settings;
  bool saw_channels = false;
  int imported_channel_count = 0;
  memset(g_imported_channels_buf, 0, sizeof(g_imported_channels_buf));

  String all(text);
  int start = 0;
  while (start <= all.length()) {
    int nl = all.indexOf('\n', start);
    String line;
    if (nl < 0) {
      line = all.substring(start);
      start = all.length() + 1;
    } else {
      line = all.substring(start, nl);
      start = nl + 1;
    }

    line.trim();
    if (line.length() == 0 || line.startsWith("#")) {
      continue;
    }

    int colon = line.indexOf(':');
    if (colon <= 0) {
      continue;
    }

    String key = line.substring(0, colon);
    String value = line.substring(colon + 1);
    key.trim();
    value.trim();

    if (key.equals("node_name")) {
      if (value.length() == 0 || value.length() > 31) {
        setImportError(err, err_size, "Invalid node_name");
        return false;
      }
      copyString(imported.node_name, sizeof(imported.node_name), value.c_str());
    } else if (key.equals("node_lat")) {
      const double lat = value.toDouble();
      if (lat < -90.0 || lat > 90.0) {
        setImportError(err, err_size, "Latitude out of range");
        return false;
      }
      imported.node_latitude = lat;
    } else if (key.equals("node_lon")) {
      const double lon = value.toDouble();
      if (lon < -180.0 || lon > 180.0) {
        setImportError(err, err_size, "Longitude out of range");
        return false;
      }
      imported.node_longitude = lon;
    } else if (key.equals("send_loc_adv")) {
      imported.send_location_in_advert = parseBoolArg(value, imported.send_location_in_advert);
    } else if (key.equals("wifi_ssid")) {
      if (value.length() == 0) {
        setImportError(err, err_size, "wifi_ssid cannot be empty");
        return false;
      }
      copyString(imported.wifi_ssid, sizeof(imported.wifi_ssid), value.c_str());
    } else if (key.equals("wifi_pass")) {
      copyString(imported.wifi_pass, sizeof(imported.wifi_pass), value.c_str());
    } else if (key.equals("timezone")) {
      if (value.length() == 0) {
        value = kDefaultTimezone;
      }
      copyString(imported.timezone, sizeof(imported.timezone), value.c_str());
    } else if (key.equals("tz_offset")) {
      const int tz_offset = value.toInt();
      if (tz_offset < -840 || tz_offset > 840) {
        setImportError(err, err_size, "tz_offset out of range");
        return false;
      }
      imported.timezone_offset_minutes = static_cast<int16_t>(tz_offset);
    } else if (key.equals("region")) {
      if (!findRegion(value.c_str())) {
        setImportError(err, err_size, "Unknown region");
        return false;
      }
      copyString(imported.region, sizeof(imported.region), value.c_str());
    } else if (key.equals("freq")) {
      const float freq = value.toFloat();
      if (freq < 100.0f || freq > 2500.0f) {
        setImportError(err, err_size, "Frequency out of range");
        return false;
      }
      imported.lora_freq_mhz = freq;
    } else if (key.equals("bw")) {
      const float bw = value.toFloat();
      if (bw < 7.0f || bw > 500.0f) {
        setImportError(err, err_size, "Bandwidth out of range");
        return false;
      }
      imported.lora_bw_khz = bw;
    } else if (key.equals("sf")) {
      const int sf = value.toInt();
      if (sf < 5 || sf > 12) {
        setImportError(err, err_size, "SF out of range");
        return false;
      }
      imported.lora_sf = static_cast<uint8_t>(sf);
    } else if (key.equals("cr")) {
      const int cr = value.toInt();
      if (cr < 5 || cr > 8) {
        setImportError(err, err_size, "CR out of range");
        return false;
      }
      imported.lora_cr = static_cast<uint8_t>(cr);
    } else if (key.equals("pwr")) {
      const int pwr = value.toInt();
      if (pwr < 1 || pwr > 30) {
        setImportError(err, err_size, "TX power out of range");
        return false;
      }
      imported.lora_tx_power_dbm = static_cast<int8_t>(pwr);
    } else if (key.equals("channel")) {
      saw_channels = true;
      if (imported_channel_count >= 40) {
        continue;
      }
      const int sep = value.indexOf('|');
      String name = sep >= 0 ? value.substring(0, sep) : value;
      String psk = sep >= 0 ? value.substring(sep + 1) : String("");
      name.trim();
      psk.trim();
      if (name.length() == 0 || name.length() > 31) {
        continue;
      }
            strncpy(g_imported_channels_buf[imported_channel_count].name, name.c_str(),
              sizeof(g_imported_channels_buf[imported_channel_count].name) - 1);
            g_imported_channels_buf[imported_channel_count].name[sizeof(g_imported_channels_buf[imported_channel_count].name) - 1] =
          '\0';
            strncpy(g_imported_channels_buf[imported_channel_count].psk_base64, psk.c_str(),
              sizeof(g_imported_channels_buf[imported_channel_count].psk_base64) - 1);
            g_imported_channels_buf[imported_channel_count]
          .psk_base64[sizeof(g_imported_channels_buf[imported_channel_count].psk_base64) - 1] = '\0';
      imported_channel_count++;
    }
  }

  const bool wifi_changed = (strcmp(g_settings.wifi_ssid, imported.wifi_ssid) != 0) ||
                            (strcmp(g_settings.wifi_pass, imported.wifi_pass) != 0);
  const bool radio_changed =
      (strcmp(g_settings.region, imported.region) != 0) ||
      (fabsf(g_settings.lora_freq_mhz - imported.lora_freq_mhz) > 0.0005f) ||
      (fabsf(g_settings.lora_bw_khz - imported.lora_bw_khz) > 0.05f) ||
      (g_settings.lora_sf != imported.lora_sf) || (g_settings.lora_cr != imported.lora_cr) ||
      (g_settings.lora_tx_power_dbm != imported.lora_tx_power_dbm);

  g_settings = imported;

  if (g_mesh) {
    g_mesh->setNodeName(g_settings.node_name);
    if (!g_mesh->setAdvertLocation(g_settings.send_location_in_advert, g_settings.node_latitude,
                                   g_settings.node_longitude)) {
      setImportError(err, err_size, "Failed to apply advert location");
      return false;
    }

    if (saw_channels) {
      memset(g_existing_channels_buf, 0, sizeof(g_existing_channels_buf));
      const int existing_count = g_mesh->exportChannels(g_existing_channels_buf, 40);
      for (int i = 0; i < existing_count; i++) {
        if (strcmp(g_existing_channels_buf[i], "Public") != 0) {
          g_mesh->removeChannel(g_existing_channels_buf[i]);
        }
      }

      for (int i = 0; i < imported_channel_count; i++) {
        const bool hashtag = g_imported_channels_buf[i].name[0] == '#';
        const char* psk_ptr = g_imported_channels_buf[i].psk_base64[0] != '\0' ? g_imported_channels_buf[i].psk_base64 : nullptr;
        if (!hashtag && !psk_ptr) {
          setImportError(err, err_size, "Non-# channel missing PSK");
          return false;
        }
        if (!g_mesh->addChannel(g_imported_channels_buf[i].name, psk_ptr)) {
          setImportError(err, err_size, "Failed to apply channels");
          return false;
        }
      }
    }

    g_mesh->broadcastSelfAdvertNow();
  }

  applyTimezoneOffsetFromSettings();
  saveSettings(g_settings);

  if (queue_reboot || wifi_changed || radio_changed) {
    g_reboot_pending = true;
    g_reboot_at_ms = millis() + kRebootDelayMs;
  }

  setImportError(err, err_size, "");
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
      "input,select,button{width:100%;box-sizing:border-box;padding:8px;border-radius:6px;border:1px solid #355674;background:#0f1a28;color:#e7eef6;font:inherit;}"
      "input[type='text'],input[type='number'],input[type='password'],input:not([type]),select{min-height:38px;line-height:1.25rem;}"
      "select{-webkit-appearance:none;appearance:none;background-image:linear-gradient(45deg,transparent 50%,#9bb1c5 50%),linear-gradient(135deg,#9bb1c5 50%,transparent 50%);background-position:calc(100% - 16px) calc(50% - 2px),calc(100% - 11px) calc(50% - 2px);background-size:5px 5px,5px 5px;background-repeat:no-repeat;padding-right:30px;}"
      "button{margin-top:8px;background:#2c9bc8;border-color:#2c9bc8;font-weight:700;}"
      "small{color:#9bb1c5;}"
      ".row{display:grid;grid-template-columns:1fr 1fr;gap:8px;}"
      ".copy-row{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:end;}"
      ".copy-row button{width:auto;min-width:88px;margin-top:0;}"
      "@media(max-width:520px){.row{grid-template-columns:1fr;}}"
      "ul{margin:8px 0 0;padding-left:18px;}"
      "li{margin:4px 0;}"
      ".meta{font-size:.82rem;color:#9bb1c5;}"
      ".tabs{display:flex;gap:8px;margin:0 0 10px;}"
      ".tabbtn{width:auto;min-width:130px;margin-top:0;background:#1a2b3a;border-color:#355674;}"
      ".tabbtn.active{background:#2c9bc8;border-color:#2c9bc8;}"
      ".tab-panel{display:none;}"
      ".tab-panel.active{display:block;}"
      "</style></head><body><div class='wrap'>"
      "<h1>Plumeria Web Config</h1>"
      "<div class='tabs'><button type='button' id='tab_btn_config' class='tabbtn active' onclick='showTab(\"config\")'>Configuration</button>"
      "<button type='button' id='tab_btn_utils' class='tabbtn' onclick='showTab(\"utils\")'>Utilities</button></div>"
      "<div id='tab_config' class='tab-panel active'>"
      "<section><h3>Identity</h3>"
      "<label>Node Name<input id='node_name' maxlength='31' value=''></label>"
      "<label>Public Key</label><div class='copy-row'><input id='public_key' readonly value=''><button type='button' id='copy_pubkey' onclick='copyPublicKey()'>Copy</button></div>"
      "<small>Used for node adverts and sender name in channel chats.</small>"
      "</section>"
      "<section><h3>Radio Configuration</h3>"
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
      "<label>Name<input id='ch_name' placeholder='Public, #SomeChannel'></label>"
      "<label>PSK Base64 (optional for #channels)<input id='ch_psk' placeholder='izOH6cXN6mrJ5e26oRXNcg=='></label>"
      "</div><button onclick='addChannel()'>Add Channel</button>"
      "<small>#channels derive encryption key from SHA-256(channel name); PSK is ignored for #channels.</small>"
      "<ul id='channels'></ul></section>"
      "<section><h3>Contacts</h3>"
      "<small>Heard contacts on mesh. Favorites are protected from oldest-contact overwrite.</small>"
      "<ul id='contacts'></ul></section>"
      "<section><h3>Timezone</h3>"
      "<label>Timezone<select id='timezone'></select></label>"
      "<small>Timezone list is populated from browser-supported IANA zones.</small>"
      "</section>"
      "<section><h3>Location</h3>"
      "<label><input id='gps_mode' type='checkbox' style='width:auto;margin-right:8px;'><span id='gps_mode_label'>GPS OFF, using default lat/long.</span></label>"
      "<small>GPS ON/OFF does not control whether location is included in adverts.</small>"
      "<small>Default Location:</small>"
      "<div class='row'><label>Latitude<input id='node_lat' type='number' step='0.000001' min='-90' max='90'></label>"
      "<label>Longitude<input id='node_lon' type='number' step='0.000001' min='-180' max='180'></label></div>"
      "<label><input id='send_loc_adv' type='checkbox' style='width:auto;margin-right:8px;'>Send location in adverts (independent of GPS ON/OFF)</label>"
      "<small>When enabled, adverts include this preset latitude/longitude.</small>"
      "</section>"
      "<section><h3>Wi-Fi</h3>"
      "<label>SSID<input id='wifi_ssid' value=''></label>"
      "<label>Password<input id='wifi_pass' value=''></label>"
      "<small>Wi-Fi settings are applied after saving and rebooting.</small>"
      "</section>"
      "<section><button onclick='saveAll()'>Save Settings</button>"
      "<small>Reboots only when Wi-Fi or radio settings change.</small></section>"
      "</div>"
      "<div id='tab_utils' class='tab-panel'>"
      "<section><h3>Advert Utilities</h3>"
      "<button type='button' onclick='utilAdvertLocal()'>Advert Local (Zero Hop)</button>"
      "<button type='button' onclick='utilAdvertFlood()'>Advert Flood</button>"
      "</section>"
      "<section><h3>Config Utilities</h3>"
      "<button type='button' onclick='utilExportConfig()'>Export Config</button>"
      "<label>Import Config File<input id='util_cfg_file' type='file' accept='.yaml,.yml,.txt' style='padding:6px;background:#0f1a28;'></label>"
      "<button type='button' onclick='utilImportConfig()'>Import Config</button>"
      "<small id='util_status'>Import applies settings and reboots firmware.</small>"
      "</section>"
      "</div>"
      "<script>"
      "const regionDefaultsBootstrap=" +
      region_defaults_js +
      ";let regionDefaults=regionDefaultsBootstrap;let radioDirty=false;let timezoneDirty=false;let nodeNameDirty=false;let locationDirty=false;let channelMutationInFlight=false;"
      "function showTab(tab){const cfg=document.getElementById('tab_config');const util=document.getElementById('tab_utils');const bCfg=document.getElementById('tab_btn_config');const bUtil=document.getElementById('tab_btn_utils');if(!cfg||!util||!bCfg||!bUtil)return;const configOn=tab==='config';cfg.classList.toggle('active',configOn);util.classList.toggle('active',!configOn);bCfg.classList.toggle('active',configOn);bUtil.classList.toggle('active',!configOn);}"
      "function ensureTimezoneOptions(selected){const tzSel=document.getElementById('timezone');if(!tzSel)return;const current=tzSel.value;const hasOptions=tzSel.options&&tzSel.options.length>0;if(hasOptions){if(selected&&selected.length>0){let found=false;for(let i=0;i<tzSel.options.length;i++){if(tzSel.options[i].value===selected){found=true;break;}}if(!found){const opt=document.createElement('option');opt.value=selected;opt.textContent=selected;tzSel.insertBefore(opt,tzSel.firstChild);}if(current!==selected){tzSel.value=selected;}}return;}let zones=[];if(typeof Intl!=='undefined'&&typeof Intl.supportedValuesOf==='function'){try{zones=Intl.supportedValuesOf('timeZone');}catch(_e){zones=[];}}if(!Array.isArray(zones)||zones.length===0){zones=['UTC0'];}if(!zones.includes('UTC0')){zones.unshift('UTC0');}if(selected&&selected.length>0&&!zones.includes(selected)){zones.unshift(selected);}tzSel.innerHTML='';zones.forEach(z=>{const opt=document.createElement('option');opt.value=z;opt.textContent=z;if(z===selected)opt.selected=true;tzSel.appendChild(opt);});if(!selected&&tzSel.options.length>0){tzSel.selectedIndex=0;}}"
      "function markRadioDirty(){radioDirty=true;}"
      "function markTimezoneDirty(){timezoneDirty=true;}"
      "function refreshGpsModeIndicator(){const gpsMode=document.getElementById('gps_mode');const gpsLabel=document.getElementById('gps_mode_label');if(!gpsMode)return;if(gpsLabel)gpsLabel.textContent=gpsMode.checked?'GPS ON':'GPS OFF, using default location.';}"
      "function bindNodeNameInput(){const el=document.getElementById('node_name');if(!el)return;el.addEventListener('input',()=>{nodeNameDirty=true;});el.addEventListener('change',()=>{nodeNameDirty=true;});}"
      "function bindLocationInputs(){['node_lat','node_lon'].forEach(id=>{const el=document.getElementById(id);if(!el)return;el.addEventListener('input',()=>{locationDirty=true;});el.addEventListener('change',()=>{locationDirty=true;});});const cb=document.getElementById('send_loc_adv');if(cb){cb.addEventListener('change',()=>{locationDirty=true;});}const gpsMode=document.getElementById('gps_mode');if(gpsMode){gpsMode.addEventListener('change',()=>{locationDirty=true;refreshGpsModeIndicator();});}refreshGpsModeIndicator();}"
      "function bindRadioInputs(){['region','freq','bw','sf','cr','pwr'].forEach(id=>{const el=document.getElementById(id);if(!el)return;el.addEventListener('input',markRadioDirty);el.addEventListener('change',markRadioDirty);});}"
      "function bindTimezoneInput(){const el=document.getElementById('timezone');if(!el)return;el.addEventListener('input',markTimezoneDirty);el.addEventListener('change',markTimezoneDirty);}"
      "async function copyPublicKey(){const el=document.getElementById('public_key');if(!el||!el.value){alert('No public key');return;}try{if(navigator.clipboard&&navigator.clipboard.writeText){await navigator.clipboard.writeText(el.value);}else{el.focus();el.select();document.execCommand('copy');}alert('Public key copied');}catch(_e){el.focus();el.select();document.execCommand('copy');alert('Public key copied');}}"
      "function calcTimezoneOffsetMinutes(tz){try{const parts=new Intl.DateTimeFormat('en-US',{timeZone:tz,timeZoneName:'longOffset'}).formatToParts(new Date());const zone=(parts.find(p=>p.type==='timeZoneName')||{}).value||'';const m=zone.match(/([+-])(\\d{1,2})(?::?(\\d{2}))?/);if(m){const sign=m[1]==='-'?-1:1;const hh=parseInt(m[2],10)||0;const mm=parseInt(m[3]||'0',10)||0;return sign*(hh*60+mm);}}catch(_e){}return 0;}"
      "async function jget(u){const r=await fetch(u);return r.json();}"
      "async function jpost(u,b){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b)});return r.json();}"
      "async function loadStatus(forceRadio=false){const s=await jget('/api/status');const tz=s.timezone||'UTC0';const nodeEl=document.getElementById('node_name');const nodeFocused=(nodeEl&&document.activeElement===nodeEl);if(nodeEl&&(forceRadio||(!nodeNameDirty&&!nodeFocused))){nodeEl.value=s.node_name||'';}const pkEl=document.getElementById('public_key');if(pkEl&&s.public_key){pkEl.value=s.public_key;}if(forceRadio||!locationDirty){document.getElementById('node_lat').value=(typeof s.node_lat==='number')?s.node_lat:0;document.getElementById('node_lon').value=(typeof s.node_lon==='number')?s.node_lon:0;document.getElementById('send_loc_adv').checked=!!s.send_loc_adv;refreshGpsModeIndicator();}document.getElementById('wifi_ssid').value=s.wifi_ssid||'';document.getElementById('wifi_pass').value=s.wifi_pass||'';const tzSel=document.getElementById('timezone');const tzNeedsInit=!(tzSel&&tzSel.options&&tzSel.options.length>0);const tzFocused=(tzSel&&document.activeElement===tzSel);if(tzNeedsInit||forceRadio||(!timezoneDirty&&!tzFocused)){ensureTimezoneOptions(tz);}if(forceRadio||!radioDirty){document.getElementById('region').value=s.region||'US';document.getElementById('freq').value=s.freq;document.getElementById('bw').value=s.bw;document.getElementById('sf').value=s.sf;document.getElementById('cr').value=s.cr;document.getElementById('pwr').value=s.pwr;}}"
      "async function loadPresets(){try{const p=await jget('/api/presets');regionDefaults=(p.region_defaults&&Object.keys(p.region_defaults).length>0)?p.region_defaults:regionDefaultsBootstrap;const r=document.getElementById('region');if(Array.isArray(p.regions)&&p.regions.length>0){r.innerHTML='';p.regions.forEach(v=>{const o=document.createElement('option');o.value=v;o.textContent=v;if(v===p.selected_region)o.selected=true;r.appendChild(o);});}if(p.selected_region){r.value=p.selected_region;}}catch(_e){regionDefaults=regionDefaultsBootstrap;}}"
      "function applyRegionPreset(){const r=document.getElementById('region').value;const d=regionDefaults[r]||regionDefaultsBootstrap[r];if(!d)return;document.getElementById('freq').value=d.freq;document.getElementById('bw').value=d.bw;document.getElementById('sf').value=d.sf;document.getElementById('cr').value=d.cr;document.getElementById('pwr').value=d.pwr;radioDirty=true;}"
      "async function loadChannels(){const c=await jget('/api/channels');const ul=document.getElementById('channels');ul.innerHTML='';c.channels.forEach(n=>{const li=document.createElement('li');li.textContent=n;if(n==='Public'){const ro=document.createElement('small');ro.textContent=' (read-only)';li.appendChild(ro);}else{const b=document.createElement('button');b.textContent='Remove';b.style.width='auto';b.style.display='inline-block';b.style.marginLeft='8px';b.onclick=async()=>{await jpost('/api/channels/remove',{name:n});await loadChannels();};li.appendChild(b);}ul.appendChild(li);});}"
      "async function loadContacts(){const c=await jget('/api/contacts');const ul=document.getElementById('contacts');ul.innerHTML='';if(!c.contacts||c.contacts.length===0){const li=document.createElement('li');li.textContent='No contacts heard yet';ul.appendChild(li);return;}c.contacts.forEach(ct=>{const li=document.createElement('li');const name=ct.name&&ct.name.length>0?ct.name:'(unnamed)';const shortPk=(ct.pubkey&&ct.pubkey.length>=12)?ct.pubkey.substring(0,12)+'...':'';li.textContent=`${name} ${shortPk?'('+shortPk+')':''}`;const fav=document.createElement('button');fav.textContent=ct.favorite?'Unfavorite':'Favorite';fav.style.width='auto';fav.style.display='inline-block';fav.style.marginLeft='8px';fav.onclick=async()=>{const r=await jpost('/api/contacts/favorite',{pubkey:ct.pubkey,favorite:ct.favorite?'0':'1'});if(!r||!r.ok){alert((r&&r.error)||'failed');return;}await loadContacts();};const del=document.createElement('button');del.textContent='Delete';del.style.width='auto';del.style.display='inline-block';del.style.marginLeft='8px';del.onclick=async()=>{if(!confirm('Delete contact '+name+'?')){return;}const r=await jpost('/api/contacts/remove',{pubkey:ct.pubkey});if(!r||!r.ok){alert((r&&r.error)||'failed');return;}await loadContacts();};li.appendChild(fav);li.appendChild(del);ul.appendChild(li);});}"
      "async function utilAdvertLocal(){const r=await jpost('/api/util/advert/local',{});alert((r&&r.message)||r.error||'done');}"
      "async function utilAdvertFlood(){const r=await jpost('/api/util/advert/flood',{});alert((r&&r.message)||r.error||'done');}"
      "function utilExportConfig(){window.location='/api/util/export';}"
      "async function utilImportConfig(){const status=document.getElementById('util_status');const input=document.getElementById('util_cfg_file');if(!input||!input.files||input.files.length===0){if(status)status.textContent='Choose a config file first.';return;}const file=input.files[0];const text=await file.text();const r=await jpost('/api/util/import',{content:text});if(status)status.textContent=(r&&r.message)?r.message:(r&&r.error)?r.error:'done';if(!r||!r.ok){alert((r&&r.error)||'failed');return;}alert(r.message||'Config imported');}"
      "async function saveAll(){if(channelMutationInFlight){alert('Channel update in progress, please wait');return;}const pendingName=(document.getElementById('ch_name').value||'').trim();const pendingPsk=(document.getElementById('ch_psk').value||'').trim();if(pendingName){if(pendingName[0]!=='#'&&!pendingPsk){alert('PSK is required for non-# channels');return;}let addRes=null;channelMutationInFlight=true;try{addRes=await jpost('/api/channels/add',{name:pendingName,psk:pendingPsk});}finally{channelMutationInFlight=false;}if(!addRes||!addRes.ok){alert((addRes&&addRes.error)||'failed to add channel');return;}document.getElementById('ch_name').value='';document.getElementById('ch_psk').value='';await loadChannels();}const tzVal=document.getElementById('timezone').value;const sendLoc=document.getElementById('send_loc_adv').checked?'1':'0';const r=await jpost('/api/save',{node_name:document.getElementById('node_name').value,node_lat:document.getElementById('node_lat').value,node_lon:document.getElementById('node_lon').value,send_loc_adv:sendLoc,ssid:document.getElementById('wifi_ssid').value,pass:document.getElementById('wifi_pass').value,timezone:tzVal,tz_offset:String(calcTimezoneOffsetMinutes(tzVal)),region:document.getElementById('region').value,freq:document.getElementById('freq').value,bw:document.getElementById('bw').value,sf:document.getElementById('sf').value,cr:document.getElementById('cr').value,pwr:document.getElementById('pwr').value});alert(r.message||r.error||'done');if(r.ok){timezoneDirty=false;nodeNameDirty=false;locationDirty=false;}}"
      "async function addChannel(){if(channelMutationInFlight){return;}const name=(document.getElementById('ch_name').value||'').trim();const psk=(document.getElementById('ch_psk').value||'').trim();if(!name){alert('Channel name is required');return;}if(name[0]!=='#'&&!psk){alert('PSK is required for non-# channels');return;}let r=null;channelMutationInFlight=true;try{r=await jpost('/api/channels/add',{name,psk});}finally{channelMutationInFlight=false;}if(!r||!r.ok){alert((r&&r.error)||'failed');return;}document.getElementById('ch_name').value='';document.getElementById('ch_psk').value='';await loadChannels();}"
      "async function boot(){showTab('config');bindNodeNameInput();bindLocationInputs();bindRadioInputs();bindTimezoneInput();ensureTimezoneOptions('UTC0');await loadPresets();await loadStatus(true);await loadChannels();await loadContacts();setInterval(()=>{loadStatus(false);loadContacts();},4000);}boot();"
      "</script></div></body></html>";

  g_server.send(200, "text/html", html);
}

void handleStatus() {
  plumeria::mesh::MeshRadioStats radio_stats{};
  if (g_mesh) {
    g_mesh->getRadioStats(&radio_stats);
  }

  char node_name[32] = {};
  char public_key_hex[65] = {};
  if (g_mesh) {
    g_mesh->getNodeName(node_name, sizeof(node_name));
    g_mesh->getPublicKeyHex(public_key_hex, sizeof(public_key_hex));
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
  payload += ",\"public_key\":";
  payload += jsonString(public_key_hex);
  payload += ",\"node_lat\":";
  payload += String(g_settings.node_latitude, 6);
  payload += ",\"node_lon\":";
  payload += String(g_settings.node_longitude, 6);
  payload += ",\"send_loc_adv\":";
  payload += g_settings.send_location_in_advert ? "true" : "false";
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

void handleContacts() {
  String payload = "{\"ok\":true,\"contacts\":[";
  if (g_mesh) {
    memset(g_contacts_web_buf, 0, sizeof(g_contacts_web_buf));
    const int count = g_mesh->exportContacts(g_contacts_web_buf, 160);

    for (int i = 1; i < count; i++) {
      plumeria::mesh::MeshContactSummary key = g_contacts_web_buf[i];
      int j = i - 1;
      while (j >= 0 && contactSortBefore(key, g_contacts_web_buf[j])) {
        g_contacts_web_buf[j + 1] = g_contacts_web_buf[j];
        j--;
      }
      g_contacts_web_buf[j + 1] = key;
    }

    for (int i = 0; i < count; i++) {
      if (i != 0) {
        payload += ",";
      }
      payload += "{\"name\":";
      payload += jsonString(g_contacts_web_buf[i].name);
      payload += ",\"pubkey\":";
      payload += jsonString(g_contacts_web_buf[i].public_key_hex);
      payload += ",\"favorite\":";
      payload += g_contacts_web_buf[i].favorite ? "true" : "false";
      payload += ",\"type\":";
      payload += String(static_cast<unsigned>(g_contacts_web_buf[i].type));
      payload += ",\"lastmod\":";
      payload += String(g_contacts_web_buf[i].lastmod);
      payload += "}";
    }
  }
  payload += "]}";
  sendJsonOk(payload);
}

void handleContactFavorite() {
  if (!g_mesh) {
    sendJsonError("Mesh adapter unavailable", 503);
    return;
  }

  String pubkey = g_server.arg("pubkey");
  String favorite = g_server.arg("favorite");
  pubkey.trim();
  favorite.trim();

  if (pubkey.length() != 64) {
    sendJsonError("Public key is required");
    return;
  }

  const bool favorite_value = parseBoolArg(favorite, true);
  if (!g_mesh->setContactFavoriteByPublicKeyHex(pubkey.c_str(), favorite_value)) {
    sendJsonError("Failed to update contact favorite");
    return;
  }

  sendJsonOk("{\"ok\":true}");
}

void handleContactRemove() {
  if (!g_mesh) {
    sendJsonError("Mesh adapter unavailable", 503);
    return;
  }

  String pubkey = g_server.arg("pubkey");
  pubkey.trim();

  if (pubkey.length() != 64) {
    sendJsonError("Public key is required");
    return;
  }

  if (!g_mesh->removeContactByPublicKeyHex(pubkey.c_str())) {
    sendJsonError("Failed to remove contact");
    return;
  }

  sendJsonOk("{\"ok\":true}");
}

void handleUtilAdvertLocal() {
  if (!g_mesh) {
    sendJsonError("Mesh adapter unavailable", 503);
    return;
  }
  if (!g_mesh->broadcastSelfAdvertNow()) {
    sendJsonError("Failed to send local advert");
    return;
  }
  sendJsonOk("{\"ok\":true,\"message\":\"Local advert sent\"}");
}

void handleUtilAdvertFlood() {
  if (!g_mesh) {
    sendJsonError("Mesh adapter unavailable", 503);
    return;
  }
  if (!g_mesh->broadcastSelfAdvertFloodNow()) {
    sendJsonError("Failed to send flood advert");
    return;
  }
  sendJsonOk("{\"ok\":true,\"message\":\"Flood advert sent\"}");
}

void handleUtilExportConfig() {
  String text = buildConfigText();
  g_server.sendHeader("Content-Disposition", "attachment; filename=plumeria-config.yaml");
  g_server.send(200, "text/plain", text);
}

void handleUtilImportConfig() {
  String content = g_server.arg("content");
  content.trim();
  if (content.length() == 0) {
    sendJsonError("Config content is required");
    return;
  }

  char err[96] = {};
  if (!applyConfigTextInternal(content.c_str(), true, err, sizeof(err))) {
    sendJsonError(err[0] ? err : "Failed to import config");
    return;
  }

  sendJsonOk("{\"ok\":true,\"needs_reboot\":true,\"message\":\"Config imported. Rebooting firmware...\"}");
}

void handleSaveAll() {
  String node_name = g_server.arg("node_name");
  String node_lat = g_server.arg("node_lat");
  String node_lon = g_server.arg("node_lon");
  String send_loc_adv = g_server.arg("send_loc_adv");
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
  node_lat.trim();
  node_lon.trim();
  send_loc_adv.trim();
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

  if (node_lat.length() == 0) {
    node_lat = String(g_settings.node_latitude, 6);
  }
  if (node_lon.length() == 0) {
    node_lon = String(g_settings.node_longitude, 6);
  }

  const double node_latitude = node_lat.toDouble();
  const double node_longitude = node_lon.toDouble();
  if (node_latitude < -90.0 || node_latitude > 90.0) {
    sendJsonError("Latitude out of range");
    return;
  }
  if (node_longitude < -180.0 || node_longitude > 180.0) {
    sendJsonError("Longitude out of range");
    return;
  }
  const bool send_location_in_advert = parseBoolArg(send_loc_adv, g_settings.send_location_in_advert);

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
  g_settings.node_latitude = node_latitude;
  g_settings.node_longitude = node_longitude;
  g_settings.send_location_in_advert = send_location_in_advert;
  copyString(g_settings.timezone, sizeof(g_settings.timezone), timezone.c_str());
  g_settings.timezone_offset_minutes = static_cast<int16_t>(tz_offset_min);

  if (g_mesh) {
    g_mesh->setNodeName(g_settings.node_name);
    if (!g_mesh->setAdvertLocation(g_settings.send_location_in_advert, g_settings.node_latitude,
                                   g_settings.node_longitude)) {
      sendJsonError("Failed to apply advert location");
      return;
    }
    g_mesh->broadcastSelfAdvertNow();
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
  g_server.on("/api/contacts", HTTP_GET, handleContacts);

  g_server.on("/api/save", HTTP_POST, handleSaveAll);
  g_server.on("/api/channels/add", HTTP_POST, handleChannelAdd);
  g_server.on("/api/channels/remove", HTTP_POST, handleChannelRemove);
  g_server.on("/api/contacts/favorite", HTTP_POST, handleContactFavorite);
  g_server.on("/api/contacts/remove", HTTP_POST, handleContactRemove);
  g_server.on("/api/util/advert/local", HTTP_POST, handleUtilAdvertLocal);
  g_server.on("/api/util/advert/flood", HTTP_POST, handleUtilAdvertFlood);
  g_server.on("/api/util/export", HTTP_GET, handleUtilExportConfig);
  g_server.on("/api/util/import", HTTP_POST, handleUtilImportConfig);

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
  double node_latitude = prefs.getDouble("node_lat", kDefaultNodeLatitude);
  double node_longitude = prefs.getDouble("node_lon", kDefaultNodeLongitude);
  bool send_location_in_advert = prefs.getBool("send_loc_adv", kDefaultSendLocationInAdvert);
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
  out_settings->node_latitude = node_latitude;
  out_settings->node_longitude = node_longitude;
  out_settings->send_location_in_advert = send_location_in_advert;
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
    g_mesh->setAdvertLocation(g_settings.send_location_in_advert, g_settings.node_latitude,
                              g_settings.node_longitude);
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

bool exportConfigText(String* out_text) {
  if (!out_text) {
    return false;
  }
  *out_text = buildConfigText();
  return true;
}

bool importConfigText(const char* text, bool queue_reboot, char* err, size_t err_size) {
  return applyConfigTextInternal(text, queue_reboot, err, err_size);
}

bool setSendLocationInAdvert(bool enabled, char* err, size_t err_size) {
  WebSettings next{};
  loadSettings(&next);
  if (next.send_location_in_advert == enabled) {
    setImportError(err, err_size, "");
    return true;
  }

  next.send_location_in_advert = enabled;

  if (g_mesh) {
    if (!g_mesh->setAdvertLocation(next.send_location_in_advert, next.node_latitude,
                                   next.node_longitude)) {
      setImportError(err, err_size, "Failed to apply advert location");
      return false;
    }
    g_mesh->broadcastSelfAdvertNow();
  }

  saveSettings(next);
  if (g_running) {
    g_settings = next;
  }

  setImportError(err, err_size, "");
  return true;
}

}  // namespace web
}  // namespace plumeria
