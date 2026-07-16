#include "ota/ota_update.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <string.h>
#include <type_traits>

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

namespace plumeria {
namespace ota {
namespace {

constexpr uint32_t kReleaseCheckTimeoutMs = 12000;

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
constexpr int kTlsRxBufBytes = 256;
constexpr int kTlsTxBufBytes = 256;
#else
constexpr int kTlsRxBufBytes = 1024;
constexpr int kTlsTxBufBytes = 512;
#endif

constexpr const char* kLatestReleaseApiUrl =
    "https://api.github.com/repos/oumike/plumeria-mc/releases/latest";
constexpr const char* kLatestVersionRawUrl =
    "https://raw.githubusercontent.com/oumike/plumeria-mc/main/VERSION";
constexpr const char* kLatestVersionGithubUrl =
    "https://github.com/oumike/plumeria-mc/raw/main/VERSION";
constexpr const char* kLatestReleasePageUrl =
    "https://github.com/oumike/plumeria-mc/releases/latest";
constexpr const char* kReleaseDownloadBaseUrl =
    "https://github.com/oumike/plumeria-mc/releases/download/";

template <typename T>
class HasSetBufferSizes {
  template <typename U, void (U::*)(int, int)>
  struct SFINAE;
  template <typename U>
  static char test(SFINAE<U, &U::setBufferSizes>*);
  template <typename U>
  static int test(...);

 public:
  static const bool value = (sizeof(test<T>(nullptr)) == sizeof(char));
};

template <typename T>
typename std::enable_if<HasSetBufferSizes<T>::value, void>::type applyTlsBufferTuning(T& client) {
  client.setBufferSizes(kTlsRxBufBytes, kTlsTxBufBytes);
}

template <typename T>
typename std::enable_if<!HasSetBufferSizes<T>::value, void>::type applyTlsBufferTuning(T& client) {
  (void)client;
}

void configureTlsClient(WiFiClientSecure& client) {
  client.setInsecure();
  applyTlsBufferTuning(client);
}

bool isLikelyTlsInitFailure(const String& err) {
  // Arduino-ESP32 often reports TLS handshake allocation failures as
  // "Network error (-1)" at HTTP layer.
  return err.indexOf("(-1)") >= 0;
}

void buildTlsLowMemError(String& out) {
  char msg[160] = {};
  const uint32_t free_int = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const uint32_t largest_int =
      static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  snprintf(msg,
           sizeof(msg),
           "TLS init failed (low memory): int_free=%lu largest=%lu",
           static_cast<unsigned long>(free_int),
           static_cast<unsigned long>(largest_int));
  out = msg;
}

void clearCheckResult(OtaCheckResult& out) {
  memset(&out, 0, sizeof(out));
}

void copyStringToBuf(char* dst, size_t dst_len, const char* src) {
  if (!dst || dst_len == 0) {
    return;
  }
  if (!src) {
    src = "";
  }
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

void trimAsciiWhitespace(String& s) {
  int start = 0;
  int end = static_cast<int>(s.length()) - 1;
  while (start <= end && isspace(static_cast<unsigned char>(s[start]))) {
    start++;
  }
  while (end >= start && isspace(static_cast<unsigned char>(s[end]))) {
    end--;
  }
  if (start == 0 && end == static_cast<int>(s.length()) - 1) {
    return;
  }
  if (end < start) {
    s = "";
    return;
  }
  s = s.substring(start, end + 1);
}

bool extractJsonStringField(const String& json, const char* field, String& value_out) {
  value_out = "";
  if (!field || !field[0]) {
    return false;
  }

  String key = String("\"") + field + "\"";
  const int key_pos = json.indexOf(key);
  if (key_pos < 0) {
    return false;
  }

  const int colon_pos = json.indexOf(':', key_pos + static_cast<int>(key.length()));
  if (colon_pos < 0) {
    return false;
  }

  const int q1 = json.indexOf('"', colon_pos + 1);
  if (q1 < 0) {
    return false;
  }

  String out = "";
  bool esc = false;
  for (int i = q1 + 1; i < static_cast<int>(json.length()); i++) {
    const char c = json[i];
    if (esc) {
      switch (c) {
        case '"':
          out += '"';
          break;
        case '\\':
          out += '\\';
          break;
        case '/':
          out += '/';
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        default:
          out += c;
          break;
      }
      esc = false;
      continue;
    }
    if (c == '\\') {
      esc = true;
      continue;
    }
    if (c == '"') {
      value_out = out;
      return true;
    }
    out += c;
  }

  return false;
}

bool httpGetString(const char* url,
                   String& body_out,
                   String& err_out,
                   bool follow_redirects,
                   int* status_out = nullptr,
                   String* location_out = nullptr,
                   const char* accept_header = nullptr) {
  preferExternalHeap();

  body_out = "";
  err_out = "";
  if (status_out) {
    *status_out = 0;
  }
  if (location_out) {
    *location_out = "";
  }

  WiFiClientSecure client;
  configureTlsClient(client);

  HTTPClient http;
  if (!http.begin(client, url)) {
    err_out = "Failed to start HTTPS request";
    return false;
  }

  http.setTimeout(static_cast<uint16_t>(kReleaseCheckTimeoutMs));
  http.addHeader("User-Agent", "plumeria-mc-ota");
  if (accept_header && accept_header[0]) {
    http.addHeader("Accept", accept_header);
  }
  http.setFollowRedirects(follow_redirects ? HTTPC_STRICT_FOLLOW_REDIRECTS
                                           : HTTPC_DISABLE_FOLLOW_REDIRECTS);

  const int code = http.GET();
  if (status_out) {
    *status_out = code;
  }
  if (location_out) {
    *location_out = http.getLocation();
  }

  if (code <= 0) {
    err_out = String("Network error (") + String(code) + ")";
    http.end();
    return false;
  }

  if (code != HTTP_CODE_OK) {
    err_out = String("HTTP ") + String(code);
    http.end();
    return false;
  }

  body_out = http.getString();
  http.end();
  return true;
}

bool extractTagFromReleasePath(const String& text, String& tag_out) {
  tag_out = "";
  const int p = text.indexOf("/tag/");
  if (p < 0) {
    return false;
  }

  const int start = p + 5;
  int end = start;
  while (end < static_cast<int>(text.length())) {
    const char c = text[end];
    if (c == '"' || c == '\'' || c == '?' || c == '&' || c == '#' ||
        isspace(static_cast<unsigned char>(c))) {
      break;
    }
    end++;
  }

  if (end <= start) {
    return false;
  }
  tag_out = text.substring(start, end);
  trimAsciiWhitespace(tag_out);
  return tag_out.length() > 0;
}

bool fetchLatestTagFromReleasePage(String& tag_out, String& err_out) {
  tag_out = "";
  err_out = "";

  String body;
  String err;
  String location;
  int status = 0;
  const bool ok = httpGetString(kLatestReleasePageUrl,
                                body,
                                err,
                                false,
                                &status,
                                &location,
                                "text/html");

  // github.com/releases/latest usually redirects to /releases/tag/<tag>.
  if (!ok) {
    if ((status == HTTP_CODE_MOVED_PERMANENTLY || status == HTTP_CODE_FOUND ||
         status == HTTP_CODE_SEE_OTHER || status == HTTP_CODE_TEMPORARY_REDIRECT ||
         status == HTTP_CODE_PERMANENT_REDIRECT) &&
        location.length() > 0 && extractTagFromReleasePath(location, tag_out)) {
      return true;
    }
    err_out = String("Release page ") + (err.length() ? err : String("failed"));
    return false;
  }

  if (extractTagFromReleasePath(body, tag_out)) {
    return true;
  }

  err_out = "Release page tag not found";
  return false;
}

bool fetchLatestTagFromVersionFile(String& tag_out, String& err_out) {
  tag_out = "";
  err_out = "";

  const char* urls[] = {
      kLatestVersionRawUrl,
      kLatestVersionGithubUrl,
  };

  String first_err;
  String second_err;

  for (int i = 0; i < 2; i++) {
    String body;
    String err;
    if (!httpGetString(urls[i], body, err, true, nullptr, nullptr, "text/plain")) {
      if (i == 0) {
        first_err = err;
      } else {
        second_err = err;
      }
      continue;
    }

    tag_out = body;
    trimAsciiWhitespace(tag_out);
    if (tag_out.length() > 0) {
      return true;
    }
    if (i == 0) {
      first_err = "VERSION file empty";
    } else {
      second_err = "VERSION file empty";
    }
  }

  err_out = String("VERSION failed (") + first_err + "; " + second_err + ")";
  return false;
}

int parseNextVersionNumber(const char* s, int& idx) {
  if (!s) {
    return -1;
  }
  while (s[idx] && !isdigit(static_cast<unsigned char>(s[idx]))) {
    idx++;
  }
  if (!s[idx]) {
    return -1;
  }

  int val = 0;
  while (isdigit(static_cast<unsigned char>(s[idx]))) {
    val = val * 10 + (s[idx] - '0');
    idx++;
  }
  return val;
}

int compareVersionTags(const char* a, const char* b) {
  if (!a) {
    a = "";
  }
  if (!b) {
    b = "";
  }

  int ia = 0;
  int ib = 0;
  while (true) {
    int va = parseNextVersionNumber(a, ia);
    int vb = parseNextVersionNumber(b, ib);

    if (va < 0 && vb < 0) {
      return 0;
    }
    if (va < 0) {
      va = 0;
    }
    if (vb < 0) {
      vb = 0;
    }
    if (va < vb) {
      return -1;
    }
    if (va > vb) {
      return 1;
    }
  }
}

bool fetchLatestReleaseTag(String& tag_out, String& err_out) {
  tag_out = "";
  err_out = "";

  if (WiFi.status() != WL_CONNECTED) {
    err_out = "WiFi not connected";
    return false;
  }

#if defined(DEVICE_TDECK)
  // T-Deck has tighter OTA TLS memory margins; keep check path minimal.
  String version_err;
  if (fetchLatestTagFromVersionFile(tag_out, version_err)) {
    return true;
  }
  if (isLikelyTlsInitFailure(version_err)) {
    buildTlsLowMemError(err_out);
    return false;
  }
  err_out = version_err.length() ? version_err : String("VERSION check failed");
  return false;
#else
  String api_err;
  String page_err;
  String version_err;
  String api_body;
  if (httpGetString(kLatestReleaseApiUrl,
                    api_body,
                    api_err,
                    true,
                    nullptr,
                    nullptr,
                    "application/vnd.github+json")) {
    if (extractJsonStringField(api_body, "tag_name", tag_out) && tag_out.length() > 0) {
      return true;
    }
    api_err = "Release API tag not found";
  } else if (isLikelyTlsInitFailure(api_err)) {
    buildTlsLowMemError(err_out);
    return false;
  }

  if (fetchLatestTagFromReleasePage(tag_out, page_err)) {
    return true;
  }
  if (isLikelyTlsInitFailure(page_err)) {
    buildTlsLowMemError(err_out);
    return false;
  }

  if (fetchLatestTagFromVersionFile(tag_out, version_err)) {
    return true;
  }
  if (isLikelyTlsInitFailure(version_err)) {
    buildTlsLowMemError(err_out);
    return false;
  }

  if (api_err.length() && page_err.length() && version_err.length()) {
    err_out = api_err + "; page fallback failed (" + page_err +
              "); version fallback failed (" + version_err + ")";
  } else if (api_err.length()) {
    err_out = api_err;
  } else if (page_err.length()) {
    err_out = page_err;
  } else {
    err_out = version_err;
  }
  return false;
#endif
}

void buildAssetUrl(const char* tag, char* out_url, size_t out_len) {
  if (!out_url || out_len == 0) {
    return;
  }
  const char* use_tag = (tag && tag[0]) ? tag : "";
  snprintf(out_url,
           out_len,
           "%s%s/plumeria-mc-%s-%s-ota.bin",
           kReleaseDownloadBaseUrl,
           use_tag,
           currentDeviceAssetSlug(),
           use_tag);
}

bool setErr(char* err_out, size_t err_len, const char* msg) {
  copyStringToBuf(err_out, err_len, msg);
  return false;
}

}  // namespace

void preferExternalHeap() {
#if defined(BOARD_HAS_PSRAM) && BOARD_HAS_PSRAM
  static bool enabled = false;
  if (!enabled) {
    // Keep more contiguous internal heap available for TLS setup buffers.
    heap_caps_malloc_extmem_enable(0);
    enabled = true;
  }
#endif
}

const char* currentDeviceAssetSlug() {
#if defined(PLUMERIA_OTA_ASSET_SLUG)
  return PLUMERIA_OTA_ASSET_SLUG;
#elif defined(DEVICE_TDECK)
  return "tdeck";
#elif defined(DEVICE_TLORA_PAGER_TFT)
  return "tlora-pager-tft";
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
  return "cardputer-cap";
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
  #if defined(DEVICE_UI_VERTICAL) && (DEVICE_UI_VERTICAL)
    return "heltec-vertical";
  #else
    return "heltec";
  #endif
#else
  return "tdeck";
#endif
}

bool checkLatestRelease(OtaCheckResult& out) {
  clearCheckResult(out);

  String latest_tag;
  String err;
  if (!fetchLatestReleaseTag(latest_tag, err)) {
    out.ok = false;
    copyStringToBuf(out.error, sizeof(out.error), err.c_str());
    return false;
  }

  copyStringToBuf(out.latest_tag, sizeof(out.latest_tag), latest_tag.c_str());
  buildAssetUrl(out.latest_tag, out.download_url, sizeof(out.download_url));
  out.update_available = (compareVersionTags(APP_VERSION, out.latest_tag) < 0);
  out.ok = true;
  return true;
}

bool installLatestRelease(const char* tag,
                         char* err_out,
                         size_t err_len,
                         OtaInstallProgressCb progress_cb) {
  preferExternalHeap();

  if (WiFi.status() != WL_CONNECTED) {
    return setErr(err_out, err_len, "WiFi not connected");
  }

  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next) {
    return setErr(err_out, err_len, "No OTA app partition available");
  }

  char tag_buf[48] = {};
  if (tag && tag[0]) {
    copyStringToBuf(tag_buf, sizeof(tag_buf), tag);
  } else {
    String latest_tag;
    String err;
    if (!fetchLatestReleaseTag(latest_tag, err)) {
      return setErr(err_out, err_len, err.c_str());
    }
    copyStringToBuf(tag_buf, sizeof(tag_buf), latest_tag.c_str());
  }

  char url[256] = {};
  buildAssetUrl(tag_buf, url, sizeof(url));

  WiFiClientSecure client;
  configureTlsClient(client);

  HTTPClient http;
  if (!http.begin(client, url)) {
    return setErr(err_out, err_len, "Failed to start OTA download");
  }

  http.setTimeout(30000);
  http.addHeader("User-Agent", "plumeria-mc-ota");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  const int code = http.GET();
  if (code <= 0) {
    if (code == -1) {
      String tls_err;
      buildTlsLowMemError(tls_err);
      http.end();
      return setErr(err_out, err_len, tls_err.c_str());
    }
    http.end();
    return setErr(err_out, err_len, "OTA download network error");
  }
  if (code != HTTP_CODE_OK) {
    char msg[96] = {};
    snprintf(msg, sizeof(msg), "OTA HTTP %d", code);
    http.end();
    return setErr(err_out, err_len, msg);
  }

  const int content_len = http.getSize();
  const size_t update_size = (content_len > 0) ? static_cast<size_t>(content_len)
                                                : static_cast<size_t>(UPDATE_SIZE_UNKNOWN);
  if (!Update.begin(update_size)) {
    char msg[96] = {};
    snprintf(msg, sizeof(msg), "Update.begin failed (%u)", static_cast<unsigned>(Update.getError()));
    http.end();
    return setErr(err_out, err_len, msg);
  }

  WiFiClient& stream = http.getStream();

  constexpr size_t kChunkSize = 512;
  constexpr uint32_t kDownloadStallTimeoutMs = 15000;
  uint8_t chunk[kChunkSize];
  size_t written = 0;
  uint32_t last_progress_ms = millis();

  while (http.connected() && (content_len <= 0 || written < static_cast<size_t>(content_len))) {
    size_t avail = static_cast<size_t>(stream.available());
    if (avail == 0) {
      if (progress_cb) {
        progress_cb(written, (content_len > 0) ? static_cast<size_t>(content_len) : 0);
      }
      if ((millis() - last_progress_ms) > kDownloadStallTimeoutMs) {
        Update.abort();
        http.end();
        return setErr(err_out, err_len, "OTA stalled (no data)");
      }
      delay(10);
      continue;
    }

    if (avail > kChunkSize) {
      avail = kChunkSize;
    }

    const int n = stream.readBytes(reinterpret_cast<char*>(chunk), avail);
    if (n <= 0) {
      if (progress_cb) {
        progress_cb(written, (content_len > 0) ? static_cast<size_t>(content_len) : 0);
      }
      if ((millis() - last_progress_ms) > kDownloadStallTimeoutMs) {
        Update.abort();
        http.end();
        return setErr(err_out, err_len, "OTA stalled (read timeout)");
      }
      delay(10);
      continue;
    }

    const size_t wr = Update.write(chunk, static_cast<size_t>(n));
    if (wr != static_cast<size_t>(n)) {
      Update.abort();
      http.end();
      return setErr(err_out, err_len, "OTA write failed");
    }

    written += wr;
    last_progress_ms = millis();
    if (progress_cb) {
      progress_cb(written, (content_len > 0) ? static_cast<size_t>(content_len) : 0);
    }
  }

  if (content_len > 0 && written != static_cast<size_t>(content_len)) {
    Update.abort();
    http.end();
    return setErr(err_out, err_len, "OTA write incomplete");
  }

  if (!Update.end()) {
    char msg[96] = {};
    snprintf(msg, sizeof(msg), "Update.end failed (%u)", static_cast<unsigned>(Update.getError()));
    http.end();
    return setErr(err_out, err_len, msg);
  }

  if (!Update.isFinished()) {
    http.end();
    return setErr(err_out, err_len, "OTA image not complete");
  }

  http.end();
  return true;
}

}  // namespace ota
}  // namespace plumeria
