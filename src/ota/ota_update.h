#pragma once

#include <Arduino.h>

namespace plumeria {
namespace ota {

struct OtaCheckResult {
  bool ok;
  bool update_available;
  char latest_tag[48];
  char download_url[256];
  char error[160];
};

typedef void (*OtaInstallProgressCb)(size_t written_bytes, size_t total_bytes);

// Route generic allocations to PSRAM when available so TLS has more internal DRAM headroom.
void preferExternalHeap();

// Device-specific release artifact slug (for example: tdeck, cardputer-cap).
const char* currentDeviceAssetSlug();

// Checks GitHub release metadata and computes the expected OTA binary URL.
bool checkLatestRelease(OtaCheckResult& out);

// Downloads and installs the latest release binary for this device target.
// If tag is null/empty, it fetches the latest release tag first.
bool installLatestRelease(const char* tag,
                         char* err_out,
                         size_t err_len,
                         OtaInstallProgressCb progress_cb = nullptr);

}  // namespace ota
}  // namespace plumeria
