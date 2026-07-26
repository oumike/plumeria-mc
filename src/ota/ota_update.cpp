#include "ota/ota_update.h"
#include "ota/ota_signing_pubkey.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/ecp.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <string.h>

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

namespace plumeria {
namespace ota {
namespace {

constexpr uint32_t kReleaseCheckTimeoutMs = 12000;
constexpr uint32_t kReleaseDownloadTimeoutMs = 30000;
constexpr uint32_t kDownloadStallTimeoutMs = 15000;
constexpr size_t kOtaSigMaxBytes = 160;
constexpr size_t kOtaDigestBytes = 32;

constexpr const char* kLatestReleaseApiUrl =
  "http://ota.plumeria.sumat.org/firmware/latest";
constexpr const char* kReleaseDownloadBaseUrl =
    "http://ota.plumeria.sumat.org/firmware/";

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

  WiFiClient client;

  HTTPClient http;
  if (!http.begin(client, url)) {
    err_out = "Failed to start HTTP request";
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

void formatMbedTlsError(int code, char* out, size_t out_len) {
  if (!out || out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (code == 0) {
    strncpy(out, "ok", out_len - 1);
    out[out_len - 1] = '\0';
    return;
  }
  mbedtls_strerror(code, out, out_len);
}

bool httpGetBytes(const char* url,
                  uint8_t* out_buf,
                  size_t out_cap,
                  size_t* out_len,
                  String& err_out,
                  const char* accept_header = nullptr) {
  if (out_len) {
    *out_len = 0;
  }
  err_out = "";
  if (!url || !out_buf || out_cap == 0) {
    err_out = "Invalid arguments";
    return false;
  }

  preferExternalHeap();
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    err_out = "Failed to start HTTP request";
    return false;
  }

  http.setTimeout(static_cast<uint16_t>(kReleaseDownloadTimeoutMs));
  http.addHeader("User-Agent", "plumeria-mc-ota");
  if (accept_header && accept_header[0]) {
    http.addHeader("Accept", accept_header);
  }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  const int code = http.GET();
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

  const int content_len = http.getSize();
  if (content_len <= 0) {
    err_out = "Signature download empty";
    http.end();
    return false;
  }
  if (static_cast<size_t>(content_len) > out_cap) {
    err_out = "Signature too large";
    http.end();
    return false;
  }

  WiFiClient& stream = http.getStream();
  size_t used = 0;
  uint32_t last_progress_ms = millis();
  while (http.connected() && used < static_cast<size_t>(content_len)) {
    size_t avail = static_cast<size_t>(stream.available());
    if (avail == 0) {
      if ((millis() - last_progress_ms) > kDownloadStallTimeoutMs) {
        err_out = "Signature download stalled";
        http.end();
        return false;
      }
      delay(10);
      continue;
    }

    if (avail > static_cast<size_t>(content_len) - used) {
      avail = static_cast<size_t>(content_len) - used;
    }

    const int n = stream.readBytes(reinterpret_cast<char*>(out_buf + used), avail);
    if (n <= 0) {
      if ((millis() - last_progress_ms) > kDownloadStallTimeoutMs) {
        err_out = "Signature read timeout";
        http.end();
        return false;
      }
      delay(10);
      continue;
    }

    used += static_cast<size_t>(n);
    last_progress_ms = millis();
  }

  http.end();
  if (used != static_cast<size_t>(content_len)) {
    err_out = "Signature incomplete";
    return false;
  }

  if (out_len) {
    *out_len = used;
  }
  return true;
}

bool verifyDetachedSignatureP256Sha256(const uint8_t* digest,
                                       size_t digest_len,
                                       const uint8_t* signature,
                                       size_t signature_len,
                                       String& err_out) {
  err_out = "";
  if (!digest || digest_len != kOtaDigestBytes || !signature || signature_len == 0) {
    err_out = "Signature verify input invalid";
    return false;
  }
  if (kOtaSigningPublicKeyPem[0] == '\0') {
    err_out = "OTA signing public key missing";
    return false;
  }

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);

  const int parse_rc = mbedtls_pk_parse_public_key(
      &pk,
      reinterpret_cast<const unsigned char*>(kOtaSigningPublicKeyPem),
      strlen(kOtaSigningPublicKeyPem) + 1);
  if (parse_rc != 0) {
    char msg[96] = {};
    formatMbedTlsError(parse_rc, msg, sizeof(msg));
    err_out = String("Public key parse failed: ") + msg;
    mbedtls_pk_free(&pk);
    return false;
  }

  if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECKEY)) {
    err_out = "Public key is not EC";
    mbedtls_pk_free(&pk);
    return false;
  }

  const mbedtls_ecp_keypair* ec_key = mbedtls_pk_ec(pk);
  if (!ec_key || ec_key->grp.id != MBEDTLS_ECP_DP_SECP256R1) {
    err_out = "Public key is not P-256";
    mbedtls_pk_free(&pk);
    return false;
  }

  const int verify_rc = mbedtls_pk_verify(&pk,
                                          MBEDTLS_MD_SHA256,
                                          digest,
                                          digest_len,
                                          signature,
                                          signature_len);
  mbedtls_pk_free(&pk);
  if (verify_rc != 0) {
    char msg[96] = {};
    formatMbedTlsError(verify_rc, msg, sizeof(msg));
    err_out = String("Signature verify failed: ") + msg;
    return false;
  }

  return true;
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

  String api_err;
  String api_body;
  if (httpGetString(kLatestReleaseApiUrl,
                    api_body,
                    api_err,
                    true,
                    nullptr,
                    nullptr,
                    "application/vnd.github+json") &&
      extractJsonStringField(api_body, "tag_name", tag_out) && tag_out.length() > 0) {
    return true;
  }

  err_out = api_err.length() ? api_err : String("Release tag not found (proxy)");
  return false;
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

void buildAssetSignatureUrl(const char* tag, char* out_url, size_t out_len) {
  if (!out_url || out_len == 0) {
    return;
  }
  char asset_url[256] = {};
  buildAssetUrl(tag, asset_url, sizeof(asset_url));
  snprintf(out_url, out_len, "%s.sig", asset_url);
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
    // Keep contiguous internal heap available for OTA staging and hashing.
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
  char sig_url[272] = {};
  buildAssetSignatureUrl(tag_buf, sig_url, sizeof(sig_url));

  uint8_t signature[kOtaSigMaxBytes] = {};
  size_t signature_len = 0;
  String sig_err;
  if (!httpGetBytes(sig_url,
                    signature,
                    sizeof(signature),
                    &signature_len,
                    sig_err,
                    "application/octet-stream")) {
    String msg = String("Signature fetch failed: ") +
                 (sig_err.length() ? sig_err : String("unknown"));
    return setErr(err_out, err_len, msg.c_str());
  }
  if (signature_len < 8) {
    return setErr(err_out, err_len, "Signature invalid (too short)");
  }

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    return setErr(err_out, err_len, "Failed to start OTA download");
  }

  http.setTimeout(static_cast<uint16_t>(kReleaseDownloadTimeoutMs));
  http.addHeader("User-Agent", "plumeria-mc-ota");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  const int code = http.GET();
  if (code <= 0) {
    String msg = String("OTA download network error (") + String(code) + ")";
    http.end();
    return setErr(err_out, err_len, msg.c_str());
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

  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  const int sha_start_rc = mbedtls_sha256_starts_ret(&sha_ctx, 0);
  if (sha_start_rc != 0) {
    char msg[112] = {};
    char detail[80] = {};
    formatMbedTlsError(sha_start_rc, detail, sizeof(detail));
    snprintf(msg, sizeof(msg), "SHA256 init failed: %s", detail);
    mbedtls_sha256_free(&sha_ctx);
    Update.abort();
    http.end();
    return setErr(err_out, err_len, msg);
  }

  WiFiClient& stream = http.getStream();

  constexpr size_t kChunkSize = 512;
  uint8_t chunk[kChunkSize];
  size_t written = 0;
  uint32_t last_progress_ms = millis();
  bool stream_ok = true;

  while (http.connected() && (content_len <= 0 || written < static_cast<size_t>(content_len))) {
    size_t avail = static_cast<size_t>(stream.available());
    if (avail == 0) {
      if (progress_cb) {
        progress_cb(written, (content_len > 0) ? static_cast<size_t>(content_len) : 0);
      }
      if ((millis() - last_progress_ms) > kDownloadStallTimeoutMs) {
        stream_ok = false;
        setErr(err_out, err_len, "OTA stalled (no data)");
        break;
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
        stream_ok = false;
        setErr(err_out, err_len, "OTA stalled (read timeout)");
        break;
      }
      delay(10);
      continue;
    }

    const int sha_update_rc = mbedtls_sha256_update_ret(&sha_ctx, chunk, static_cast<size_t>(n));
    if (sha_update_rc != 0) {
      char msg[112] = {};
      char detail[80] = {};
      formatMbedTlsError(sha_update_rc, detail, sizeof(detail));
      snprintf(msg, sizeof(msg), "SHA256 update failed: %s", detail);
      setErr(err_out, err_len, msg);
      stream_ok = false;
      break;
    }

    const size_t wr = Update.write(chunk, static_cast<size_t>(n));
    if (wr != static_cast<size_t>(n)) {
      setErr(err_out, err_len, "OTA write failed");
      stream_ok = false;
      break;
    }

    written += wr;
    last_progress_ms = millis();
    if (progress_cb) {
      progress_cb(written, (content_len > 0) ? static_cast<size_t>(content_len) : 0);
    }
  }

  uint8_t digest[kOtaDigestBytes] = {};
  bool digest_ok = false;
  if (stream_ok && (content_len <= 0 || written == static_cast<size_t>(content_len))) {
    const int sha_finish_rc = mbedtls_sha256_finish_ret(&sha_ctx, digest);
    if (sha_finish_rc != 0) {
      char msg[112] = {};
      char detail[80] = {};
      formatMbedTlsError(sha_finish_rc, detail, sizeof(detail));
      snprintf(msg, sizeof(msg), "SHA256 finish failed: %s", detail);
      setErr(err_out, err_len, msg);
    } else {
      digest_ok = true;
    }
  } else if (stream_ok && content_len > 0 && written != static_cast<size_t>(content_len)) {
    setErr(err_out, err_len, "OTA write incomplete");
  }
  mbedtls_sha256_free(&sha_ctx);

  if (!stream_ok || !digest_ok) {
    Update.abort();
    http.end();
    return false;
  }

  String verify_err;
  if (!verifyDetachedSignatureP256Sha256(digest, sizeof(digest), signature, signature_len, verify_err)) {
    Update.abort();
    http.end();
    String msg = String("OTA signature invalid: ") +
                 (verify_err.length() ? verify_err : String("unknown"));
    return setErr(err_out, err_len, msg.c_str());
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
