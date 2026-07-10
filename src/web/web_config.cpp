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

constexpr uint8_t kContactTypeRepeater = 2;

constexpr uint32_t kStaConnectTimeoutMs = 10000;
constexpr uint32_t kRebootDelayMs = 1500;
constexpr uint32_t kNtpSyncTimeoutMs = 6000;
constexpr time_t kTimeValidEpoch = 1700000000;
constexpr char kPrefsNs[] = "plumeria_web";
constexpr char kDefaultSsid[] = "";
constexpr char kDefaultPass[] = "";
constexpr char kDefaultNodeName[] = "Plumeria";
constexpr double kDefaultNodeLatitude = 0.0;
constexpr double kDefaultNodeLongitude = 0.0;
constexpr bool kDefaultSendLocationInAdvert = false;
constexpr uint16_t kDefaultAdvertIntervalMinutes = 360;
constexpr uint16_t kMinAdvertIntervalMinutes = 60;
constexpr uint16_t kDefaultScreenTimeoutSeconds = 30;
constexpr uint16_t kMinScreenTimeoutSeconds = 1;
constexpr uint16_t kMaxScreenTimeoutSeconds = 600;
constexpr bool kDefaultNotificationsEnabled = true;
constexpr char kDefaultTimezone[] = "UTC0";
constexpr char kDefaultRegion[] = "US";
constexpr uint8_t kDefaultPathHashMode = 0;
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
bool g_server_enabled = false;
char g_mode[8] = "off";
char g_ip[20] = "";
plumeria::mesh::MeshAdapter* g_mesh = nullptr;
plumeria::web::WebSettings g_settings{};
bool g_reboot_pending = false;
uint32_t g_reboot_at_ms = 0;
char g_channels_web_buf[40][32]{};
plumeria::mesh::MeshContactSummary g_contacts_web_buf[160]{};
plumeria::mesh::MeshChannelConfig g_imported_channels_buf[40]{};
plumeria::mesh::MeshChannelConfig g_export_channels_buf[40]{};
char g_imported_favorite_pubkeys[160][65]{};
char g_existing_channels_buf[40][32]{};
char g_export_identity_public_hex[193]{};
char g_export_identity_private_hex[193]{};

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

const char* boolToText(bool value) {
  return value ? "true" : "false";
}

void buildPosixUtcFromOffsetMinutes(int16_t offset_minutes, char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }

  // POSIX TZ uses reversed sign: local = UTC - value.
  int posix_value_min = -static_cast<int>(offset_minutes);
  bool neg = posix_value_min < 0;
  int abs_min = neg ? -posix_value_min : posix_value_min;
  int hours = abs_min / 60;
  int mins = abs_min % 60;

  if (mins == 0) {
    snprintf(out, out_size, "UTC%s%d", neg ? "-" : "", hours);
  } else {
    snprintf(out, out_size, "UTC%s%d:%02d", neg ? "-" : "", hours, mins);
  }
}

void resolveTimezoneSpec(char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (g_settings.timezone_posix[0] != '\0') {
    strncpy(out, g_settings.timezone_posix, out_size - 1);
    out[out_size - 1] = '\0';
    return;
  }
  // Prefer explicit POSIX-style TZ strings when provided. Browser IANA names
  // (contains '/') are not directly usable in this runtime, so fall back to
  // stored offset-derived UTC spec.
  if (g_settings.timezone[0] != '\0' && strchr(g_settings.timezone, '/') == nullptr) {
    strncpy(out, g_settings.timezone, out_size - 1);
    out[out_size - 1] = '\0';
  } else {
    buildPosixUtcFromOffsetMinutes(g_settings.timezone_offset_minutes, out, out_size);
  }
}

bool radioParamsEqual(float lhs_freq, float lhs_bw, uint8_t lhs_sf, uint8_t lhs_cr, int8_t lhs_pwr,
                      float rhs_freq, float rhs_bw, uint8_t rhs_sf, uint8_t rhs_cr, int8_t rhs_pwr) {
  return (fabsf(lhs_freq - rhs_freq) <= 0.0005f) && (fabsf(lhs_bw - rhs_bw) <= 0.05f) &&
         (lhs_sf == rhs_sf) && (lhs_cr == rhs_cr) && (lhs_pwr == rhs_pwr);
}

void applyTimezoneOffsetFromSettings() {
  char tz_buf[24] = {};
  resolveTimezoneSpec(tz_buf, sizeof(tz_buf));
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

uint16_t clampScreenTimeoutSeconds(int timeout_seconds) {
  if (timeout_seconds < static_cast<int>(kMinScreenTimeoutSeconds)) {
    return kDefaultScreenTimeoutSeconds;
  }
  if (timeout_seconds > static_cast<int>(kMaxScreenTimeoutSeconds)) {
    return kMaxScreenTimeoutSeconds;
  }
  return static_cast<uint16_t>(timeout_seconds);
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
  if (!prefs.begin(kPrefsNs, false)) {
    return;
  }
  prefs.putString("node_name", settings.node_name);
  prefs.putDouble("node_lat", settings.node_latitude);
  prefs.putDouble("node_lon", settings.node_longitude);
  prefs.putBool("send_loc_adv", settings.send_location_in_advert);
  prefs.putUShort("adv_int_min", settings.advert_interval_minutes);
  prefs.putString("wifi_ssid", settings.wifi_ssid);
  prefs.putString("wifi_pass", settings.wifi_pass);
  prefs.putString("timezone", settings.timezone);
  prefs.putString("timezone_posix", settings.timezone_posix);
  prefs.putInt("tz_offset", static_cast<int>(settings.timezone_offset_minutes));
  prefs.putString("region", settings.region);
  prefs.putFloat("lora_freq", settings.lora_freq_mhz);
  prefs.putFloat("lora_bw", settings.lora_bw_khz);
  prefs.putUChar("lora_sf", settings.lora_sf);
  prefs.putUChar("lora_cr", settings.lora_cr);
  prefs.putChar("lora_pwr", settings.lora_tx_power_dbm);
  prefs.putUChar("path_hash_mode", settings.path_hash_mode);
  prefs.putBool("multi_ack", settings.multi_ack);
  prefs.putBool("repeater", settings.repeater_mode);
  prefs.putBool("notifications", settings.notifications_enabled);
  prefs.putUShort("screen_timeout", settings.screen_timeout_seconds);
  prefs.putString("mesh_region", settings.mesh_region);
  prefs.end();
}

void setIpFrom(const IPAddress& address) {
  String ip = address.toString();
  copyString(g_ip, sizeof(g_ip), ip.c_str());
}

bool syncTimeFromNtp() {
  char tz_buf[24] = {};
  resolveTimezoneSpec(tz_buf, sizeof(tz_buf));
  configTzTime(tz_buf, "pool.ntp.org", "time.nist.gov", "time.google.com");

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

  syncTimeFromNtp();

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
  out.reserve(6144);
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
  out += "timezone_posix: ";
  out += configSafeValue(g_settings.timezone_posix);
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
  out += "advert_interval_minutes: ";
  out += String(g_settings.advert_interval_minutes);
  out += "\n";
  out += "path_hash_mode: ";
  out += String(g_settings.path_hash_mode);
  out += "\n";
  out += "multi_ack: ";
  out += g_settings.multi_ack ? "1" : "0";
  out += "\n";
  out += "repeater_mode: ";
  out += g_settings.repeater_mode ? "1" : "0";
  out += "\n";
  out += "notifications_enabled: ";
  out += g_settings.notifications_enabled ? "1" : "0";
  out += "\n";
  out += "mesh_region: ";
  out += configSafeValue(g_settings.mesh_region);
  out += "\n";
  out += "screen_timeout_seconds: ";
  out += String(g_settings.screen_timeout_seconds);
  out += "\n";

  if (g_mesh) {
    const int nign = g_mesh->ignoredCount();
    for (int i = 0; i < nign; i++) {
      char ikey[65] = {};
      char iname[32] = {};
      if (g_mesh->getIgnoredEntry(i, ikey, sizeof(ikey), iname, sizeof(iname))) {
        out += "ignore: ";
        out += ikey;
        out += "|";
        out += configSafeValue(iname);
        out += "\n";
      }
    }
  }

  if (g_mesh) {
    memset(g_export_identity_public_hex, 0, sizeof(g_export_identity_public_hex));
    memset(g_export_identity_private_hex, 0, sizeof(g_export_identity_private_hex));
    if (g_mesh->getIdentityKeysHex(g_export_identity_public_hex, sizeof(g_export_identity_public_hex),
                                   g_export_identity_private_hex, sizeof(g_export_identity_private_hex))) {
      out += "identity_public_key: ";
      out += g_export_identity_public_hex;
      out += "\n";
      out += "identity_private_key: ";
      out += g_export_identity_private_hex;
      out += "\n";
    }
  }

  out += "channels:\n";
  if (g_mesh) {
    memset(g_export_channels_buf, 0, sizeof(g_export_channels_buf));
    const int count = g_mesh->exportChannelConfigs(g_export_channels_buf, 40);
    for (int i = 0; i < count; i++) {
      out += "channel: ";
      out += configSafeValue(g_export_channels_buf[i].name);
      out += "|";
      out += configSafeValue(g_export_channels_buf[i].psk_base64);
      out += "\n";
    }
  }

  out += "contacts:\n";
  if (g_mesh) {
    memset(g_contacts_web_buf, 0, sizeof(g_contacts_web_buf));
    const int contact_count = g_mesh->exportContacts(g_contacts_web_buf, 160);
    for (int i = 0; i < contact_count; i++) {
      if (g_contacts_web_buf[i].public_key_hex[0] == '\0') {
        continue;
      }

      String safe_name = configSafeValue(g_contacts_web_buf[i].name);
      safe_name.replace("|", "/");

      out += "contact: ";
      out += g_contacts_web_buf[i].public_key_hex;
      out += "|";
      out += safe_name;
      out += "|";
      out += (g_contacts_web_buf[i].type == kContactTypeRepeater) ? "repeater" : "contact";
      out += "\n";
    }
  }

  out += "favorites:\n";
  if (g_mesh) {
    memset(g_contacts_web_buf, 0, sizeof(g_contacts_web_buf));
    const int contact_count = g_mesh->exportContacts(g_contacts_web_buf, 160);
    for (int i = 0; i < contact_count; i++) {
      if (!g_contacts_web_buf[i].favorite) {
        continue;
      }
      if (g_contacts_web_buf[i].public_key_hex[0] == '\0') {
        continue;
      }
      out += "favorite_contact: ";
      out += g_contacts_web_buf[i].public_key_hex;
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
  if (message[0] != '\0') {
    Serial.printf("[IMPORT] ERROR: %s\n", message);
  }
}

bool applyConfigTextInternal(const char* text, bool queue_reboot, char* err, size_t err_size) {
  if (!text || text[0] == '\0') {
    setImportError(err, err_size, "Config content is empty");
    return false;
  }

  Serial.println("[IMPORT] ----- begin -----");
  Serial.printf("[IMPORT] queue_reboot request: %s\n", boolToText(queue_reboot));
  Serial.printf("[IMPORT] payload bytes: %u\n", static_cast<unsigned>(strlen(text)));

  plumeria::web::WebSettings imported = g_settings;
  bool saw_channels = false;
  int imported_channel_count = 0;
  int imported_contact_count = 0;
  int imported_favorite_count = 0;
  bool saw_ignored = false;
  int imported_ignored_count = 0;
  bool saw_identity_private_key = false;
  bool saw_identity_public_key = false;
  bool saw_timezone = false;
  bool saw_tz_offset = false;
  bool saw_timezone_posix = false;
  bool saw_region = false;
  bool saw_freq = false;
  bool saw_bw = false;
  bool saw_sf = false;
  bool saw_cr = false;
  bool saw_pwr = false;
  char imported_identity_public_hex[193] = {};
  char imported_identity_private_hex[193] = {};
  memset(g_imported_channels_buf, 0, sizeof(g_imported_channels_buf));
  memset(g_contacts_web_buf, 0, sizeof(g_contacts_web_buf));
  memset(g_imported_favorite_pubkeys, 0, sizeof(g_imported_favorite_pubkeys));

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
      // Empty SSID is valid: device falls back to AP mode until configured.
      copyString(imported.wifi_ssid, sizeof(imported.wifi_ssid), value.c_str());
    } else if (key.equals("wifi_pass")) {
      copyString(imported.wifi_pass, sizeof(imported.wifi_pass), value.c_str());
    } else if (key.equals("timezone")) {
      if (value.length() == 0) {
        value = kDefaultTimezone;
      }
      copyString(imported.timezone, sizeof(imported.timezone), value.c_str());
      saw_timezone = true;
    } else if (key.equals("timezone_posix")) {
      if (value.length() >= static_cast<int>(sizeof(imported.timezone_posix))) {
        setImportError(err, err_size, "timezone_posix too long");
        return false;
      }
      copyString(imported.timezone_posix, sizeof(imported.timezone_posix), value.c_str());
      saw_timezone_posix = true;
    } else if (key.equals("tz_offset")) {
      const int tz_offset = value.toInt();
      if (tz_offset < -840 || tz_offset > 840) {
        setImportError(err, err_size, "tz_offset out of range");
        return false;
      }
      imported.timezone_offset_minutes = static_cast<int16_t>(tz_offset);
      saw_tz_offset = true;
    } else if (key.equals("region")) {
      if (!findRegion(value.c_str())) {
        setImportError(err, err_size, "Unknown region");
        return false;
      }
      copyString(imported.region, sizeof(imported.region), value.c_str());
      saw_region = true;
    } else if (key.equals("freq")) {
      const float freq = value.toFloat();
      if (freq < 100.0f || freq > 2500.0f) {
        setImportError(err, err_size, "Frequency out of range");
        return false;
      }
      imported.lora_freq_mhz = freq;
      saw_freq = true;
    } else if (key.equals("bw")) {
      const float bw = value.toFloat();
      if (bw < 7.0f || bw > 500.0f) {
        setImportError(err, err_size, "Bandwidth out of range");
        return false;
      }
      imported.lora_bw_khz = bw;
      saw_bw = true;
    } else if (key.equals("sf")) {
      const int sf = value.toInt();
      if (sf < 5 || sf > 12) {
        setImportError(err, err_size, "SF out of range");
        return false;
      }
      imported.lora_sf = static_cast<uint8_t>(sf);
      saw_sf = true;
    } else if (key.equals("cr")) {
      const int cr = value.toInt();
      if (cr < 5 || cr > 8) {
        setImportError(err, err_size, "CR out of range");
        return false;
      }
      imported.lora_cr = static_cast<uint8_t>(cr);
      saw_cr = true;
    } else if (key.equals("pwr")) {
      const int pwr = value.toInt();
      if (pwr < 1 || pwr > 30) {
        setImportError(err, err_size, "TX power out of range");
        return false;
      }
      imported.lora_tx_power_dbm = static_cast<int8_t>(pwr);
      saw_pwr = true;
    } else if (key.equals("advert_interval_minutes")) {
      const int minutes = value.toInt();
      if (minutes < static_cast<int>(kMinAdvertIntervalMinutes) || minutes > 65535) {
        setImportError(err, err_size, "Advert interval out of range");
        return false;
      }
      imported.advert_interval_minutes = static_cast<uint16_t>(minutes);
    } else if (key.equals("path_hash_mode")) {
      const int mode = value.toInt();
      if (mode < 0 || mode > 2) {
        setImportError(err, err_size, "path_hash_mode out of range");
        return false;
      }
      imported.path_hash_mode = static_cast<uint8_t>(mode);
    } else if (key.equals("multi_ack")) {
      imported.multi_ack = parseBoolArg(value, imported.multi_ack);
    } else if (key.equals("repeater_mode")) {
      imported.repeater_mode = parseBoolArg(value, imported.repeater_mode);
    } else if (key.equals("notifications_enabled")) {
      imported.notifications_enabled = parseBoolArg(value, imported.notifications_enabled);
    } else if (key.equals("ignore")) {
      // Format: "<pubkey_hex>|<name>". Applied inline to avoid a large static
      // buffer: the first entry clears the existing list; persisted at apply.
      const int bar = value.indexOf('|');
      String hex = (bar >= 0) ? value.substring(0, bar) : value;
      String name = (bar >= 0) ? value.substring(bar + 1) : String("");
      hex.trim();
      name.trim();
      if (hex.length() > 0 && g_mesh) {
        if (!saw_ignored) {
          g_mesh->clearIgnoredContacts();
          saw_ignored = true;
        }
        g_mesh->addIgnoredContact(hex.c_str(), name.c_str());
        imported_ignored_count++;
      }
    } else if (key.equals("mesh_region")) {
      if (value.length() >= static_cast<int>(sizeof(imported.mesh_region))) {
        setImportError(err, err_size, "mesh_region too long");
        return false;
      }
      copyString(imported.mesh_region, sizeof(imported.mesh_region), value.c_str());
    } else if (key.equals("screen_timeout_seconds")) {
      const int timeout_seconds = value.toInt();
      if (timeout_seconds < static_cast<int>(kMinScreenTimeoutSeconds) ||
          timeout_seconds > static_cast<int>(kMaxScreenTimeoutSeconds)) {
        setImportError(err, err_size, "Screen timeout out of range");
        return false;
      }
      imported.screen_timeout_seconds = static_cast<uint16_t>(timeout_seconds);
    } else if (key.equals("identity_public_key")) {
      value.trim();
      if (value.length() > 0) {
        if (value.length() >= static_cast<int>(sizeof(imported_identity_public_hex))) {
          setImportError(err, err_size, "identity_public_key is too long");
          return false;
        }
        copyString(imported_identity_public_hex, sizeof(imported_identity_public_hex), value.c_str());
        saw_identity_public_key = true;
      }
    } else if (key.equals("identity_private_key")) {
      value.trim();
      if (value.length() > 0) {
        if (value.length() >= static_cast<int>(sizeof(imported_identity_private_hex))) {
          setImportError(err, err_size, "identity_private_key is too long");
          return false;
        }
        copyString(imported_identity_private_hex, sizeof(imported_identity_private_hex), value.c_str());
        saw_identity_private_key = true;
      }
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
    } else if (key.equals("contact")) {
      value.trim();
      if (value.length() == 0) {
        continue;
      }
      if (imported_contact_count >= 160) {
        continue;
      }

      int sep = value.indexOf('|');
      String left = sep >= 0 ? value.substring(0, sep) : value;
      String rest = sep >= 0 ? value.substring(sep + 1) : String("");
      left.trim();
      rest.trim();

      sep = rest.indexOf('|');
      String right = sep >= 0 ? rest.substring(0, sep) : rest;
      String role = sep >= 0 ? rest.substring(sep + 1) : String("");
      right.trim();
      role.trim();

      String pubkey;
      String name;
      if (left.length() == 64) {
        pubkey = left;
        name = right;
      } else if (right.length() == 64) {
        pubkey = right;
        name = left;
      } else {
        setImportError(err, err_size, "contact must include a 64-char hex public key");
        return false;
      }

      uint8_t contact_type = 0;
      if (role.equalsIgnoreCase("repeater") || role.equals("2") || role.equalsIgnoreCase("type=2") ||
          role.equalsIgnoreCase("repeater=true") || role.equalsIgnoreCase("is_repeater=true")) {
        contact_type = kContactTypeRepeater;
      }

      copyString(g_contacts_web_buf[imported_contact_count].public_key_hex,
             sizeof(g_contacts_web_buf[imported_contact_count].public_key_hex), pubkey.c_str());
      copyString(g_contacts_web_buf[imported_contact_count].name,
             sizeof(g_contacts_web_buf[imported_contact_count].name), name.c_str());
      g_contacts_web_buf[imported_contact_count].type = contact_type;
      imported_contact_count++;
    } else if (key.equals("favorite_contact")) {
      value.trim();
      if (value.length() == 0) {
        continue;
      }
      if (value.length() != 64) {
        setImportError(err, err_size, "favorite_contact must be a 64-char hex public key");
        return false;
      }
      if (imported_favorite_count >= 160) {
        continue;
      }
      copyString(g_imported_favorite_pubkeys[imported_favorite_count],
                 sizeof(g_imported_favorite_pubkeys[imported_favorite_count]), value.c_str());
      imported_favorite_count++;
    }
  }

  // Honor region-only updates in imported config by applying preset defaults
  // when explicit radio fields are omitted.
  if (saw_region && !(saw_freq || saw_bw || saw_sf || saw_cr || saw_pwr)) {
    const RegionPreset* preset = findRegion(imported.region);
    if (preset) {
      imported.lora_freq_mhz = preset->frequency_mhz;
      imported.lora_bw_khz = preset->bandwidth_khz;
      imported.lora_sf = preset->spreading_factor;
      imported.lora_cr = preset->coding_rate;
      imported.lora_tx_power_dbm = preset->tx_power_dbm;
      saw_freq = saw_bw = saw_sf = saw_cr = saw_pwr = true;
    }
  }

  if (!saw_timezone_posix && (saw_timezone || saw_tz_offset)) {
    if (imported.timezone[0] != '\0' && strchr(imported.timezone, '/') == nullptr) {
      copyString(imported.timezone_posix, sizeof(imported.timezone_posix), imported.timezone);
    } else {
      buildPosixUtcFromOffsetMinutes(imported.timezone_offset_minutes, imported.timezone_posix,
                                     sizeof(imported.timezone_posix));
    }
  }

  const bool wifi_changed = (strcmp(g_settings.wifi_ssid, imported.wifi_ssid) != 0) ||
                            (strcmp(g_settings.wifi_pass, imported.wifi_pass) != 0);
  const bool radio_changed = !radioParamsEqual(
      g_settings.lora_freq_mhz, g_settings.lora_bw_khz, g_settings.lora_sf, g_settings.lora_cr,
      g_settings.lora_tx_power_dbm, imported.lora_freq_mhz, imported.lora_bw_khz, imported.lora_sf,
      imported.lora_cr, imported.lora_tx_power_dbm);
  bool identity_changed = false;

  Serial.printf("[IMPORT] node_name=%s\n", imported.node_name);
  Serial.printf("[IMPORT] node_lat=%.6f\n", imported.node_latitude);
  Serial.printf("[IMPORT] node_lon=%.6f\n", imported.node_longitude);
  Serial.printf("[IMPORT] send_loc_adv=%s\n", boolToText(imported.send_location_in_advert));
  Serial.printf("[IMPORT] wifi_ssid=%s\n", imported.wifi_ssid[0] ? imported.wifi_ssid : "(empty)");
  Serial.printf("[IMPORT] wifi_pass_len=%u\n", static_cast<unsigned>(strlen(imported.wifi_pass)));
  Serial.printf("[IMPORT] timezone=%s\n", imported.timezone);
  Serial.printf("[IMPORT] timezone_posix=%s\n", imported.timezone_posix[0] ? imported.timezone_posix : "(empty)");
  Serial.printf("[IMPORT] tz_offset=%d\n", static_cast<int>(imported.timezone_offset_minutes));
  Serial.printf("[IMPORT] region=%s\n", imported.region);
  Serial.printf("[IMPORT] freq=%.3f\n", imported.lora_freq_mhz);
  Serial.printf("[IMPORT] bw=%.1f\n", imported.lora_bw_khz);
  Serial.printf("[IMPORT] sf=%u\n", static_cast<unsigned>(imported.lora_sf));
  Serial.printf("[IMPORT] cr=%u\n", static_cast<unsigned>(imported.lora_cr));
  Serial.printf("[IMPORT] pwr=%d\n", static_cast<int>(imported.lora_tx_power_dbm));
  Serial.printf("[IMPORT] advert_interval_minutes=%u\n", static_cast<unsigned>(imported.advert_interval_minutes));
  Serial.printf("[IMPORT] path_hash_mode=%u\n", static_cast<unsigned>(imported.path_hash_mode));
  Serial.printf("[IMPORT] multi_ack=%s\n", boolToText(imported.multi_ack));
  Serial.printf("[IMPORT] notifications_enabled=%s\n", boolToText(imported.notifications_enabled));
  Serial.printf("[IMPORT] mesh_region=%s\n", imported.mesh_region[0] ? imported.mesh_region : "(empty)");
  Serial.printf("[IMPORT] screen_timeout_seconds=%u\n", static_cast<unsigned>(imported.screen_timeout_seconds));
  Serial.printf("[IMPORT] identity_public_key_present=%s\n", boolToText(saw_identity_public_key));
  Serial.printf("[IMPORT] identity_private_key_present=%s\n", boolToText(saw_identity_private_key));
  Serial.printf("[IMPORT] channels_section=%s count=%d\n", boolToText(saw_channels), imported_channel_count);
  for (int i = 0; i < imported_channel_count; i++) {
    const bool has_psk = g_imported_channels_buf[i].psk_base64[0] != '\0';
    Serial.printf("[IMPORT] channel[%d] name=%s psk_present=%s\n", i,
                  g_imported_channels_buf[i].name,
                  boolToText(has_psk));
  }
  Serial.printf("[IMPORT] contacts_count=%d\n", imported_contact_count);
  for (int i = 0; i < imported_contact_count; i++) {
    Serial.printf("[IMPORT] contact[%d] key=%s name=%s repeater=%s\n", i,
                  g_contacts_web_buf[i].public_key_hex,
                  g_contacts_web_buf[i].name[0] ? g_contacts_web_buf[i].name : "(empty)",
                  boolToText(g_contacts_web_buf[i].type == kContactTypeRepeater));
  }
  Serial.printf("[IMPORT] favorites_count=%d\n", imported_favorite_count);
  for (int i = 0; i < imported_favorite_count; i++) {
    Serial.printf("[IMPORT] favorite_contact[%d]=%s\n", i, g_imported_favorite_pubkeys[i]);
  }

  if (saw_identity_public_key != saw_identity_private_key) {
    setImportError(err, err_size, "Both identity_public_key and identity_private_key are required");
    return false;
  }

  if (saw_identity_private_key && !g_mesh) {
    setImportError(err, err_size, "Mesh adapter unavailable for identity import");
    return false;
  }

  g_settings = imported;

  if (g_mesh) {
    Serial.println("[IMPORT] applying to mesh adapter");
    if (saw_identity_private_key) {
      Serial.println("[IMPORT] applying identity keys");
      if (!g_mesh->importIdentityKeysHex(imported_identity_public_hex, imported_identity_private_hex)) {
        setImportError(err, err_size, "Failed to import identity keys");
        return false;
      }
      identity_changed = true;
    }

    Serial.printf("[IMPORT] setNodeName(%s)\n", g_settings.node_name);
    g_mesh->setNodeName(g_settings.node_name);
    Serial.printf("[IMPORT] setAdvertLocation(enabled=%s lat=%.6f lon=%.6f)\n",
                  boolToText(g_settings.send_location_in_advert), g_settings.node_latitude,
                  g_settings.node_longitude);
    if (!g_mesh->setAdvertLocation(g_settings.send_location_in_advert, g_settings.node_latitude,
                                   g_settings.node_longitude)) {
      setImportError(err, err_size, "Failed to apply advert location");
      return false;
    }
    Serial.printf("[IMPORT] setGpsEnabled(%s)\n", boolToText(!g_settings.send_location_in_advert));
    g_mesh->setGpsEnabled(!g_settings.send_location_in_advert);
    Serial.printf("[IMPORT] setAutoAdvertIntervalMinutes(%u)\n",
                  static_cast<unsigned>(g_settings.advert_interval_minutes));
    g_mesh->setAutoAdvertIntervalMinutes(g_settings.advert_interval_minutes);
    Serial.printf("[IMPORT] setPathHashMode(%u)\n", static_cast<unsigned>(g_settings.path_hash_mode));
    g_mesh->setPathHashMode(g_settings.path_hash_mode);
    Serial.printf("[IMPORT] setMultiAck(%s)\n", boolToText(g_settings.multi_ack));
    g_mesh->setMultiAck(g_settings.multi_ack);
    Serial.printf("[IMPORT] setRepeaterMode(%s)\n", boolToText(g_settings.repeater_mode));
    g_mesh->setRepeaterMode(g_settings.repeater_mode);
    Serial.printf("[IMPORT] setMeshRegion(%s)\n", g_settings.mesh_region[0] ? g_settings.mesh_region : "");
    g_mesh->setMeshRegion(g_settings.mesh_region);

    if (saw_ignored) {
      // Entries were cleared+added inline during parse; persist the final list.
      Serial.printf("[IMPORT] ignore list replaced (%d entries)\n", imported_ignored_count);
      g_mesh->persistIgnoredContacts();
    }

    if (saw_channels) {
      Serial.println("[IMPORT] replacing channels");
      memset(g_existing_channels_buf, 0, sizeof(g_existing_channels_buf));
      const int existing_count = g_mesh->exportChannels(g_existing_channels_buf, 40);
      for (int i = 0; i < existing_count; i++) {
        if (strcmp(g_existing_channels_buf[i], "Public") != 0) {
          Serial.printf("[IMPORT] removeChannel(%s)\n", g_existing_channels_buf[i]);
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
        Serial.printf("[IMPORT] addChannel(%s, psk_present=%s)\n",
                      g_imported_channels_buf[i].name,
                      boolToText(psk_ptr != nullptr));
        if (!g_mesh->addChannel(g_imported_channels_buf[i].name, psk_ptr)) {
          setImportError(err, err_size, "Failed to apply channels");
          return false;
        }
      }
    }

    if (imported_contact_count > 0) {
      int contacts_applied = 0;
      int contacts_failed = 0;
      for (int i = 0; i < imported_contact_count; i++) {
        if (g_contacts_web_buf[i].public_key_hex[0] == '\0') {
          contacts_failed++;
          continue;
        }
        const char* imported_name = g_contacts_web_buf[i].name[0] != '\0'
                                        ? g_contacts_web_buf[i].name
                                        : nullptr;
        const bool is_repeater = g_contacts_web_buf[i].type == kContactTypeRepeater;
        const bool ok = g_mesh->importContactByPublicKeyHex(g_contacts_web_buf[i].public_key_hex,
                         imported_name,
                         g_contacts_web_buf[i].type);
        Serial.printf("[IMPORT] contact[%d] key=%s name=%s repeater=%s (%s)\n", i,
                      g_contacts_web_buf[i].public_key_hex,
                      imported_name ? imported_name : "(empty)",
                boolToText(is_repeater),
                      ok ? "applied_or_created" : "failed");
        if (ok) {
          contacts_applied++;
        } else {
          contacts_failed++;
        }
      }
      Serial.printf("[IMPORT] contacts applied=%d failed=%d\n", contacts_applied, contacts_failed);
    }

    if (imported_favorite_count > 0) {
      // Ensure listed contacts are favorited, creating placeholders when needed.
      int fav_applied = 0;
      int fav_skipped = 0;
      for (int i = 0; i < imported_favorite_count; i++) {
        if (g_imported_favorite_pubkeys[i][0] == '\0') {
          fav_skipped++;
          continue;
        }
        const bool ok = g_mesh->importFavoriteContactByPublicKeyHex(g_imported_favorite_pubkeys[i]);
        Serial.printf("[IMPORT] favorite_contact[%d] %s (%s)\n", i,
                g_imported_favorite_pubkeys[i], ok ? "applied_or_created" : "failed");
        if (ok) {
          fav_applied++;
        } else {
          fav_skipped++;
        }
      }
      Serial.printf("[IMPORT] favorites applied=%d skipped=%d\n", fav_applied, fav_skipped);
    }

    Serial.println("[IMPORT] broadcastSelfAdvertNow()");
    g_mesh->broadcastSelfAdvertNow();
  } else {
    Serial.println("[IMPORT] mesh adapter not available; parsed settings saved only");
  }

  applyTimezoneOffsetFromSettings();
  saveSettings(g_settings);

  Serial.printf("[IMPORT] wifi_changed=%s radio_changed=%s identity_changed=%s\n",
                boolToText(wifi_changed), boolToText(radio_changed), boolToText(identity_changed));

  if (queue_reboot || wifi_changed || radio_changed || identity_changed) {
    g_reboot_pending = true;
    g_reboot_at_ms = millis() + kRebootDelayMs;
    Serial.printf("[IMPORT] reboot queued in %u ms\n", static_cast<unsigned>(kRebootDelayMs));
  } else {
    Serial.println("[IMPORT] reboot not required");
  }

  setImportError(err, err_size, "");
  Serial.println("[IMPORT] ----- success -----");
  return true;
}

void startFallbackAp() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
  // Cardputer builds have shown intermittent crashes in softAP startup.
  // Keep network stack disabled instead of forcing fallback AP mode.
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  copyString(g_mode, sizeof(g_mode), "off");
  g_ip[0] = '\0';
  return;
#endif

  WiFi.disconnect(false, true);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.setSleep(false);
  WiFi.softAP(kFallbackApSsid);
  delay(100);

  copyString(g_mode, sizeof(g_mode), "ap");
  setIpFrom(WiFi.softAPIP());
}

void bringupNetwork() {
  if (connectSta(g_settings.wifi_ssid, g_settings.wifi_pass)) {
    return;
  }

  startFallbackAp();
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
  static const char kRootPage[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Plumeria Config</title>
<style>
body{font-family:system-ui,Segoe UI,Arial,sans-serif;background:#0e1622;color:#e7eef6;margin:0;padding:14px}
.wrap{max-width:760px;margin:0 auto}.meta{font-size:.82rem;color:#9bb1c5;margin:0 0 10px}
section{background:#162333;border:1px solid #2a435d;border-radius:8px;padding:10px;margin:0 0 10px}
label{display:block;font-size:.85rem;color:#b7cadd;margin-top:6px}
input,select,button,textarea{width:100%;box-sizing:border-box;padding:8px;border-radius:6px;border:1px solid #355674;background:#0f1a28;color:#e7eef6;font:inherit}
button{margin-top:8px;background:#2c9bc8;border-color:#2c9bc8;font-weight:700}
small{color:#9bb1c5}.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.tabs{display:flex;gap:8px;margin:0 0 10px;flex-wrap:wrap}.tabbtn{width:auto;min-width:120px;margin-top:0;background:#1a2b3a}.tabbtn.active{background:#2c9bc8}
.tab{display:none}.tab.active{display:block}
.contacts-toolbar{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:end}
.contacts-toolbar small{align-self:center;justify-self:end}
#contacts{list-style:none;padding-left:0;margin:10px 0 0;display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:10px;align-items:start}
#contacts li{display:flex;flex-direction:column;gap:8px;border:1px solid #2a435d;border-radius:8px;background:#0f1a28;padding:10px}
#contacts li.active{border-color:#2c9bc8;box-shadow:0 0 0 1px rgba(44,155,200,.35) inset}
#contacts .card-top{display:flex;flex-direction:column;gap:6px;flex:1}
#contacts .contact-name{font-weight:700;font-size:.92rem;line-height:1.2;word-break:break-word}
#contacts .contact-meta{font-size:.75rem;color:#9bb1c5;line-height:1.25}
#contacts .key-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:start}
#contacts .key-text{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;font-size:.72rem;line-height:1.2;color:#cfe2f4;white-space:normal;overflow-wrap:anywhere;word-break:break-all}
#contacts .actions{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:auto}
.mini{width:100%;margin-top:0;padding:6px 10px;font-size:.78rem;min-height:34px;white-space:nowrap}
#map{height:300px;border:1px solid #2a435d;border-radius:8px}
.util-row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.util-row button{margin-top:0}
.import-panel{margin-top:10px;padding:10px;border:1px solid #355674;border-radius:8px;background:#101d2d}
.import-panel h4{margin:0 0 8px;font-size:.92rem;color:#9fe7ff}
@media(max-width:760px){.row,.contacts-toolbar,.util-row{grid-template-columns:1fr}}
@media(max-width:520px){#contacts .actions{grid-template-columns:1fr}}
</style>
</head><body><div class='wrap'>
<h1>Plumeria Web Config</h1>
<div id='meta' class='meta'>Loading...</div>
<div class='tabs'>
<button type='button' id='tab_btn_config' class='tabbtn active' onclick='showTab("config")'>Configuration</button>
<button type='button' id='tab_btn_contacts' class='tabbtn' onclick='showTab("contacts")'>Contacts</button>
<button type='button' id='tab_btn_heatmap' class='tabbtn' onclick='showTab("heatmap")'>Heat Map</button>
<button type='button' id='tab_btn_utils' class='tabbtn' onclick='showTab("utils")'>Utilities</button>
</div>

<div id='tab_config' class='tab active'>
<section><h3>Identity</h3>
<label>Node Name<input id='node_name' maxlength='31'></label>
<label>Public Key<input id='public_key' readonly></label>
</section>

<section><h3>Radio</h3>
<div class='row'>
<label>Region<select id='region'></select></label>
<label>&nbsp;<button type='button' id='apply_region_defaults' style='margin-top:22px'>Apply Region Defaults</button></label>
</div>
<label>Frequency MHz<input id='freq' type='number' step='0.001'></label>
<div class='row'>
<label>Bandwidth kHz<input id='bw' type='number' step='0.1'></label>
<label>Spreading Factor<input id='sf' type='number' min='5' max='12'></label>
</div>
<div class='row'>
<label>Coding Rate<input id='cr' type='number' min='5' max='8'></label>
<label>TX Power dBm<input id='pwr' type='number' min='1' max='30'></label>
</div>
<label>Advert Interval Minutes<input id='adv_int_min' type='number' min='60' step='1'></label>
<label>Multipaths
<select id='path_hash_mode'>
<option value='0'>1</option>
<option value='1'>2</option>
<option value='2'>3</option>
</select>
</label>
<label><input id='multi_ack' type='checkbox' style='width:auto;margin-right:8px'>Multi-ACK (show per-hop delivery count in message receipts)</label>
<label><input id='repeater_mode' type='checkbox' style='width:auto;margin-right:8px'>Repeater mode (forward mesh traffic for other nodes)</label>
<div style='color:#c07a2a;font-size:0.85em;margin:2px 0 8px 24px'>Warning: continuously repeats packets. Significantly increases radio airtime and battery drain; advertises this node as a repeater.</div>
<label><input id='notifications_enabled' type='checkbox' style='width:auto;margin-right:8px'>Notifications (new message chime)</label>
<label>Mesh Region (filter; blank = unfiltered)<input id='mesh_region' maxlength='31' placeholder='e.g. #mountains-west or leave blank'></label>
<label>Screen Timeout Seconds<input id='screen_timeout_sec' type='number' min='1' max='600' step='1'></label>
</section>

<section><h3>Channels</h3>
<div class='row'>
<label>Name<input id='ch_name' placeholder='Public, #SomeChannel'></label>
<label>PSK Base64 (optional for #channels)<input id='ch_psk' placeholder='izOH6cXN6mrJ5e26oRXNcg=='></label>
</div>
<button type='button' onclick='addChannel()'>Add Channel</button>
<ul id='channels'></ul>
</section>

<section><h3>Timezone & Location</h3>
<label>Timezone<select id='timezone'></select></label>
<div class='row'>
<label>Latitude<input id='node_lat' type='number' step='0.000001' min='-90' max='90'></label>
<label>Longitude<input id='node_lon' type='number' step='0.000001' min='-180' max='180'></label>
</div>
<label><input id='send_loc_adv' type='checkbox' style='width:auto;margin-right:8px'>Send location in adverts</label>
</section>

<section><h3>Wi-Fi</h3>
<label>SSID<input id='wifi_ssid'></label>
<label>Password<input id='wifi_pass'></label>
<button type='button' onclick='saveAll()'>Save Settings</button>
</section>
</div>

<div id='tab_contacts' class='tab'>
<section><h3>Contacts</h3>
<div class='contacts-toolbar'>
<label style='margin-top:0'>Filter by name<input id='contacts_filter' type='text' placeholder='Type part of a contact name'></label>
<small id='contacts_meta'>No contacts loaded.</small>
</div>
<ul id='contacts'></ul>
<small id='contacts_empty' style='display:none'>No contacts match the current filter.</small>
</section>
</div>

<div id='tab_heatmap' class='tab'>
<section><h3>Contacts Heat Map</h3><div id='map'></div><small id='map_meta'>No points yet.</small><br><small>Ignored contacts are excluded.</small></section>
</div>

<div id='tab_utils' class='tab'>
<section><h3>Advert Utilities</h3>
<div class='util-row'>
<button type='button' onclick='utilAdvertLocal()'>Advert Local (Zero Hop)</button>
<button type='button' onclick='utilAdvertFlood()'>Advert Flood</button>
</div>
</section>
<section><h3>Config Utilities</h3>
<button type='button' onclick='utilExportConfig()'>Export Config</button>
<div class='import-panel'>
<h4>Import Config</h4>
<label>Import Config File<input id='util_cfg_file' type='file' accept='.yaml,.yml,.txt'></label>
<button type='button' onclick='utilImportConfig()'>Import Config</button>
</div>
<small id='util_status'>Ready.</small>
</section>
</div>

<script>
let statusCache=null,presets={},contactsCache=[],selContact='',contactsFilter='';
let map=null,layer=null,leafletLoading=false;
let nodeNameDirty=false,locationDirty=false,wifiDirty=false,radioDirty=false,timezoneDirty=false;
const fallbackPresets={
  "US":{freq:910.525,bw:62.5,sf:7,cr:5,pwr:22},
  "EU_868":{freq:869.525,bw:62.5,sf:8,cr:5,pwr:22},
  "EU_433":{freq:433.500,bw:62.5,sf:8,cr:5,pwr:10},
  "ANZ":{freq:921.500,bw:62.5,sf:8,cr:5,pwr:22},
  "JP":{freq:922.000,bw:62.5,sf:8,cr:5,pwr:13},
  "KR":{freq:921.500,bw:62.5,sf:8,cr:5,pwr:22},
  "IN":{freq:866.000,bw:62.5,sf:8,cr:5,pwr:22},
  "TH":{freq:922.500,bw:62.5,sf:8,cr:5,pwr:16},
  "BR_902":{freq:904.750,bw:62.5,sf:8,cr:5,pwr:22}
};

function showTab(tab){['config','contacts','heatmap','utils'].forEach(t=>{const p=document.getElementById('tab_'+t),b=document.getElementById('tab_btn_'+t);if(p)p.classList.toggle('active',t===tab);if(b)b.classList.toggle('active',t===tab);});if(tab==='heatmap'){setTimeout(drawMap,10);}}
async function jget(u){const r=await fetch(u);return r.json();}
async function jpost(u,b){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b)});return r.json();}
function tzOffsetMinutes(tz){try{const p=new Intl.DateTimeFormat('en-US',{timeZone:tz,timeZoneName:'longOffset'}).formatToParts(new Date());const v=(p.find(x=>x.type==='timeZoneName')||{}).value||'';const m=v.match(/([+-])(\d{1,2})(?::?(\d{2}))?/);if(m){const s=m[1]==='-'?-1:1,h=parseInt(m[2],10)||0,n=parseInt(m[3]||'0',10)||0;return s*(h*60+n);}}catch(_e){}return 0;}
function tzPosixFromOffsetMinutes(offsetMinutes){const posixValue=-Number(offsetMinutes||0);const neg=posixValue<0;const absMin=Math.abs(posixValue);const h=Math.floor(absMin/60);const m=absMin%60;return m===0?('UTC'+(neg?'-':'')+String(h)):('UTC'+(neg?'-':'')+String(h)+':'+String(m).padStart(2,'0'));}
function validCoord(lat,lon){return Number.isFinite(lat)&&Number.isFinite(lon)&&lat>=-90&&lat<=90&&lon>=-180&&lon<=180&&!(lat===0&&lon===0);}
function bindDirtyTracking(){
  const node=document.getElementById('node_name');
  if(node){node.addEventListener('input',()=>{nodeNameDirty=true;});node.addEventListener('change',()=>{nodeNameDirty=true;});}
  ['node_lat','node_lon'].forEach(id=>{const e=document.getElementById(id);if(e){e.addEventListener('input',()=>{locationDirty=true;});e.addEventListener('change',()=>{locationDirty=true;});}});
  const sendLoc=document.getElementById('send_loc_adv');
  if(sendLoc){sendLoc.addEventListener('change',()=>{locationDirty=true;});}
  ['wifi_ssid','wifi_pass'].forEach(id=>{const e=document.getElementById(id);if(e){e.addEventListener('input',()=>{wifiDirty=true;});e.addEventListener('change',()=>{wifiDirty=true;});}});
  ['region','freq','bw','sf','cr','pwr','adv_int_min','path_hash_mode','screen_timeout_sec','mesh_region','notifications_enabled'].forEach(id=>{const e=document.getElementById(id);if(e){e.addEventListener('input',()=>{radioDirty=true;});e.addEventListener('change',()=>{radioDirty=true;});}});
  const tz=document.getElementById('timezone');
  if(tz){tz.addEventListener('input',()=>{timezoneDirty=true;});tz.addEventListener('change',()=>{timezoneDirty=true;});}
}
function bindContactsFilter(){const el=document.getElementById('contacts_filter');if(!el)return;el.addEventListener('input',()=>{contactsFilter=String(el.value||'').trim().toLowerCase();renderContacts();});}
function applyRegionPreset(){const r=document.getElementById('region');if(!r)return;const d=(presets&&presets[r.value])||fallbackPresets[r.value];if(!d)return;document.getElementById('freq').value=d.freq;document.getElementById('bw').value=d.bw;document.getElementById('sf').value=d.sf;document.getElementById('cr').value=d.cr;document.getElementById('pwr').value=d.pwr;radioDirty=true;}
function bindRegionPresetUi(){const r=document.getElementById('region');if(r){r.onchange=applyRegionPreset;}const b=document.getElementById('apply_region_defaults');if(b){b.onclick=applyRegionPreset;}}

function ensureTimezoneOptions(selected){const el=document.getElementById('timezone');if(!el)return;let zones=[];if(typeof Intl!=='undefined'&&typeof Intl.supportedValuesOf==='function'){try{zones=Intl.supportedValuesOf('timeZone');}catch(_e){zones=[];}}if(!zones.length)zones=['UTC0'];if(!zones.includes('UTC0'))zones.unshift('UTC0');if(selected&&!zones.includes(selected))zones.unshift(selected);el.innerHTML='';zones.forEach(z=>{const o=document.createElement('option');o.value=z;o.textContent=z;if(z===selected)o.selected=true;el.appendChild(o);});}

function populatePresetRegions(regions,selected){const r=document.getElementById('region');if(!r)return;r.innerHTML='';(regions||[]).forEach(v=>{const o=document.createElement('option');o.value=v;o.textContent=v;if(v===selected)o.selected=true;r.appendChild(o);});}
async function loadPresets(){
  try{
    const p=await jget('/api/presets');
    presets=(p&&p.region_defaults)?p.region_defaults:fallbackPresets;
    const regions=(p&&Array.isArray(p.regions)&&p.regions.length)?p.regions:Object.keys(presets);
    populatePresetRegions(regions,p&&p.selected_region?p.selected_region:'US');
  }catch(_e){
    presets=fallbackPresets;
    populatePresetRegions(Object.keys(fallbackPresets),'US');
  }
  bindRegionPresetUi();
}

async function loadStatus(force=false){
  const s=await jget('/api/status');
  statusCache=s;
  document.getElementById('meta').textContent='Mode: '+(s.mode||'?')+' | IP: '+(s.ip||'?');

  const nodeEl=document.getElementById('node_name');
  const nodeFocused=(nodeEl&&document.activeElement===nodeEl);
  if(nodeEl&&(force||(!nodeNameDirty&&!nodeFocused))){nodeEl.value=s.node_name||'';}

  const pkEl=document.getElementById('public_key');
  if(pkEl){pkEl.value=s.public_key||'';}

  const wifiSsid=document.getElementById('wifi_ssid');
  const wifiPass=document.getElementById('wifi_pass');
  const wifiFocused=(document.activeElement===wifiSsid||document.activeElement===wifiPass);
  if(force||(!wifiDirty&&!wifiFocused)){
    if(wifiSsid)wifiSsid.value=s.wifi_ssid||'';
    if(wifiPass)wifiPass.value=s.wifi_pass||'';
  }

  const regionEl=document.getElementById('region');
  const freqEl=document.getElementById('freq');
  const bwEl=document.getElementById('bw');
  const sfEl=document.getElementById('sf');
  const crEl=document.getElementById('cr');
  const pwrEl=document.getElementById('pwr');
  const advEl=document.getElementById('adv_int_min');
  const pathHashEl=document.getElementById('path_hash_mode');
  const timeoutEl=document.getElementById('screen_timeout_sec');
  const meshRegionEl=document.getElementById('mesh_region');
  const radioFocused=(document.activeElement===regionEl||document.activeElement===freqEl||document.activeElement===bwEl||document.activeElement===sfEl||document.activeElement===crEl||document.activeElement===pwrEl||document.activeElement===advEl||document.activeElement===pathHashEl||document.activeElement===timeoutEl||document.activeElement===meshRegionEl);
  if(force||(!radioDirty&&!radioFocused)){
    if(regionEl)regionEl.value=s.region||'US';
    if(freqEl)freqEl.value=s.freq;
    if(bwEl)bwEl.value=s.bw;
    if(sfEl)sfEl.value=s.sf;
    if(crEl)crEl.value=s.cr;
    if(pwrEl)pwrEl.value=s.pwr;
    if(advEl)advEl.value=s.adv_int_min||360;
    if(pathHashEl){
      const mode=(typeof s.path_hash_mode==='number'&&s.path_hash_mode>=0&&s.path_hash_mode<=2)?s.path_hash_mode:0;
      pathHashEl.value=String(mode);
    }
    if(timeoutEl)timeoutEl.value=s.screen_timeout_sec||30;
    if(meshRegionEl)meshRegionEl.value=s.mesh_region||'';
    const multiAckEl=document.getElementById('multi_ack');if(multiAckEl)multiAckEl.checked=!!s.multi_ack;
    const repEl=document.getElementById('repeater_mode');if(repEl)repEl.checked=!!s.repeater_mode;
    const notifEl=document.getElementById('notifications_enabled');if(notifEl)notifEl.checked=(typeof s.notifications_enabled==='boolean')?s.notifications_enabled:true;
  }

  const latEl=document.getElementById('node_lat');
  const lonEl=document.getElementById('node_lon');
  const sendLoc=document.getElementById('send_loc_adv');
  const locFocused=(document.activeElement===latEl||document.activeElement===lonEl);
  if(force||(!locationDirty&&!locFocused)){
    if(latEl)latEl.value=s.node_lat;
    if(lonEl)lonEl.value=s.node_lon;
    if(sendLoc)sendLoc.checked=!!s.send_loc_adv;
  }

  const tzEl=document.getElementById('timezone');
  const tzFocused=(document.activeElement===tzEl);
  if(force||(!timezoneDirty&&!tzFocused)){
    ensureTimezoneOptions(s.timezone||'UTC0');
  }
}

async function loadChannels(){const c=await jget('/api/channels');const ul=document.getElementById('channels');ul.innerHTML='';(c.channels||[]).forEach(n=>{const li=document.createElement('li');li.textContent=n;if(n!=='Public'){const b=document.createElement('button');b.className='mini';b.textContent='Remove';b.onclick=async()=>{await jpost('/api/channels/remove',{name:n});await loadChannels();};li.appendChild(b);}ul.appendChild(li);});}

async function copyTextToClipboard(text){if(!text)return false;try{if(navigator.clipboard&&navigator.clipboard.writeText){await navigator.clipboard.writeText(text);return true;}}catch(_e){}try{const ta=document.createElement('textarea');ta.value=text;ta.setAttribute('readonly','readonly');ta.style.position='fixed';ta.style.opacity='0';ta.style.left='-1000px';document.body.appendChild(ta);ta.focus();ta.select();const ok=document.execCommand('copy');document.body.removeChild(ta);return !!ok;}catch(_e){return false;}}

function ensureLeaflet(){return new Promise(resolve=>{if(window.L){resolve(true);return;}if(leafletLoading){setTimeout(()=>ensureLeaflet().then(resolve),120);return;}leafletLoading=true;if(!document.getElementById('leaflet_css')){const l=document.createElement('link');l.id='leaflet_css';l.rel='stylesheet';l.href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css';document.head.appendChild(l);}if(!document.getElementById('leaflet_js')){const s=document.createElement('script');s.id='leaflet_js';s.src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js';s.onload=()=>{leafletLoading=false;resolve(!!window.L);};s.onerror=()=>{leafletLoading=false;resolve(false);};document.head.appendChild(s);}else{const w=()=>{if(window.L){leafletLoading=false;resolve(true);}else setTimeout(w,120);};w();}});}

async function drawMap(){const panel=document.getElementById('tab_heatmap');if(!panel||!panel.classList.contains('active'))return;const meta=document.getElementById('map_meta');const ok=await ensureLeaflet();if(!ok){if(meta)meta.textContent='Map tiles unavailable (offline).';return;}if(!map){map=L.map('map',{zoomControl:true,attributionControl:true});L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'&copy; OpenStreetMap contributors'}).addTo(map);layer=L.layerGroup().addTo(map);}layer.clearLayers();const pts=[];contactsCache.forEach(c=>{if(c.ignored)return;const lat=Number(c.gps_lat),lon=Number(c.gps_lon);if(validCoord(lat,lon))pts.push({lat,lon,name:c.name||'(unnamed)',key:c.pubkey,self:false,lastmod:Number(c.lastmod||0)});});if(statusCache){const lat=Number(statusCache.node_lat),lon=Number(statusCache.node_lon);if(validCoord(lat,lon))pts.push({lat,lon,name:statusCache.node_name||'Me',key:'__self__',self:true,lastmod:0});}const nowSec=Math.floor(Date.now()/1000);pts.forEach(p=>{const age=(p.lastmod>0&&p.lastmod<4100000000)?Math.max(0,nowSec-p.lastmod):0;const intensity=p.self?1:Math.max(0.25,1-(age/(24*3600)));L.circleMarker([p.lat,p.lon],{radius:p.self?18:14,stroke:false,fillColor:p.self?'#59e4a7':'#ff9f43',fillOpacity:p.self?0.30:(0.10*intensity)}).addTo(layer);const m=L.circleMarker([p.lat,p.lon],{radius:p.self?7:6,color:p.self?'#59e4a7':'#ffc078',weight:2,fillOpacity:0.95}).addTo(layer);m.bindTooltip(p.self?'ME':p.name,{permanent:false});m.on('click',()=>{if(!p.self){selContact=p.key;renderContacts();}});});if(pts.length){map.setView([pts[0].lat,pts[0].lon],pts[0].self?13:8);}else{map.setView([0,0],2);}if(meta)meta.textContent=pts.length?('Points: '+pts.length):'No points yet.';setTimeout(()=>map.invalidateSize(false),0);}

function renderContacts(){const ul=document.getElementById('contacts');const meta=document.getElementById('contacts_meta');const empty=document.getElementById('contacts_empty');if(!ul)return;ul.innerHTML='';const source=Array.isArray(contactsCache)?contactsCache:[];const filter=contactsFilter;const visible=filter.length?source.filter(c=>String(c.name||'').toLowerCase().includes(filter)):source;if(meta)meta.textContent=visible.length+' shown / '+source.length+' total';if(!visible.length){if(empty){empty.style.display='block';empty.textContent=source.length?'No contacts match the current filter.':'No contacts heard yet.';}drawMap();return;}if(empty)empty.style.display='none';visible.forEach(c=>{const li=document.createElement('li');if(selContact&&selContact===c.pubkey){li.classList.add('active');}li.onclick=(ev)=>{if(ev.target&&ev.target.tagName==='BUTTON')return;selContact=c.pubkey;renderContacts();drawMap();};const top=document.createElement('div');top.className='card-top';const name=document.createElement('div');name.className='contact-name';name.textContent=c.name||'(unnamed)';const lat=Number(c.gps_lat),lon=Number(c.gps_lon);const hasLoc=validCoord(lat,lon);const metaLine=document.createElement('div');metaLine.className='contact-meta';metaLine.textContent='Last heard: '+String(c.lastmod||0)+(hasLoc?(' | GPS: '+lat.toFixed(5)+', '+lon.toFixed(5)):' | GPS: unavailable');const keyRow=document.createElement('div');keyRow.className='key-row';const keyText=document.createElement('div');keyText.className='key-text';keyText.textContent=c.pubkey||'';const copy=document.createElement('button');copy.className='mini';copy.type='button';copy.textContent='Copy';copy.onclick=async()=>{const ok=await copyTextToClipboard(c.pubkey||'');copy.textContent=ok?'Copied':'Copy failed';setTimeout(()=>{copy.textContent='Copy';},900);};keyRow.appendChild(keyText);keyRow.appendChild(copy);top.appendChild(name);top.appendChild(metaLine);top.appendChild(keyRow);const actions=document.createElement('div');actions.className='actions';const f=document.createElement('button');f.className='mini';f.type='button';f.textContent=c.favorite?'Unfavorite':'Favorite';f.onclick=async()=>{await jpost('/api/contacts/favorite',{pubkey:c.pubkey,favorite:c.favorite?'0':'1'});await loadContacts();};const d=document.createElement('button');d.className='mini';d.type='button';d.textContent='Delete';d.onclick=async()=>{if(!confirm('Delete contact?'))return;await jpost('/api/contacts/remove',{pubkey:c.pubkey});await loadContacts();};actions.appendChild(f);actions.appendChild(d);li.appendChild(top);li.appendChild(actions);ul.appendChild(li);});if(!selContact&&visible.length){selContact=visible[0].pubkey;}drawMap();}

async function loadContacts(){const c=await jget('/api/contacts');contactsCache=Array.isArray(c.contacts)?c.contacts:[];contactsCache.sort((a,b)=>{if(!!a.favorite!==!!b.favorite)return a.favorite?-1:1;return Number(b.lastmod||0)-Number(a.lastmod||0);});renderContacts();}

async function addChannel(){const name=(document.getElementById('ch_name').value||'').trim();const psk=(document.getElementById('ch_psk').value||'').trim();if(!name){alert('Channel name is required');return;}if(name[0]!=='#'&&!psk){alert('PSK is required for non-# channels');return;}let r=null;try{r=await jpost('/api/channels/add',{name,psk});}catch(e){alert('Add channel request failed: '+(e&&e.message?e.message:String(e)));return;}if(!r||!r.ok){alert((r&&r.error)||'failed');return;}document.getElementById('ch_name').value='';document.getElementById('ch_psk').value='';await loadChannels();}

async function saveAll(){const tz=document.getElementById('timezone').value;const tzOffset=tzOffsetMinutes(tz);const tzPosix=tzPosixFromOffsetMinutes(tzOffset);let r=null;try{r=await jpost('/api/save',{node_name:document.getElementById('node_name').value,node_lat:document.getElementById('node_lat').value,node_lon:document.getElementById('node_lon').value,send_loc_adv:document.getElementById('send_loc_adv').checked?'1':'0',ssid:document.getElementById('wifi_ssid').value,pass:document.getElementById('wifi_pass').value,timezone:tz,timezone_posix:tzPosix,tz_offset:String(tzOffset),region:document.getElementById('region').value,freq:document.getElementById('freq').value,bw:document.getElementById('bw').value,sf:document.getElementById('sf').value,cr:document.getElementById('cr').value,pwr:document.getElementById('pwr').value,adv_int_min:document.getElementById('adv_int_min').value,path_hash_mode:document.getElementById('path_hash_mode').value,multi_ack:document.getElementById('multi_ack').checked?'1':'0',repeater_mode:document.getElementById('repeater_mode').checked?'1':'0',notifications_enabled:document.getElementById('notifications_enabled').checked?'1':'0',screen_timeout_sec:document.getElementById('screen_timeout_sec').value,mesh_region:document.getElementById('mesh_region').value});}catch(e){alert('Save request failed: '+(e&&e.message?e.message:String(e)));return;}alert((r&&r.message)||((r&&r.error)||'done'));if(r&&r.ok){nodeNameDirty=false;locationDirty=false;wifiDirty=false;radioDirty=false;timezoneDirty=false;await loadStatus(true);}}

async function utilAdvertLocal(){const r=await jpost('/api/util/advert/local',{});alert((r&&r.message)||((r&&r.error)||'done'));}
async function utilAdvertFlood(){const r=await jpost('/api/util/advert/flood',{});alert((r&&r.message)||((r&&r.error)||'done'));}
function utilExportConfig(){window.location='/api/util/export';}
async function utilImportConfig(){const status=document.getElementById('util_status');const input=document.getElementById('util_cfg_file');if(!input||!input.files||input.files.length===0){if(status)status.textContent='Choose a config file first.';return;}const file=input.files[0];const text=await file.text();const r=await jpost('/api/util/import',{content:text});if(status)status.textContent=(r&&r.message)?r.message:((r&&r.error)||'done');}

async function boot(){try{await loadPresets();bindDirtyTracking();bindContactsFilter();await loadStatus(true);await loadChannels();await loadContacts();setInterval(()=>{loadStatus(false);loadContacts();},5000);}catch(e){document.getElementById('meta').textContent='UI init failed: '+e;}}
boot();
</script>
</div></body></html>
)HTML";

  g_server.send_P(200, "text/html", kRootPage);
  return;

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
      ".map-wrap{position:relative;height:300px;border:1px solid #2a435d;border-radius:8px;overflow:hidden;background:linear-gradient(180deg,#12263a,#0b192a);}"
      ".map-wrap canvas{display:block;width:100%;height:100%;}"
      ".map-wrap .map-host{width:100%;height:100%;}"
      ".map-toolbar{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:0 0 8px;font-size:.76rem;}"
      ".map-toolbar button{width:auto;min-width:44px;margin-top:0;padding:4px 8px;font-size:.74rem;min-height:26px;}"
      ".contacts-layout{display:grid;grid-template-columns:1.1fr .9fr;gap:10px;}"
      ".contacts-list{max-height:280px;overflow:auto;padding-left:0;list-style:none;margin:0;font-size:.78rem;}"
      ".contacts-list li{margin:0 0 6px;display:flex;gap:4px;align-items:center;}"
      ".contacts-list button{margin-top:0;}"
      ".contacts-list .pick{flex:1;text-align:left;background:#15293d;border-color:#355674;font-weight:600;font-size:.78rem;padding:5px 6px;min-height:28px;}"
      ".contacts-list .pick.active{background:#2c9bc8;border-color:#2c9bc8;}"
      ".contacts-list .mini{width:auto;min-width:62px;padding:4px 6px;font-size:.74rem;min-height:28px;background:#1d334a;border-color:#355674;}"
      ".contact-detail{border:1px solid #2a435d;border-radius:8px;padding:8px;min-height:280px;background:#0f1a28;font-size:.76rem;}"
      ".contact-detail .kv{margin:0 0 4px;word-break:break-word;}"
      "@media(max-width:760px){.contacts-layout{grid-template-columns:1fr;}.contact-detail{min-height:0;}}"
      "</style></head><body><div class='wrap'>"
      "<h1>Plumeria Web Config</h1>"
      "<div class='tabs'><button type='button' id='tab_btn_config' class='tabbtn active' onclick='showTab(\"config\")'>Configuration</button>"
      "<button type='button' id='tab_btn_contacts' class='tabbtn' onclick='showTab(\"contacts\")'>Contacts</button>"
      "<button type='button' id='tab_btn_utils' class='tabbtn' onclick='showTab(\"utils\")'>Utilities</button></div>"
      "<div id='tab_config' class='tab-panel active'>"
      "<section><h3>Identity</h3>"
      "<label>Node Name<input id='node_name' maxlength='31' value=''></label>"
      "<label>Public Key</label><div class='copy-row'><input id='public_key' readonly value=''><button type='button' id='copy_pubkey' onclick='copyPublicKey()'>Copy</button></div>"
      "<small>Used for node adverts and sender name in channel chats.</small>"
      "</section>"
      "<section><h3>Radio Configuration</h3>"
      "<div class='row'><label>Region<select id='region' onchange='applyRegionPreset();markRadioDirty();'>__REGION_OPTIONS__</select></label>"
      "<label>&nbsp;<button type='button' onclick='applyRegionPreset()' style='margin-top:22px'>Apply Region Defaults</button></label></div>"
      "<div class='row'><label>Frequency MHz<input id='freq' type='number' step='0.001'></label>"
      "<label>Bandwidth kHz<input id='bw' type='number' step='0.1'></label></div>"
      "<div class='row'><label>Spreading Factor<input id='sf' type='number' min='5' max='12' step='1'></label>"
      "<label>Coding Rate (4/x)<input id='cr' type='number' min='5' max='8' step='1'></label></div>"
      "<label>TX Power dBm<input id='pwr' type='number' min='1' max='30' step='1'></label>"
      "<label>Advert Interval Minutes<input id='adv_int_min' type='number' min='60' step='1'></label>"
      "<small>Automatic adverts send both zero-hop and flood at this interval (minimum 60 minutes).</small>"
      "<small>This firmware uses explicit LoRa values (freq, bw, sf, cr, tx power), not modem profile names.</small>"
      "</section>"
      "<section><h3>Channels</h3><div class='row'>"
      "<label>Name<input id='ch_name' placeholder='Public, #SomeChannel'></label>"
      "<label>PSK Base64 (optional for #channels)<input id='ch_psk' placeholder='izOH6cXN6mrJ5e26oRXNcg=='></label>"
      "</div><button onclick='addChannel()'>Add Channel</button>"
      "<small>#channels derive encryption key from SHA-256(channel name); PSK is ignored for #channels.</small>"
      "<ul id='channels'></ul></section>"
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
      "<div id='tab_contacts' class='tab-panel'>"
      "<section><h3>Contacts Heat Map</h3>"
      "<div class='map-toolbar'>"
      "<button type='button' onclick='zoomContactsMap(1.25)'>+</button>"
      "<button type='button' onclick='zoomContactsMap(0.8)'>-</button>"
      "<button type='button' onclick='focusMapOnMe()'>ME</button>"
      "<small id='contacts_map_meta'>No map points yet.</small></div>"
      "<div class='map-wrap'><canvas id='contacts_map'></canvas></div>"
      "<small>Heat points include heard contacts with location and your node location.</small>"
      "</section>"
      "<section><h3>Contacts</h3>"
      "<small>Click a contact to inspect telemetry. Favorites are protected from oldest-contact overwrite.</small>"
      "<div class='contacts-layout'>"
      "<ul id='contacts' class='contacts-list'></ul>"
      "<div id='contact_detail' class='contact-detail'>Select a contact to view telemetry.</div>"
      "</div></section>"
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
      "const regionDefaultsBootstrap=__REGION_DEFAULTS__;let regionDefaults=regionDefaultsBootstrap;let radioDirty=false;let timezoneDirty=false;let nodeNameDirty=false;let locationDirty=false;let wifiDirty=false;let channelMutationInFlight=false;let contactsCache=[];let selectedContactKey='';let statusCache=null;let contactsMapView={centerLat:0,centerLon:0,zoom:1};let contactsMapCenterSet=false;let contactsLeafletMap=null;let contactsLeafletLayer=null;let contactsLeafletLoading=false;let contactsMapHostId='contacts_map_leaflet';"
      "function showTab(tab){const cfg=document.getElementById('tab_config');const contacts=document.getElementById('tab_contacts');const util=document.getElementById('tab_utils');const bCfg=document.getElementById('tab_btn_config');const bContacts=document.getElementById('tab_btn_contacts');const bUtil=document.getElementById('tab_btn_utils');if(!cfg||!contacts||!util||!bCfg||!bContacts||!bUtil)return;const configOn=tab==='config';const contactsOn=tab==='contacts';const utilsOn=tab==='utils';cfg.classList.toggle('active',configOn);contacts.classList.toggle('active',contactsOn);util.classList.toggle('active',utilsOn);bCfg.classList.toggle('active',configOn);bContacts.classList.toggle('active',contactsOn);bUtil.classList.toggle('active',utilsOn);if(contactsOn){setTimeout(()=>{drawContactsMap();},0);}}"
      "function ensureTimezoneOptions(selected){const tzSel=document.getElementById('timezone');if(!tzSel)return;const current=tzSel.value;const hasOptions=tzSel.options&&tzSel.options.length>0;if(hasOptions){if(selected&&selected.length>0){let found=false;for(let i=0;i<tzSel.options.length;i++){if(tzSel.options[i].value===selected){found=true;break;}}if(!found){const opt=document.createElement('option');opt.value=selected;opt.textContent=selected;tzSel.insertBefore(opt,tzSel.firstChild);}if(current!==selected){tzSel.value=selected;}}return;}let zones=[];if(typeof Intl!=='undefined'&&typeof Intl.supportedValuesOf==='function'){try{zones=Intl.supportedValuesOf('timeZone');}catch(_e){zones=[];}}if(!Array.isArray(zones)||zones.length===0){zones=['UTC0'];}if(!zones.includes('UTC0')){zones.unshift('UTC0');}if(selected&&selected.length>0&&!zones.includes(selected)){zones.unshift(selected);}tzSel.innerHTML='';zones.forEach(z=>{const opt=document.createElement('option');opt.value=z;opt.textContent=z;if(z===selected)opt.selected=true;tzSel.appendChild(opt);});if(!selected&&tzSel.options.length>0){tzSel.selectedIndex=0;}}"
      "function markRadioDirty(){radioDirty=true;}"
      "function markTimezoneDirty(){timezoneDirty=true;}"
      "function refreshGpsModeIndicator(){const gpsMode=document.getElementById('gps_mode');const gpsLabel=document.getElementById('gps_mode_label');if(!gpsMode)return;if(gpsLabel)gpsLabel.textContent=gpsMode.checked?'GPS ON':'GPS OFF, using default location.';}"
      "function bindNodeNameInput(){const el=document.getElementById('node_name');if(!el)return;el.addEventListener('input',()=>{nodeNameDirty=true;});el.addEventListener('change',()=>{nodeNameDirty=true;});}"
      "function bindLocationInputs(){['node_lat','node_lon'].forEach(id=>{const el=document.getElementById(id);if(!el)return;el.addEventListener('input',()=>{locationDirty=true;});el.addEventListener('change',()=>{locationDirty=true;});});const cb=document.getElementById('send_loc_adv');if(cb){cb.addEventListener('change',()=>{locationDirty=true;});}const gpsMode=document.getElementById('gps_mode');if(gpsMode){gpsMode.addEventListener('change',()=>{locationDirty=true;refreshGpsModeIndicator();});}refreshGpsModeIndicator();}"
      "function bindWifiInputs(){['wifi_ssid','wifi_pass'].forEach(id=>{const el=document.getElementById(id);if(!el)return;el.addEventListener('input',()=>{wifiDirty=true;});el.addEventListener('change',()=>{wifiDirty=true;});});}"
      "function bindRadioInputs(){['region','freq','bw','sf','cr','pwr','adv_int_min'].forEach(id=>{const el=document.getElementById(id);if(!el)return;el.addEventListener('input',markRadioDirty);el.addEventListener('change',markRadioDirty);});}"
      "function bindTimezoneInput(){const el=document.getElementById('timezone');if(!el)return;el.addEventListener('input',markTimezoneDirty);el.addEventListener('change',markTimezoneDirty);}"
      "async function copyPublicKey(){const el=document.getElementById('public_key');if(!el||!el.value){alert('No public key');return;}try{if(navigator.clipboard&&navigator.clipboard.writeText){await navigator.clipboard.writeText(el.value);}else{el.focus();el.select();document.execCommand('copy');}alert('Public key copied');}catch(_e){el.focus();el.select();document.execCommand('copy');alert('Public key copied');}}"
      "function calcTimezoneOffsetMinutes(tz){try{const parts=new Intl.DateTimeFormat('en-US',{timeZone:tz,timeZoneName:'longOffset'}).formatToParts(new Date());const zone=(parts.find(p=>p.type==='timeZoneName')||{}).value||'';const m=zone.match(/([+-])(\\d{1,2})(?::?(\\d{2}))?/);if(m){const sign=m[1]==='-'?-1:1;const hh=parseInt(m[2],10)||0;const mm=parseInt(m[3]||'0',10)||0;return sign*(hh*60+mm);}}catch(_e){}return 0;}"
      "async function jget(u){const r=await fetch(u);return r.json();}"
      "async function jpost(u,b){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b)});return r.json();}"
      "function hasValidCoord(lat,lon){return Number.isFinite(lat)&&Number.isFinite(lon)&&lat>=-90&&lat<=90&&lon>=-180&&lon<=180&&!(lat===0&&lon===0);}"
      "function clamp(v,min,max){return Math.max(min,Math.min(max,v));}"
      "function contactDisplayName(ct){return (ct&&ct.name&&ct.name.length>0)?ct.name:'(unnamed)';}"
      "function formatLastHeard(lastmod){const v=Number(lastmod||0);if(!Number.isFinite(v)||v<=0)return 'unknown';if(v>1000000000&&v<4100000000){try{return new Date(v*1000).toLocaleString();}catch(_e){return String(v);}}return String(v);}"
      "function getMapPoints(){const pts=[];contactsCache.forEach(ct=>{const lat=Number(ct.gps_lat);const lon=Number(ct.gps_lon);if(!hasValidCoord(lat,lon))return;pts.push({lat,lon,label:contactDisplayName(ct),key:ct.pubkey,isSelf:false,lastmod:Number(ct.lastmod||0),favorite:!!ct.favorite});});if(statusCache){const selfLat=Number(statusCache.node_lat);const selfLon=Number(statusCache.node_lon);if(hasValidCoord(selfLat,selfLon)){pts.push({lat:selfLat,lon:selfLon,label:(statusCache.node_name&&statusCache.node_name.length>0)?statusCache.node_name:'Me',key:'__self__',isSelf:true,lastmod:0,favorite:true});}}return pts;}"
      "function projectMapPoint(lat,lon,w,h){const zoom=clamp(Number(contactsMapView.zoom||1),0.4,8);const latSpan=clamp(140/zoom,14,180);const lonSpan=clamp(220/zoom,20,360);const centerLat=clamp(Number(contactsMapView.centerLat||0),-90,90);let centerLon=Number(contactsMapView.centerLon||0);while(centerLon>180)centerLon-=360;while(centerLon<-180)centerLon+=360;let dlon=lon-centerLon;while(dlon>180)dlon-=360;while(dlon<-180)dlon+=360;const dlat=centerLat-lat;if(Math.abs(dlon)>lonSpan/2||Math.abs(dlat)>latSpan/2)return null;const x=(w/2)+(dlon/(lonSpan/2))*(w/2);const y=(h/2)+(dlat/(latSpan/2))*(h/2);return {x,y};}"
      "function drawContactsMap(){const canvas=document.getElementById('contacts_map');const meta=document.getElementById('contacts_map_meta');if(!canvas)return;const rect=canvas.getBoundingClientRect();const w=Math.max(220,Math.floor(rect.width||220));const h=Math.max(180,Math.floor(rect.height||300));const dpr=Math.max(1,Math.min(2,window.devicePixelRatio||1));if(canvas.width!==Math.floor(w*dpr)||canvas.height!==Math.floor(h*dpr)){canvas.width=Math.floor(w*dpr);canvas.height=Math.floor(h*dpr);}const ctx=canvas.getContext('2d');if(!ctx)return;ctx.setTransform(1,0,0,1,0,0);ctx.scale(dpr,dpr);const bg=ctx.createLinearGradient(0,0,0,h);bg.addColorStop(0,'#142c42');bg.addColorStop(1,'#0a1523');ctx.fillStyle=bg;ctx.fillRect(0,0,w,h);ctx.strokeStyle='rgba(110,154,194,.24)';ctx.lineWidth=1;for(let i=1;i<6;i++){const gx=Math.round((w*i)/6)+0.5;ctx.beginPath();ctx.moveTo(gx,0);ctx.lineTo(gx,h);ctx.stroke();}for(let i=1;i<4;i++){const gy=Math.round((h*i)/4)+0.5;ctx.beginPath();ctx.moveTo(0,gy);ctx.lineTo(w,gy);ctx.stroke();}const pts=getMapPoints();if(pts.length>0&&!contactsMapCenterSet){contactsMapView.centerLat=pts[0].lat;contactsMapView.centerLon=pts[0].lon;contactsMapCenterSet=true;}const nowSec=Math.floor(Date.now()/1000);pts.forEach(pt=>{const p=projectMapPoint(pt.lat,pt.lon,w,h);if(!p)return;const ageSec=(pt.lastmod>0&&pt.lastmod<4100000000)?Math.max(0,nowSec-pt.lastmod):0;const decay=pt.isSelf?1:clamp(1-(ageSec/(24*3600)),0.25,1);const radius=pt.isSelf?22:16;const heat=ctx.createRadialGradient(p.x,p.y,1,p.x,p.y,radius);heat.addColorStop(0,pt.isSelf?'rgba(82,224,160,'+(0.55*decay)+')':'rgba(255,166,78,'+(0.50*decay)+')');heat.addColorStop(1,'rgba(0,0,0,0)');ctx.fillStyle=heat;ctx.beginPath();ctx.arc(p.x,p.y,radius,0,Math.PI*2);ctx.fill();});pts.forEach(pt=>{const p=projectMapPoint(pt.lat,pt.lon,w,h);if(!p)return;const selected=selectedContactKey&&selectedContactKey===pt.key;ctx.fillStyle=pt.isSelf?'#59e4a7':'#ffc078';ctx.strokeStyle=selected?'#ffffff':'rgba(20,34,50,.8)';ctx.lineWidth=selected?3:2;ctx.beginPath();ctx.arc(p.x,p.y,pt.isSelf?6:5,0,Math.PI*2);ctx.fill();ctx.stroke();if(selected||pt.isSelf){ctx.fillStyle='#e7eef6';ctx.font='12px system-ui, Segoe UI, Arial, sans-serif';ctx.fillText(pt.isSelf?'ME':(pt.label||'Contact'),p.x+8,p.y-8);}});if(meta){meta.textContent=pts.length>0?('Points: '+pts.length+' | Zoom: '+(Number(contactsMapView.zoom||1).toFixed(2))):'No map points yet.';}}"
      "function zoomContactsMap(factor){const next=clamp(Number(contactsMapView.zoom||1)*Number(factor||1),0.4,8);contactsMapView.zoom=next;drawContactsMap();}"
      "function focusMapOnMe(){if(!statusCache)return;const lat=Number(statusCache.node_lat);const lon=Number(statusCache.node_lon);if(!hasValidCoord(lat,lon)){alert('Your node has no valid location set');return;}contactsMapView.centerLat=lat;contactsMapView.centerLon=lon;contactsMapView.zoom=Math.max(contactsMapView.zoom||1,1.2);contactsMapCenterSet=true;drawContactsMap();}"
      "function selectContact(pubkey){selectedContactKey=pubkey||'';renderContactsList();renderContactDetail();drawContactsMap();}"
      "function renderContactDetail(){const detail=document.getElementById('contact_detail');if(!detail)return;if(!selectedContactKey){detail.textContent='Select a contact to view telemetry.';return;}const ct=contactsCache.find(c=>c.pubkey===selectedContactKey);if(!ct){detail.textContent='Selected contact is no longer available.';return;}const lat=Number(ct.gps_lat);const lon=Number(ct.gps_lon);const hasLoc=hasValidCoord(lat,lon);let html='';html+='<div class=\"kv\"><strong>Name:</strong> '+contactDisplayName(ct)+'</div>';html+='<div class=\"kv\"><strong>Public Key:</strong> '+(ct.pubkey||'')+'</div>';html+='<div class=\"kv\"><strong>Favorite:</strong> '+(ct.favorite?'Yes':'No')+'</div>';html+='<div class=\"kv\"><strong>Type:</strong> '+String((ct.type===undefined||ct.type===null)?'':ct.type)+'</div>';html+='<div class=\"kv\"><strong>Last Heard:</strong> '+formatLastHeard(ct.lastmod)+'</div>';html+='<div class=\"kv\"><strong>GPS (int):</strong> '+String(ct.gps_lat_i||0)+', '+String(ct.gps_lon_i||0)+'</div>';html+='<div class=\"kv\"><strong>GPS (deg):</strong> '+(hasLoc?(lat.toFixed(6)+', '+lon.toFixed(6)):'Unavailable')+'</div>';detail.innerHTML=html;}"
      "function renderContactsList(){const ul=document.getElementById('contacts');if(!ul)return;ul.innerHTML='';if(!Array.isArray(contactsCache)||contactsCache.length===0){const li=document.createElement('li');li.textContent='No contacts heard yet';ul.appendChild(li);renderContactDetail();drawContactsMap();return;}contactsCache.forEach(ct=>{const li=document.createElement('li');const pick=document.createElement('button');pick.type='button';pick.className='pick'+((selectedContactKey&&selectedContactKey===ct.pubkey)?' active':'');pick.textContent=contactDisplayName(ct);pick.onclick=()=>selectContact(ct.pubkey);const fav=document.createElement('button');fav.type='button';fav.className='mini';fav.textContent=ct.favorite?'Unfavorite':'Favorite';fav.onclick=async()=>{const r=await jpost('/api/contacts/favorite',{pubkey:ct.pubkey,favorite:ct.favorite?'0':'1'});if(!r||!r.ok){alert((r&&r.error)||'failed');return;}await loadContacts();};const del=document.createElement('button');del.type='button';del.className='mini';del.textContent='Delete';del.onclick=async()=>{if(!confirm('Delete contact '+contactDisplayName(ct)+'?'))return;const r=await jpost('/api/contacts/remove',{pubkey:ct.pubkey});if(!r||!r.ok){alert((r&&r.error)||'failed');return;}await loadContacts();};li.appendChild(pick);li.appendChild(fav);li.appendChild(del);ul.appendChild(li);});renderContactDetail();drawContactsMap();}"
      "async function loadStatus(forceRadio=false){const s=await jget('/api/status');statusCache=s;const tz=s.timezone||'UTC0';const nodeEl=document.getElementById('node_name');const nodeFocused=(nodeEl&&document.activeElement===nodeEl);if(nodeEl&&(forceRadio||(!nodeNameDirty&&!nodeFocused))){nodeEl.value=s.node_name||'';}const pkEl=document.getElementById('public_key');if(pkEl&&s.public_key){pkEl.value=s.public_key;}if(forceRadio||!locationDirty){document.getElementById('node_lat').value=(typeof s.node_lat==='number')?s.node_lat:0;document.getElementById('node_lon').value=(typeof s.node_lon==='number')?s.node_lon:0;document.getElementById('send_loc_adv').checked=!!s.send_loc_adv;refreshGpsModeIndicator();}const ssidEl=document.getElementById('wifi_ssid');const passEl=document.getElementById('wifi_pass');const wifiFocused=(document.activeElement===ssidEl||document.activeElement===passEl);if(forceRadio||(!wifiDirty&&!wifiFocused)){if(ssidEl)ssidEl.value=s.wifi_ssid||'';if(passEl)passEl.value=s.wifi_pass||'';}const tzSel=document.getElementById('timezone');const tzNeedsInit=!(tzSel&&tzSel.options&&tzSel.options.length>0);const tzFocused=(tzSel&&document.activeElement===tzSel);if(tzNeedsInit||forceRadio||(!timezoneDirty&&!tzFocused)){ensureTimezoneOptions(tz);}if(forceRadio||!radioDirty){document.getElementById('region').value=s.region||'US';document.getElementById('freq').value=s.freq;document.getElementById('bw').value=s.bw;document.getElementById('sf').value=s.sf;document.getElementById('cr').value=s.cr;document.getElementById('pwr').value=s.pwr;document.getElementById('adv_int_min').value=s.adv_int_min||360;}if(forceRadio&&hasValidCoord(Number(s.node_lat),Number(s.node_lon))){contactsMapView.centerLat=Number(s.node_lat);contactsMapView.centerLon=Number(s.node_lon);contactsMapCenterSet=true;}drawContactsMap();}"
      "async function loadPresets(){try{const p=await jget('/api/presets');regionDefaults=(p.region_defaults&&Object.keys(p.region_defaults).length>0)?p.region_defaults:regionDefaultsBootstrap;const r=document.getElementById('region');if(Array.isArray(p.regions)&&p.regions.length>0){r.innerHTML='';p.regions.forEach(v=>{const o=document.createElement('option');o.value=v;o.textContent=v;if(v===p.selected_region)o.selected=true;r.appendChild(o);});}if(p.selected_region){r.value=p.selected_region;}}catch(_e){regionDefaults=regionDefaultsBootstrap;}}"
      "function applyRegionPreset(){const r=document.getElementById('region').value;const d=regionDefaults[r]||regionDefaultsBootstrap[r];if(!d)return;document.getElementById('freq').value=d.freq;document.getElementById('bw').value=d.bw;document.getElementById('sf').value=d.sf;document.getElementById('cr').value=d.cr;document.getElementById('pwr').value=d.pwr;radioDirty=true;}"
      "async function loadChannels(){const c=await jget('/api/channels');const ul=document.getElementById('channels');ul.innerHTML='';c.channels.forEach(n=>{const li=document.createElement('li');li.textContent=n;if(n==='Public'){const ro=document.createElement('small');ro.textContent=' (read-only)';li.appendChild(ro);}else{const b=document.createElement('button');b.textContent='Remove';b.style.width='auto';b.style.display='inline-block';b.style.marginLeft='8px';b.onclick=async()=>{await jpost('/api/channels/remove',{name:n});await loadChannels();};li.appendChild(b);}ul.appendChild(li);});}"
      "async function loadContacts(){const c=await jget('/api/contacts');contactsCache=Array.isArray(c.contacts)?c.contacts:[];contactsCache.sort((a,b)=>{if(!!a.favorite!==!!b.favorite)return a.favorite?-1:1;const am=Number(a.lastmod||0);const bm=Number(b.lastmod||0);if(am!==bm)return bm-am;const an=(a.name||'').toLowerCase();const bn=(b.name||'').toLowerCase();if(an<bn)return-1;if(an>bn)return 1;return 0;});if(selectedContactKey&&!contactsCache.some(ct=>ct.pubkey===selectedContactKey)){selectedContactKey='';}if(!selectedContactKey&&contactsCache.length>0){selectedContactKey=contactsCache[0].pubkey;}renderContactsList();}"
      "async function utilAdvertLocal(){const r=await jpost('/api/util/advert/local',{});alert((r&&r.message)||r.error||'done');}"
      "async function utilAdvertFlood(){const r=await jpost('/api/util/advert/flood',{});alert((r&&r.message)||r.error||'done');}"
      "function utilExportConfig(){window.location='/api/util/export';}"
      "async function utilImportConfig(){const status=document.getElementById('util_status');const input=document.getElementById('util_cfg_file');if(!input||!input.files||input.files.length===0){if(status)status.textContent='Choose a config file first.';return;}const file=input.files[0];const text=await file.text();const r=await jpost('/api/util/import',{content:text});if(status)status.textContent=(r&&r.message)?r.message:(r&&r.error)?r.error:'done';if(!r||!r.ok){alert((r&&r.error)||'failed');return;}alert(r.message||'Config imported');}"
      "async function saveAll(){if(channelMutationInFlight){alert('Channel update in progress, please wait');return;}const pendingName=(document.getElementById('ch_name').value||'').trim();const pendingPsk=(document.getElementById('ch_psk').value||'').trim();if(pendingName){if(pendingName[0]!=='#'&&!pendingPsk){alert('PSK is required for non-# channels');return;}let addRes=null;channelMutationInFlight=true;try{addRes=await jpost('/api/channels/add',{name:pendingName,psk:pendingPsk});}finally{channelMutationInFlight=false;}if(!addRes||!addRes.ok){alert((addRes&&addRes.error)||'failed to add channel');return;}document.getElementById('ch_name').value='';document.getElementById('ch_psk').value='';await loadChannels();}const tzVal=document.getElementById('timezone').value;const sendLoc=document.getElementById('send_loc_adv').checked?'1':'0';const r=await jpost('/api/save',{node_name:document.getElementById('node_name').value,node_lat:document.getElementById('node_lat').value,node_lon:document.getElementById('node_lon').value,send_loc_adv:sendLoc,ssid:document.getElementById('wifi_ssid').value,pass:document.getElementById('wifi_pass').value,timezone:tzVal,tz_offset:String(calcTimezoneOffsetMinutes(tzVal)),region:document.getElementById('region').value,freq:document.getElementById('freq').value,bw:document.getElementById('bw').value,sf:document.getElementById('sf').value,cr:document.getElementById('cr').value,pwr:document.getElementById('pwr').value,adv_int_min:document.getElementById('adv_int_min').value});alert(r.message||r.error||'done');if(r.ok){timezoneDirty=false;nodeNameDirty=false;locationDirty=false;wifiDirty=false;}}"
      "async function addChannel(){if(channelMutationInFlight){return;}const name=(document.getElementById('ch_name').value||'').trim();const psk=(document.getElementById('ch_psk').value||'').trim();if(!name){alert('Channel name is required');return;}if(name[0]!=='#'&&!psk){alert('PSK is required for non-# channels');return;}let r=null;channelMutationInFlight=true;try{r=await jpost('/api/channels/add',{name,psk});}finally{channelMutationInFlight=false;}if(!r||!r.ok){alert((r&&r.error)||'failed');return;}document.getElementById('ch_name').value='';document.getElementById('ch_psk').value='';await loadChannels();}"
      "function ensureContactsMapHost(){const wrap=document.querySelector('.map-wrap');if(!wrap)return null;let host=document.getElementById(contactsMapHostId);if(host)return host;wrap.innerHTML='<div id=\"'+contactsMapHostId+'\" class=\"map-host\"></div>';host=document.getElementById(contactsMapHostId);return host;}"
      "function ensureLeafletLoaded(){return new Promise(resolve=>{if(window.L){resolve(true);return;}if(contactsLeafletLoading){setTimeout(()=>{ensureLeafletLoaded().then(resolve);},120);return;}contactsLeafletLoading=true;if(!document.getElementById('leaflet_css')){const link=document.createElement('link');link.id='leaflet_css';link.rel='stylesheet';link.href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css';document.head.appendChild(link);}if(document.getElementById('leaflet_js')){const wait=()=>{if(window.L){contactsLeafletLoading=false;resolve(true);}else{setTimeout(wait,120);}};wait();return;}const script=document.createElement('script');script.id='leaflet_js';script.src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js';script.onload=()=>{contactsLeafletLoading=false;resolve(!!window.L);};script.onerror=()=>{contactsLeafletLoading=false;resolve(false);};document.head.appendChild(script);});}"
      "async function drawContactsMap(){const meta=document.getElementById('contacts_map_meta');const pts=getMapPoints();if(meta){meta.textContent=pts.length>0?('Points: '+pts.length):'No map points yet.';}const host=ensureContactsMapHost();if(!host)return;const ok=await ensureLeafletLoaded();if(!ok){if(meta)meta.textContent='Map tiles unavailable (offline).';return;}if(!contactsLeafletMap){contactsLeafletMap=L.map(host,{zoomControl:false,attributionControl:true});L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'&copy; OpenStreetMap contributors'}).addTo(contactsLeafletMap);contactsLeafletLayer=L.layerGroup().addTo(contactsLeafletMap);contactsLeafletMap.on('moveend zoomend',()=>{const c=contactsLeafletMap.getCenter();contactsMapView.centerLat=c.lat;contactsMapView.centerLon=c.lng;contactsMapView.zoom=contactsLeafletMap.getZoom();contactsMapCenterSet=true;});}if(!contactsMapCenterSet){const self=statusCache&&hasValidCoord(Number(statusCache.node_lat),Number(statusCache.node_lon));if(self){contactsMapView.centerLat=Number(statusCache.node_lat);contactsMapView.centerLon=Number(statusCache.node_lon);contactsMapView.zoom=13;}else if(pts.length>0){contactsMapView.centerLat=pts[0].lat;contactsMapView.centerLon=pts[0].lon;contactsMapView.zoom=8;}else{contactsMapView.centerLat=0;contactsMapView.centerLon=0;contactsMapView.zoom=2;}contactsMapCenterSet=true;}contactsLeafletMap.setView([contactsMapView.centerLat,contactsMapView.centerLon],Math.round(clamp(Number(contactsMapView.zoom||2),2,19)),{animate:false});if(!contactsLeafletLayer)return;contactsLeafletLayer.clearLayers();const nowSec=Math.floor(Date.now()/1000);pts.forEach(pt=>{const selected=selectedContactKey&&selectedContactKey===pt.key;const ageSec=(pt.lastmod>0&&pt.lastmod<4100000000)?Math.max(0,nowSec-pt.lastmod):0;const decay=pt.isSelf?1:clamp(1-(ageSec/(24*3600)),0.25,1);const heatColor=pt.isSelf?'#52e0a0':'#ffa64e';const pointColor=pt.isSelf?'#59e4a7':'#ffc078';L.circle([pt.lat,pt.lon],{radius:pt.isSelf?260:180,stroke:false,fillColor:heatColor,fillOpacity:(pt.isSelf?0.38:0.30)*decay}).addTo(contactsLeafletLayer);const m=L.circleMarker([pt.lat,pt.lon],{radius:pt.isSelf?7:6,color:selected?'#ffffff':'#142232',weight:selected?3:2,fillColor:pointColor,fillOpacity:1}).addTo(contactsLeafletLayer);if(selected||pt.isSelf){m.bindTooltip(pt.isSelf?'ME':(pt.label||'Contact'),{permanent:true,direction:'right',offset:[8,0]});}m.on('click',()=>{if(pt.isSelf){focusMapOnMe();}else{selectContact(pt.key);}});});if(meta){meta.textContent=pts.length>0?('Points: '+pts.length+' | Zoom: '+Number(contactsMapView.zoom||2).toFixed(0)):'No map points yet.';}setTimeout(()=>{if(contactsLeafletMap){contactsLeafletMap.invalidateSize(false);}},0);}"
      "function zoomContactsMap(factor){if(contactsLeafletMap){const delta=(Number(factor||1)>=1)?1:-1;contactsLeafletMap.setZoom(clamp(contactsLeafletMap.getZoom()+delta,2,19));return;}contactsMapView.zoom=clamp(Number(contactsMapView.zoom||2)*Number(factor||1),2,19);drawContactsMap();}"
      "function focusMapOnMe(){if(!statusCache)return;const lat=Number(statusCache.node_lat);const lon=Number(statusCache.node_lon);if(!hasValidCoord(lat,lon)){alert('Your node has no valid location set');return;}contactsMapView.centerLat=lat;contactsMapView.centerLon=lon;contactsMapView.zoom=Math.max(Number(contactsMapView.zoom||2),13);contactsMapCenterSet=true;if(contactsLeafletMap){contactsLeafletMap.setView([lat,lon],Math.round(clamp(contactsMapView.zoom,2,19)));}drawContactsMap();}"
      "async function boot(){showTab('config');bindNodeNameInput();bindLocationInputs();bindWifiInputs();bindRadioInputs();bindTimezoneInput();window.addEventListener('resize',()=>{drawContactsMap();});ensureTimezoneOptions('UTC0');await loadPresets();await loadStatus(true);await loadChannels();await loadContacts();setInterval(()=>{loadStatus(false);loadContacts();},4000);}boot();"
      "</script></div></body></html>";

  html.replace("__REGION_OPTIONS__", region_options);
  html.replace("__REGION_DEFAULTS__", region_defaults_js);
  if (html.length() == 0) {
    g_server.send(503, "text/plain", "Web UI unavailable (low memory)");
    return;
  }

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
  payload += ",\"timezone_posix\":";
  payload += jsonString(g_settings.timezone_posix);
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
  payload += ",\"adv_int_min\":";
  payload += String(g_settings.advert_interval_minutes);
  payload += ",\"path_hash_mode\":";
  payload += String(g_settings.path_hash_mode);
  payload += ",\"multi_ack\":";
  payload += g_settings.multi_ack ? "true" : "false";
  payload += ",\"repeater_mode\":";
  payload += g_settings.repeater_mode ? "true" : "false";
  payload += ",\"notifications_enabled\":";
  payload += g_settings.notifications_enabled ? "true" : "false";
  payload += ",\"mesh_region\":";
  payload += jsonString(g_settings.mesh_region);
  payload += ",\"screen_timeout_sec\":";
  payload += String(g_settings.screen_timeout_seconds);
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
      payload += ",\"ignored\":";
      payload += g_contacts_web_buf[i].ignored ? "true" : "false";
      payload += ",\"type\":";
      payload += String(static_cast<unsigned>(g_contacts_web_buf[i].type));
      payload += ",\"lastmod\":";
      payload += String(g_contacts_web_buf[i].lastmod);
      payload += ",\"gps_lat_i\":";
      payload += String(g_contacts_web_buf[i].gps_lat_i);
      payload += ",\"gps_lon_i\":";
      payload += String(g_contacts_web_buf[i].gps_lon_i);
      payload += ",\"gps_lat\":";
      payload += String(static_cast<double>(g_contacts_web_buf[i].gps_lat_i) / 10000000.0, 7);
      payload += ",\"gps_lon\":";
      payload += String(static_cast<double>(g_contacts_web_buf[i].gps_lon_i) / 10000000.0, 7);
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
  String timezone_posix = g_server.arg("timezone_posix");
  String tz_offset = g_server.arg("tz_offset");
  String region = g_server.arg("region");
  String freq = g_server.arg("freq");
  String bw = g_server.arg("bw");
  String sf = g_server.arg("sf");
  String cr = g_server.arg("cr");
  String pwr = g_server.arg("pwr");
  String adv_int_min = g_server.arg("adv_int_min");
  String path_hash_mode = g_server.arg("path_hash_mode");
  String multi_ack_str = g_server.arg("multi_ack");
  String repeater_str = g_server.arg("repeater_mode");
  String notifications_str = g_server.arg("notifications_enabled");
  String screen_timeout_sec = g_server.arg("screen_timeout_sec");
  String mesh_region = g_server.arg("mesh_region");

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
  timezone_posix.trim();
  tz_offset.trim();
  region.trim();
  freq.trim();
  bw.trim();
  sf.trim();
  cr.trim();
  pwr.trim();
  adv_int_min.trim();
  path_hash_mode.trim();
  multi_ack_str.trim();
  repeater_str.trim();
  notifications_str.trim();
  screen_timeout_sec.trim();
  mesh_region.trim();

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

  if (timezone_posix.length() == 0) {
    char tz_buf[24] = {};
    buildPosixUtcFromOffsetMinutes(static_cast<int16_t>(tz_offset_min), tz_buf, sizeof(tz_buf));
    timezone_posix = tz_buf;
  }

  if (timezone_posix.length() >= static_cast<int>(sizeof(g_settings.timezone_posix))) {
    sendJsonError("Timezone POSIX value too long");
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

  int advert_interval_minutes = static_cast<int>(g_settings.advert_interval_minutes);
  if (adv_int_min.length() > 0) {
    advert_interval_minutes = adv_int_min.toInt();
  }
  if (advert_interval_minutes < static_cast<int>(kMinAdvertIntervalMinutes) || advert_interval_minutes > 65535) {
    sendJsonError("Advert interval out of range");
    return;
  }

  int path_hash_mode_value = static_cast<int>(g_settings.path_hash_mode);
  if (path_hash_mode.length() > 0) {
    path_hash_mode_value = path_hash_mode.toInt();
  }
  if (path_hash_mode_value < 0 || path_hash_mode_value > 2) {
    sendJsonError("Path hash mode out of range");
    return;
  }

  int screen_timeout_seconds = static_cast<int>(g_settings.screen_timeout_seconds);
  if (screen_timeout_sec.length() > 0) {
    screen_timeout_seconds = screen_timeout_sec.toInt();
  }
  if (screen_timeout_seconds < static_cast<int>(kMinScreenTimeoutSeconds) ||
      screen_timeout_seconds > static_cast<int>(kMaxScreenTimeoutSeconds)) {
    sendJsonError("Screen timeout out of range");
    return;
  }

  if (mesh_region.length() >= static_cast<int>(sizeof(g_settings.mesh_region))) {
    sendJsonError("Mesh region too long");
    return;
  }

  const bool region_changed = strcmp(g_settings.region, region.c_str()) != 0;
  const bool posted_radio_matches_current = radioParamsEqual(
      g_settings.lora_freq_mhz, g_settings.lora_bw_khz, g_settings.lora_sf, g_settings.lora_cr,
      g_settings.lora_tx_power_dbm, freq_mhz, bw_khz, static_cast<uint8_t>(sf_value),
      static_cast<uint8_t>(cr_value), static_cast<int8_t>(pwr_value));

  // If only the region changed (without explicit radio edits), honor that by
  // applying the region preset server-side.
  if (region_changed && posted_radio_matches_current) {
    const RegionPreset* preset = findRegion(region.c_str());
    if (preset) {
      freq_mhz = preset->frequency_mhz;
      bw_khz = preset->bandwidth_khz;
      sf_value = static_cast<int>(preset->spreading_factor);
      cr_value = static_cast<int>(preset->coding_rate);
      pwr_value = static_cast<int>(preset->tx_power_dbm);
    }
  }

  const bool wifi_changed =
      (strcmp(g_settings.wifi_ssid, ssid.c_str()) != 0) || (strcmp(g_settings.wifi_pass, pass.c_str()) != 0);
  const bool radio_changed = !radioParamsEqual(
      g_settings.lora_freq_mhz, g_settings.lora_bw_khz, g_settings.lora_sf, g_settings.lora_cr,
      g_settings.lora_tx_power_dbm, freq_mhz, bw_khz, static_cast<uint8_t>(sf_value),
      static_cast<uint8_t>(cr_value), static_cast<int8_t>(pwr_value));
  const bool reboot_required = wifi_changed || radio_changed;

  copyString(g_settings.region, sizeof(g_settings.region), region.c_str());
  g_settings.lora_freq_mhz = freq_mhz;
  g_settings.lora_bw_khz = bw_khz;
  copyString(g_settings.mesh_region, sizeof(g_settings.mesh_region), mesh_region.c_str());
  g_settings.lora_sf = static_cast<uint8_t>(sf_value);
  g_settings.lora_cr = static_cast<uint8_t>(cr_value);
  g_settings.lora_tx_power_dbm = static_cast<int8_t>(pwr_value);
  g_settings.advert_interval_minutes = static_cast<uint16_t>(advert_interval_minutes);
  g_settings.path_hash_mode = static_cast<uint8_t>(path_hash_mode_value);
  g_settings.multi_ack = (multi_ack_str == "1" || multi_ack_str.equalsIgnoreCase("true"));
  // Guard against older cached pages that omit the field: only overwrite when present.
  if (repeater_str.length() > 0) {
    g_settings.repeater_mode = (repeater_str == "1" || repeater_str.equalsIgnoreCase("true"));
  }
  // Guard against older cached pages that omit the field: only overwrite when present.
  if (notifications_str.length() > 0) {
    g_settings.notifications_enabled =
        (notifications_str == "1" || notifications_str.equalsIgnoreCase("true"));
  }
  g_settings.screen_timeout_seconds = static_cast<uint16_t>(screen_timeout_seconds);
  copyString(g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), ssid.c_str());
  copyString(g_settings.wifi_pass, sizeof(g_settings.wifi_pass), pass.c_str());
  copyString(g_settings.node_name, sizeof(g_settings.node_name), node_name.c_str());
  g_settings.node_latitude = node_latitude;
  g_settings.node_longitude = node_longitude;
  g_settings.send_location_in_advert = send_location_in_advert;
  copyString(g_settings.timezone, sizeof(g_settings.timezone), timezone.c_str());
  copyString(g_settings.timezone_posix, sizeof(g_settings.timezone_posix), timezone_posix.c_str());
  g_settings.timezone_offset_minutes = static_cast<int16_t>(tz_offset_min);

  if (g_mesh) {
    g_mesh->setNodeName(g_settings.node_name);
    if (!g_mesh->setAdvertLocation(g_settings.send_location_in_advert, g_settings.node_latitude,
                                   g_settings.node_longitude)) {
      sendJsonError("Failed to apply advert location");
      return;
    }
    g_mesh->setGpsEnabled(!g_settings.send_location_in_advert);
    g_mesh->setAutoAdvertIntervalMinutes(g_settings.advert_interval_minutes);
    g_mesh->setPathHashMode(g_settings.path_hash_mode);
    g_mesh->setMultiAck(g_settings.multi_ack);
    g_mesh->setRepeaterMode(g_settings.repeater_mode);
    g_mesh->setMeshRegion(g_settings.mesh_region);
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
  g_server.on("/", HTTP_ANY, handleRoot);
  g_server.on("/index.html", HTTP_ANY, handleRoot);
  g_server.on("/generate_204", HTTP_ANY, handleRoot);              // Android captive portal probe
  g_server.on("/gen_204", HTTP_ANY, handleRoot);                   // Some Android variants
  g_server.on("/hotspot-detect.html", HTTP_ANY, handleRoot);       // Apple captive portal probe
  g_server.on("/library/test/success.html", HTTP_ANY, handleRoot); // Apple captive portal probe
  g_server.on("/connecttest.txt", HTTP_ANY, handleRoot);           // Microsoft captive portal probe
  g_server.on("/ncsi.txt", HTTP_ANY, handleRoot);                  // Microsoft captive portal probe
  g_server.on("/favicon.ico", HTTP_ANY, []() { g_server.send(204, "text/plain", ""); });
  g_server.on("/apple-touch-icon.png", HTTP_ANY, []() { g_server.send(204, "text/plain", ""); });
  g_server.on("/apple-touch-icon-precomposed.png", HTTP_ANY, []() { g_server.send(204, "text/plain", ""); });
  g_server.on("/robots.txt", HTTP_ANY, []() { g_server.send(204, "text/plain", ""); });
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
    const String uri = g_server.uri();
    // In AP mode, redirect unknown browser/captive portal paths to the root UI.
    if (uri.length() > 0 && !uri.startsWith("/api/")) {
      handleRoot();
      return;
    }
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
  // Use RW open so first boot creates namespace without noisy NOT_FOUND logs.
  if (!prefs.begin(kPrefsNs, false)) {
    return;
  }

  String node_name = kDefaultNodeName;
  if (prefs.isKey("node_name")) {
    node_name = prefs.getString("node_name", kDefaultNodeName);
  }

  double node_latitude = kDefaultNodeLatitude;
  if (prefs.isKey("node_lat")) {
    node_latitude = prefs.getDouble("node_lat", kDefaultNodeLatitude);
  }

  double node_longitude = kDefaultNodeLongitude;
  if (prefs.isKey("node_lon")) {
    node_longitude = prefs.getDouble("node_lon", kDefaultNodeLongitude);
  }

  bool send_location_in_advert = kDefaultSendLocationInAdvert;
  if (prefs.isKey("send_loc_adv")) {
    send_location_in_advert = prefs.getBool("send_loc_adv", kDefaultSendLocationInAdvert);
  }

  uint16_t advert_interval_minutes = kDefaultAdvertIntervalMinutes;
  if (prefs.isKey("adv_int_min")) {
    advert_interval_minutes = prefs.getUShort("adv_int_min", kDefaultAdvertIntervalMinutes);
  }

  uint16_t screen_timeout_seconds = kDefaultScreenTimeoutSeconds;
  if (prefs.isKey("screen_timeout")) {
    screen_timeout_seconds = prefs.getUShort("screen_timeout", kDefaultScreenTimeoutSeconds);
  }

  String ssid = kDefaultSsid;
  if (prefs.isKey("wifi_ssid")) {
    ssid = prefs.getString("wifi_ssid", kDefaultSsid);
  }

  String pass = kDefaultPass;
  if (prefs.isKey("wifi_pass")) {
    pass = prefs.getString("wifi_pass", kDefaultPass);
  }

  String timezone = kDefaultTimezone;
  if (prefs.isKey("timezone")) {
    timezone = prefs.getString("timezone", kDefaultTimezone);
  }

  String timezone_posix = "";
  if (prefs.isKey("timezone_posix")) {
    timezone_posix = prefs.getString("timezone_posix", "");
  }

  int tz_offset = 0;
  if (prefs.isKey("tz_offset")) {
    tz_offset = prefs.getInt("tz_offset", 0);
  }

  if (timezone_posix.length() == 0) {
    if (timezone.indexOf('/') < 0 && timezone.length() > 0) {
      timezone_posix = timezone;
    } else {
      char tz_buf[24] = {};
      buildPosixUtcFromOffsetMinutes(static_cast<int16_t>(tz_offset), tz_buf, sizeof(tz_buf));
      timezone_posix = tz_buf;
    }
  }

  String region = kDefaultRegion;
  if (prefs.isKey("region")) {
    region = prefs.getString("region", kDefaultRegion);
  }

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
  uint8_t path_hash_mode = kDefaultPathHashMode;

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
  if (prefs.isKey("path_hash_mode")) {
    path_hash_mode = prefs.getUChar("path_hash_mode", kDefaultPathHashMode);
  }
  bool multi_ack = false;
  if (prefs.isKey("multi_ack")) {
    multi_ack = prefs.getBool("multi_ack", false);
  }
  // Repeater mode defaults OFF on fresh installs (key absent).
  bool repeater_mode = false;
  if (prefs.isKey("repeater")) {
    repeater_mode = prefs.getBool("repeater", false);
  }
  bool notifications_enabled = kDefaultNotificationsEnabled;
  if (prefs.isKey("notifications")) {
    notifications_enabled = prefs.getBool("notifications", kDefaultNotificationsEnabled);
  }
  String mesh_region = String("");
  if (prefs.isKey("mesh_region")) {
    mesh_region = prefs.getString("mesh_region", "");
  }
  prefs.end();

  if (path_hash_mode > 2) {
    path_hash_mode = kDefaultPathHashMode;
  }

  if (advert_interval_minutes < kMinAdvertIntervalMinutes) {
    advert_interval_minutes = kDefaultAdvertIntervalMinutes;
  }
  screen_timeout_seconds = clampScreenTimeoutSeconds(screen_timeout_seconds);

  copyString(out_settings->node_name, sizeof(out_settings->node_name), node_name.c_str());
  out_settings->node_latitude = node_latitude;
  out_settings->node_longitude = node_longitude;
  out_settings->send_location_in_advert = send_location_in_advert;
  out_settings->advert_interval_minutes = advert_interval_minutes;
  out_settings->screen_timeout_seconds = screen_timeout_seconds;
  copyString(out_settings->wifi_ssid, sizeof(out_settings->wifi_ssid), ssid.c_str());
  copyString(out_settings->wifi_pass, sizeof(out_settings->wifi_pass), pass.c_str());
  copyString(out_settings->timezone, sizeof(out_settings->timezone), timezone.c_str());
  copyString(out_settings->timezone_posix, sizeof(out_settings->timezone_posix), timezone_posix.c_str());
  out_settings->timezone_offset_minutes = static_cast<int16_t>(tz_offset);
  copyString(out_settings->region, sizeof(out_settings->region), region.c_str());
  out_settings->lora_freq_mhz = freq_mhz;
  out_settings->lora_bw_khz = bw_khz;
  out_settings->lora_sf = sf;
  out_settings->lora_cr = cr;
  out_settings->lora_tx_power_dbm = pwr;
  out_settings->path_hash_mode = path_hash_mode;
  out_settings->multi_ack = multi_ack;
  out_settings->repeater_mode = repeater_mode;
  out_settings->notifications_enabled = notifications_enabled;
  copyString(out_settings->mesh_region, sizeof(out_settings->mesh_region), mesh_region.c_str());
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
  g_settings.screen_timeout_seconds = clampScreenTimeoutSeconds(static_cast<int>(g_settings.screen_timeout_seconds));
  if (g_mesh) {
    if (g_settings.node_name[0] != '\0') {
      g_mesh->setNodeName(g_settings.node_name);
    }
    g_mesh->setAdvertLocation(g_settings.send_location_in_advert, g_settings.node_latitude,
                              g_settings.node_longitude);
    g_mesh->setGpsEnabled(!g_settings.send_location_in_advert);
    g_mesh->setAutoAdvertIntervalMinutes(g_settings.advert_interval_minutes);
    g_mesh->setPathHashMode(g_settings.path_hash_mode);
    g_mesh->setMultiAck(g_settings.multi_ack);
    g_mesh->setRepeaterMode(g_settings.repeater_mode);
    g_mesh->setMeshRegion(g_settings.mesh_region);
  }
  applyTimezoneOffsetFromSettings();
  g_reboot_pending = false;
  g_reboot_at_ms = 0;

  registerRoutes();
  bringupNetwork();

  g_server_enabled = (strcmp(g_mode, "off") != 0);
  if (g_server_enabled) {
    g_server.begin();
  }
  g_running = true;
  return true;
}

void loop() {
  if (!g_running) {
    return;
  }
  if (g_server_enabled) {
    g_server.handleClient();
  }

  if (g_reboot_pending && static_cast<int32_t>(millis() - g_reboot_at_ms) >= 0) {
    delay(50);
    ESP.restart();
  }
}

void end() {
  if (!g_running) {
    return;
  }
  if (g_server_enabled) {
    g_server.stop();
    g_server_enabled = false;
  }
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

bool setNodeName(const char* node_name, char* err, size_t err_size) {
  if (!node_name || node_name[0] == '\0') {
    setImportError(err, err_size, "Node name is required");
    return false;
  }

  const size_t name_len = strlen(node_name);
  if (name_len == 0 || name_len > 31) {
    setImportError(err, err_size, "Node name length invalid");
    return false;
  }

  WebSettings next{};
  loadSettings(&next);
  copyString(next.node_name, sizeof(next.node_name), node_name);

  if (g_mesh) {
    g_mesh->setNodeName(next.node_name);
    g_mesh->broadcastSelfAdvertNow();
  }

  saveSettings(next);
  if (g_running) {
    g_settings = next;
  }

  setImportError(err, err_size, "");
  return true;
}

bool setSendLocationInAdvert(bool enabled, char* err, size_t err_size) {
  WebSettings next{};
  loadSettings(&next);
  Serial.printf("[GPSDBG][WEB] setSendLocationInAdvert request=%d current=%d\n",
                enabled ? 1 : 0, next.send_location_in_advert ? 1 : 0);
  if (next.send_location_in_advert == enabled) {
    Serial.println("[GPSDBG][WEB] no-op (already requested state)");
    setImportError(err, err_size, "");
    return true;
  }

  next.send_location_in_advert = enabled;

  if (g_mesh) {
    if (!g_mesh->setAdvertLocation(next.send_location_in_advert, next.node_latitude,
                                   next.node_longitude)) {
      Serial.println("[GPSDBG][WEB] setAdvertLocation failed");
      setImportError(err, err_size, "Failed to apply advert location");
      return false;
    }
    const bool gps_ok = g_mesh->setGpsEnabled(!next.send_location_in_advert);
    Serial.printf("[GPSDBG][WEB] mesh setGpsEnabled(%d) => %d\n",
                  next.send_location_in_advert ? 0 : 1, gps_ok ? 1 : 0);
    g_mesh->broadcastSelfAdvertNow();
  }

  saveSettings(next);
  Serial.printf("[GPSDBG][WEB] saved send_loc_adv=%d\n", next.send_location_in_advert ? 1 : 0);
  if (g_running) {
    g_settings = next;
  }

  setImportError(err, err_size, "");
  return true;
}

bool setPathHashMode(uint8_t mode, char* err, size_t err_size) {
  if (mode > 2) {
    setImportError(err, err_size, "path_hash_mode out of range");
    return false;
  }

  WebSettings next{};
  loadSettings(&next);
  if (next.path_hash_mode == mode) {
    setImportError(err, err_size, "");
    return true;
  }

  next.path_hash_mode = mode;

  if (g_mesh) {
    g_mesh->setPathHashMode(next.path_hash_mode);
  }

  saveSettings(next);
  if (g_running) {
    g_settings = next;
  }

  setImportError(err, err_size, "");
  return true;
}

bool setMultiAck(bool enabled, char* err, size_t err_size) {
  WebSettings next{};
  loadSettings(&next);
  next.multi_ack = enabled;
  if (g_mesh) {
    g_mesh->setMultiAck(enabled);
  }
  saveSettings(next);
  if (g_running) {
    g_settings = next;
  }
  setImportError(err, err_size, "");
  return true;
}

bool setRepeaterMode(bool enabled, char* err, size_t err_size) {
  WebSettings next{};
  loadSettings(&next);
  next.repeater_mode = enabled;
  if (g_mesh) {
    g_mesh->setRepeaterMode(enabled);
  }
  saveSettings(next);
  if (g_running) {
    g_settings = next;
  }
  setImportError(err, err_size, "");
  return true;
}

bool setNotificationsEnabled(bool enabled, char* err, size_t err_size) {
  WebSettings next{};
  loadSettings(&next);
  next.notifications_enabled = enabled;
  saveSettings(next);
  if (g_running) {
    g_settings = next;
  }
  setImportError(err, err_size, "");
  return true;
}

int regionPresetCount() {
  return static_cast<int>(sizeof(kRegionPresets) / sizeof(kRegionPresets[0]));
}

const char* regionPresetId(int index) {
  if (index < 0 || index >= regionPresetCount()) {
    return "";
  }
  return kRegionPresets[index].id;
}

const char* defaultRegionId() {
  return kDefaultRegion;
}

// Apply a region's radio preset (freq/bw/sf/cr/pwr + region id) and persist.
// Does NOT reboot; the radio is re-initialized at next boot, so the caller is
// responsible for restarting when it wants the change to take effect.
bool setRegionPreset(const char* region_id, char* err, size_t err_size) {
  const RegionPreset* preset = findRegion(region_id);
  if (!preset) {
    setImportError(err, err_size, "Unknown region preset");
    return false;
  }

  WebSettings next{};
  loadSettings(&next);
  copyString(next.region, sizeof(next.region), preset->id);
  next.lora_freq_mhz = preset->frequency_mhz;
  next.lora_bw_khz = preset->bandwidth_khz;
  next.lora_sf = preset->spreading_factor;
  next.lora_cr = preset->coding_rate;
  next.lora_tx_power_dbm = preset->tx_power_dbm;
  saveSettings(next);
  if (g_running) {
    g_settings = next;
  }
  setImportError(err, err_size, "");
  return true;
}

// Persist WiFi credentials (empty strings clear them). Applied at next boot;
// caller reboots.
bool setWifiCredentials(const char* ssid, const char* pass, char* err, size_t err_size) {
  WebSettings next{};
  loadSettings(&next);
  copyString(next.wifi_ssid, sizeof(next.wifi_ssid), ssid ? ssid : "");
  copyString(next.wifi_pass, sizeof(next.wifi_pass), pass ? pass : "");
  saveSettings(next);
  if (g_running) {
    g_settings = next;
  }
  setImportError(err, err_size, "");
  return true;
}

bool setMeshRegion(const char* region_name, char* err, size_t err_size) {
  const char* safe = region_name ? region_name : "";
  if (strlen(safe) >= sizeof(g_settings.mesh_region)) {
    setImportError(err, err_size, "Mesh region too long");
    return false;
  }

  WebSettings next{};
  loadSettings(&next);
  if (strcmp(next.mesh_region, safe) == 0) {
    setImportError(err, err_size, "");
    return true;
  }

  copyString(next.mesh_region, sizeof(next.mesh_region), safe);

  if (g_mesh) {
    g_mesh->setMeshRegion(next.mesh_region);
  }

  saveSettings(next);
  if (g_running) {
    g_settings = next;
  }

  setImportError(err, err_size, "");
  return true;
}

uint16_t screenTimeoutSeconds() {
  return clampScreenTimeoutSeconds(static_cast<int>(g_settings.screen_timeout_seconds));
}

bool notificationsEnabled() {
  if (g_running) {
    return g_settings.notifications_enabled;
  }
  WebSettings current{};
  loadSettings(&current);
  return current.notifications_enabled;
}

}  // namespace web
}  // namespace plumeria
