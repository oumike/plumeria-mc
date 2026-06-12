#pragma once

#include <stdint.h>

namespace plumeria {
namespace hal {

struct RadioConfig {
  float frequency_mhz;
  float bandwidth_khz;
  uint8_t spreading_factor;
  uint8_t coding_rate;
  int8_t tx_power_dbm;
  bool rx_boosted_gain;

  int8_t spi_sck;
  int8_t spi_miso;
  int8_t spi_mosi;
  int8_t radio_cs;
  int8_t radio_dio1;
  int8_t radio_rst;
  int8_t radio_busy;
};

class DeviceBoard {
 public:
  bool begin();
  void loop();
  RadioConfig defaultRadioConfig() const;
};

}  // namespace hal
}  // namespace plumeria
