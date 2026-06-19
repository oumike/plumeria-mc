#include "hal/device_board.h"

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

#if defined(DEVICE_TDECK)
#ifndef BOARD_POWERON
#define BOARD_POWERON 10
#endif
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#ifndef BOARD_POWERON
#define BOARD_POWERON 7
#endif
#else
#ifndef BOARD_POWERON
#define BOARD_POWERON -1
#endif
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
#ifndef BOARD_VEXT_ENABLE
#define BOARD_VEXT_ENABLE 36
#endif
#else
#ifndef BOARD_VEXT_ENABLE
#define BOARD_VEXT_ENABLE -1
#endif
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
#ifndef BOARD_VEXT_ON_LEVEL
#define BOARD_VEXT_ON_LEVEL LOW
#endif
#else
#ifndef BOARD_VEXT_ON_LEVEL
#define BOARD_VEXT_ON_LEVEL HIGH
#endif
#endif

#if defined(DEVICE_TDECK)
#ifndef P_LORA_SCLK
#define P_LORA_SCLK 40
#endif
#ifndef P_LORA_MISO
#define P_LORA_MISO 38
#endif
#ifndef P_LORA_MOSI
#define P_LORA_MOSI 41
#endif
#ifndef P_LORA_NSS
#define P_LORA_NSS 9
#endif
#ifndef P_LORA_DIO_1
#define P_LORA_DIO_1 45
#endif
#ifndef P_LORA_RESET
#define P_LORA_RESET 17
#endif
#ifndef P_LORA_BUSY
#define P_LORA_BUSY 13
#endif
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
#ifndef P_LORA_SCLK
#define P_LORA_SCLK 40
#endif
#ifndef P_LORA_MISO
#define P_LORA_MISO 39
#endif
#ifndef P_LORA_MOSI
#define P_LORA_MOSI 14
#endif
#ifndef P_LORA_NSS
#define P_LORA_NSS 5
#endif
#ifndef P_LORA_DIO_1
#define P_LORA_DIO_1 4
#endif
#ifndef P_LORA_RESET
#define P_LORA_RESET 3
#endif
#ifndef P_LORA_BUSY
#define P_LORA_BUSY 6
#endif
#else
#ifndef P_LORA_SCLK
#define P_LORA_SCLK 35
#endif
#ifndef P_LORA_MISO
#define P_LORA_MISO 33
#endif
#ifndef P_LORA_MOSI
#define P_LORA_MOSI 34
#endif
#ifndef P_LORA_NSS
#define P_LORA_NSS 36
#endif
#ifndef P_LORA_DIO_1
#define P_LORA_DIO_1 14
#endif
#ifndef P_LORA_RESET
#define P_LORA_RESET 47
#endif
#ifndef P_LORA_BUSY
#define P_LORA_BUSY 48
#endif
#endif

}  // namespace

namespace plumeria {
namespace hal {

bool DeviceBoard::begin() {
  #if (BOARD_POWERON >= 0)
  pinMode(BOARD_POWERON, OUTPUT);
  digitalWrite(BOARD_POWERON, HIGH);
  delay(20);
  #endif

  #if (BOARD_VEXT_ENABLE >= 0)
  pinMode(BOARD_VEXT_ENABLE, OUTPUT);
  digitalWrite(BOARD_VEXT_ENABLE, BOARD_VEXT_ON_LEVEL);
  delay(20);
  #endif

  #if defined(DEVICE_TDECK)
  if (false) Serial.println("[HAL] T-Deck board boot scaffold initialized");
  #else
  if (false) Serial.println("[HAL] T-Lora pager board boot scaffold initialized");
  #endif
  return true;
}

void DeviceBoard::loop() {
  // Reserved for board-level periodic tasks (battery, sensors, PMU) as hardware is added.
}

RadioConfig DeviceBoard::defaultRadioConfig() const {
  RadioConfig cfg{};

  cfg.spi_sck = P_LORA_SCLK;
  cfg.spi_miso = P_LORA_MISO;
  cfg.spi_mosi = P_LORA_MOSI;
  cfg.radio_cs = P_LORA_NSS;
  cfg.radio_dio1 = P_LORA_DIO_1;
  cfg.radio_rst = P_LORA_RESET;
  cfg.radio_busy = P_LORA_BUSY;

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
