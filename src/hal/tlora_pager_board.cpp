#include "hal/tlora_pager_board.h"

#include <Arduino.h>

namespace {

#ifndef LORA_FREQ
#define LORA_FREQ 869.618f
#endif

#ifndef LORA_BW
#define LORA_BW 62.5f
#endif

#ifndef LORA_SF
#define LORA_SF 8
#endif

#ifndef LORA_CR
#define LORA_CR 5
#endif

#ifndef LORA_TX_PWR
#define LORA_TX_PWR 22
#endif

#ifndef LORA_RX_BOOSTED_GAIN
#define LORA_RX_BOOSTED_GAIN 1
#endif

}  // namespace

namespace plumeria {
namespace hal {

bool TloraPagerBoard::begin() {
  Serial.println("[HAL] T-Lora pager board boot scaffold initialized");
  return true;
}

void TloraPagerBoard::loop() {
  // Reserved for board-level periodic tasks (battery, sensors, PMU) as hardware is added.
}

TloraPagerRadioConfig TloraPagerBoard::defaultRadioConfig() const {
  TloraPagerRadioConfig cfg{};

  // LilyGo T-Lora Pager TFT SX1262 pinout.
  cfg.spi_sck = 35;
  cfg.spi_miso = 33;
  cfg.spi_mosi = 34;
  cfg.radio_cs = 36;
  cfg.radio_dio1 = 14;
  cfg.radio_rst = 47;
  cfg.radio_busy = 48;

  cfg.frequency_mhz = LORA_FREQ;
  cfg.bandwidth_khz = LORA_BW;
  cfg.spreading_factor = static_cast<uint8_t>(LORA_SF);
  cfg.coding_rate = static_cast<uint8_t>(LORA_CR);
  cfg.tx_power_dbm = static_cast<int8_t>(LORA_TX_PWR);
  cfg.rx_boosted_gain = LORA_RX_BOOSTED_GAIN != 0;
  return cfg;
}

}  // namespace hal
}  // namespace plumeria
