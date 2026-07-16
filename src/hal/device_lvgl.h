#pragma once

#include <stddef.h>
#include <stdint.h>

namespace plumeria {
namespace hal {

class DeviceLvgl {
 public:
  bool beginBootDisplay();
  bool begin();
  void loop();

  void drawBootStatus(const char* line1, const char* line2 = nullptr);
  void drawBootProgress(const char* title,
                        const char* detail,
                        size_t written_bytes,
                        size_t total_bytes,
                        bool stalled);

  void setScreenTimeoutSeconds(uint16_t timeout_seconds);
  void setScreenOn(bool on);
  bool screenOn() const;
};

}  // namespace hal
}  // namespace plumeria
