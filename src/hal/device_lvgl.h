#pragma once

#include <stdint.h>

namespace plumeria {
namespace hal {

class DeviceLvgl {
 public:
  bool begin();
  void loop();

  void setScreenTimeoutSeconds(uint16_t timeout_seconds);
  void setScreenOn(bool on);
  bool screenOn() const;
};

}  // namespace hal
}  // namespace plumeria
