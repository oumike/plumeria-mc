#include "hal/device_lvgl.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lvgl.h>

namespace {

#ifndef PLUMERIA_KEY_DEBUG
#define PLUMERIA_KEY_DEBUG 0
#endif

#if defined(DEVICE_TDECK)
constexpr int kTftSck = 40;
constexpr int kTftMiso = 38;
constexpr int kTftMosi = 41;
constexpr int kTftCs = 12;
constexpr int kTftDc = 11;
constexpr int kTftBacklight = 42;

constexpr int kPanelWidth = 240;
constexpr int kPanelHeight = 320;
constexpr int kPanelOffsetX = 0;
constexpr int kPanelOffsetY = 0;

constexpr int kScrollWheelUpPin = 3;
constexpr int kScrollWheelDownPin = 2;
constexpr int kScrollWheelPressPin = 0;

constexpr uint16_t kLvglMaxHorRes = 320;
constexpr uint16_t kLvglFallbackHorRes = 320;
constexpr uint16_t kLvglFallbackVerRes = 240;
constexpr int kDisplayRotation = 1;
constexpr int kDisplayBrightness = 128;
constexpr int kBacklightPwmChannel = 0;
constexpr uint32_t kTftWriteHz = 40000000;
constexpr uint32_t kTftReadHz = 1000000;

constexpr int kTouchSda = 18;
constexpr int kTouchScl = 8;
constexpr int kTouchInt = 16;
constexpr int kTouchRst = -1;
constexpr int kTouchI2cPort = 0;
constexpr int kTouchAddr = 0x5D;

constexpr int kKeyboardSda = 18;
constexpr int kKeyboardScl = 8;
constexpr int kKeyboardInt = 46;
constexpr uint32_t kKeyboardI2cClockHz = 100000UL;
constexpr uint32_t kKeyboardStartupDelayMs = 500;
constexpr uint8_t kKeyboardAddrDefault = 0x55;
constexpr uint8_t kKeyboardBrightnessCmd = 0x01;
constexpr uint8_t kKeyboardAltBBrightnessCmd = 0x02;
constexpr uint8_t kKeyboardDefaultBrightness = 127;
constexpr uint8_t kKeyboardReadBytes = 1;
constexpr uint8_t kKeyboardAddrCandidates[] = {0x55, 0x5F, 0x56};
constexpr uint32_t kKeyboardIdleProbeMs = 12;  // Fallback polling for boards with unreliable keyboard IRQ.
constexpr uint32_t kKeyboardRecoveryProbeMs = 1000;
constexpr uint32_t kKeyboardBusRecoverDelayMs = 2;
constexpr uint8_t kKeyboardMaxFailStreak = 4;
constexpr bool kEnableTouchInput = true;
#elif defined(DEVICE_TLORA_PAGER_TFT)
constexpr int kTftSck = 35;
constexpr int kTftMiso = 33;
constexpr int kTftMosi = 34;
constexpr int kTftCs = 38;
constexpr int kTftDc = 37;
constexpr int kTftBacklight = 42;

constexpr int kPanelWidth = 222;
constexpr int kPanelHeight = 480;
constexpr int kPanelOffsetX = 49;
constexpr int kPanelOffsetY = 0;

constexpr int kScrollWheelUpPin = 40;
constexpr int kScrollWheelDownPin = 41;
constexpr int kScrollWheelPressPin = 7;
constexpr uint16_t kLvglMaxHorRes = 480;
constexpr uint16_t kLvglFallbackHorRes = 480;
constexpr uint16_t kLvglFallbackVerRes = 222;
constexpr int kDisplayRotation = 3;
constexpr int kDisplayBrightness = 130;
constexpr int kBacklightPwmChannel = 7;
constexpr uint32_t kTftWriteHz = 40000000;
constexpr uint32_t kTftReadHz = 16000000;

constexpr int kKeyboardSda = 3;
constexpr int kKeyboardScl = 2;
constexpr int kKeyboardInt = 6;
constexpr int kKeyboardBacklightPin = 46;
constexpr uint32_t kKeyboardI2cClockHz = 400000UL;
constexpr uint32_t kKeyboardStartupDelayMs = 30;
constexpr uint8_t kKeyboardAddrDefault = 0x34;
constexpr uint32_t kKeyboardIdleProbeMs = 120;
constexpr uint32_t kKeyboardRecoveryProbeMs = 1000;
constexpr uint32_t kKeyboardBusRecoverDelayMs = 2;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
// Placeholder profile for future Cardputer-specific display/input tuning.
constexpr int kTftSck = 35;
constexpr int kTftMiso = 33;
constexpr int kTftMosi = 34;
constexpr int kTftCs = 38;
constexpr int kTftDc = 37;
constexpr int kTftBacklight = 42;

constexpr int kPanelWidth = 222;
constexpr int kPanelHeight = 480;
constexpr int kPanelOffsetX = 49;
constexpr int kPanelOffsetY = 0;

constexpr int kScrollWheelUpPin = 40;
constexpr int kScrollWheelDownPin = 41;
constexpr int kScrollWheelPressPin = 7;
constexpr uint16_t kLvglMaxHorRes = 480;
constexpr uint16_t kLvglFallbackHorRes = 480;
constexpr uint16_t kLvglFallbackVerRes = 222;
constexpr int kDisplayRotation = 3;
constexpr int kDisplayBrightness = 130;
constexpr int kBacklightPwmChannel = 7;
constexpr uint32_t kTftWriteHz = 40000000;
constexpr uint32_t kTftReadHz = 16000000;
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
// Placeholder profile for future Heltec horizontal/vertical split behavior.
constexpr int kTftSck = 35;
constexpr int kTftMiso = 33;
constexpr int kTftMosi = 34;
constexpr int kTftCs = 38;
constexpr int kTftDc = 37;
constexpr int kTftBacklight = 42;

constexpr int kPanelWidth = 222;
constexpr int kPanelHeight = 480;
constexpr int kPanelOffsetX = 49;
constexpr int kPanelOffsetY = 0;

constexpr int kScrollWheelUpPin = 40;
constexpr int kScrollWheelDownPin = 41;
constexpr int kScrollWheelPressPin = 7;
constexpr uint16_t kLvglMaxHorRes = 480;
constexpr uint16_t kLvglFallbackHorRes = 480;
constexpr uint16_t kLvglFallbackVerRes = 222;
constexpr int kDisplayRotation = 3;
constexpr int kDisplayBrightness = 130;
constexpr int kBacklightPwmChannel = 7;
constexpr uint32_t kTftWriteHz = 40000000;
constexpr uint32_t kTftReadHz = 16000000;
#else
// Generic placeholder profile for non-target boards.
constexpr int kTftSck = 35;
constexpr int kTftMiso = 33;
constexpr int kTftMosi = 34;
constexpr int kTftCs = 38;
constexpr int kTftDc = 37;
constexpr int kTftBacklight = 42;

constexpr int kPanelWidth = 222;
constexpr int kPanelHeight = 480;
constexpr int kPanelOffsetX = 49;
constexpr int kPanelOffsetY = 0;

constexpr int kScrollWheelUpPin = 40;
constexpr int kScrollWheelDownPin = 41;
constexpr int kScrollWheelPressPin = 7;
constexpr uint16_t kLvglMaxHorRes = 480;
constexpr uint16_t kLvglFallbackHorRes = 480;
constexpr uint16_t kLvglFallbackVerRes = 222;
constexpr int kDisplayRotation = 3;
constexpr int kDisplayBrightness = 130;
constexpr int kBacklightPwmChannel = 7;
constexpr uint32_t kTftWriteHz = 40000000;
constexpr uint32_t kTftReadHz = 16000000;
#endif

constexpr int kInputActiveLevel = LOW;
#if defined(DEVICE_TLORA_PAGER_TFT)
constexpr uint32_t kInputDebounceMs = 24;
constexpr uint32_t kClickDebounceMs = 220;
constexpr uint32_t kClickAfterScrollGuardMs = 80;
constexpr uint32_t kWheelReleaseGuardMs = 10;
constexpr uint32_t kWheelPollIntervalMs = 2;
constexpr uint32_t kWheelButtonDebounceMs = 20;
constexpr uint8_t kWheelQueueDepth = 12;
constexpr bool kWheelInvertDirection = false;
#else
constexpr uint32_t kInputDebounceMs = 70;
constexpr uint32_t kClickDebounceMs = 70;
constexpr uint32_t kClickAfterScrollGuardMs = 0;
constexpr uint32_t kWheelDirectionHoldMs = 0;
constexpr uint32_t kWheelReleaseGuardMs = 0;
constexpr bool kWheelInvertDirection = false;
#endif
constexpr uint16_t kLvglBufferLines = 20;

#if defined(DEVICE_TDECK)
using DisplayPanel = lgfx::Panel_ST7789;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
// Placeholder until Cardputer panel profile is implemented.
using DisplayPanel = lgfx::Panel_ST7796;
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
// Placeholder until Heltec panel profile is implemented.
using DisplayPanel = lgfx::Panel_ST7796;
#else
using DisplayPanel = lgfx::Panel_ST7796;
#endif

class LGFX_DeviceDisplay : public lgfx::LGFX_Device {
 public:
  LGFX_DeviceDisplay() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = kTftWriteHz;
      cfg.freq_read = kTftReadHz;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.pin_sclk = kTftSck;
      cfg.pin_miso = kTftMiso;
      cfg.pin_mosi = kTftMosi;
      cfg.pin_dc = kTftDc;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }

    {
      auto cfg = panel_.config();
      cfg.pin_cs = kTftCs;
      cfg.pin_rst = -1;
      cfg.panel_width = kPanelWidth;
      cfg.panel_height = kPanelHeight;
      cfg.offset_x = kPanelOffsetX;
      cfg.offset_y = kPanelOffsetY;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.readable = true;
      panel_.config(cfg);
    }

    {
      auto cfg = light_.config();
      cfg.pin_bl = kTftBacklight;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = kBacklightPwmChannel;
      light_.config(cfg);
      panel_.setLight(&light_);
    }

#if defined(DEVICE_TDECK)
    if (kEnableTouchInput) {
      auto cfg = touch_.config();
      cfg.x_min = 0;
      cfg.x_max = kPanelWidth - 1;
      cfg.y_min = 0;
      cfg.y_max = kPanelHeight - 1;
      cfg.pin_int = kTouchInt;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = kTouchI2cPort;
      cfg.i2c_addr = kTouchAddr;
      cfg.pin_sda = kTouchSda;
      cfg.pin_scl = kTouchScl;
      cfg.pin_rst = kTouchRst;
      cfg.freq = 400000;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
#endif

    setPanel(&panel_);
  }

 private:
  DisplayPanel panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
#if defined(DEVICE_TDECK)
  lgfx::Touch_GT911 touch_;
#endif
};

LGFX_DeviceDisplay g_lcd;

lv_disp_draw_buf_t g_draw_buf;
lv_color_t g_draw_pixels[kLvglMaxHorRes * kLvglBufferLines];
lv_disp_drv_t g_disp_drv;
lv_indev_drv_t g_indev_drv;
lv_indev_drv_t g_touch_indev_drv;
#if defined(DEVICE_TDECK)
bool g_keyboard_available = false;
uint8_t g_keyboard_fail_streak = 0;
uint32_t g_last_keyboard_diag_ms = 0;
uint8_t g_keyboard_addr = kKeyboardAddrDefault;
bool g_keyboard_backlight_inited = false;
uint8_t g_last_keyboard_press_code = 0;
uint32_t g_last_keyboard_press_ms = 0;
enum class KeyboardMapMode : uint8_t {
  Unknown = 0,
  Ascii,
  Hid,
};
KeyboardMapMode g_keyboard_map_mode = KeyboardMapMode::Unknown;

bool probeKeyboardAddress(uint8_t* out_addr) {
  if (!out_addr) {
    return false;
  }

  for (size_t i = 0; i < (sizeof(kKeyboardAddrCandidates) / sizeof(kKeyboardAddrCandidates[0])); i++) {
    const uint8_t addr = kKeyboardAddrCandidates[i];
    Wire.beginTransmission(addr);
    const int err = Wire.endTransmission();
    if (err == 0) {
      *out_addr = addr;
      return true;
    }
  }

  return false;
}

void initKeyboardBacklightDefaults() {
  if (g_keyboard_backlight_inited) {
    return;
  }

  Wire.beginTransmission(g_keyboard_addr);
  Wire.write(kKeyboardAltBBrightnessCmd);
  Wire.write(kKeyboardDefaultBrightness);
  const int err_alt = Wire.endTransmission();

  Wire.beginTransmission(g_keyboard_addr);
  Wire.write(kKeyboardBrightnessCmd);
  Wire.write(kKeyboardDefaultBrightness);
  const int err_now = Wire.endTransmission();

  if (err_alt == 0 && err_now == 0) {
    g_keyboard_backlight_inited = true;
  }
}

void recoverKeyboardBus() {
  Wire.end();
  delay(kKeyboardBusRecoverDelayMs);
  Wire.begin(kKeyboardSda, kKeyboardScl, 100000UL);
  Wire.setClock(kKeyboardI2cClockHz);
}

void initKeyboardInterface() {
  recoverKeyboardBus();
  delay(kKeyboardStartupDelayMs);

  uint8_t detected_addr = kKeyboardAddrDefault;
  g_keyboard_available = probeKeyboardAddress(&detected_addr);
  g_keyboard_addr = detected_addr;
  g_keyboard_fail_streak = 0;
  g_keyboard_backlight_inited = false;
  g_keyboard_map_mode = KeyboardMapMode::Unknown;
  g_last_keyboard_diag_ms = 0;

  if (g_keyboard_available) {
    initKeyboardBacklightDefaults();
  }
}
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
bool g_keyboard_available = false;
uint8_t g_keyboard_addr = kKeyboardAddrDefault;
uint8_t g_pager_mod_state = 0;
uint32_t g_pager_mod_set_ms = 0;

struct PagerWheelEvent {
  int8_t dir;
  bool click;
};

QueueHandle_t g_pager_wheel_queue = nullptr;
TaskHandle_t g_pager_wheel_task = nullptr;

constexpr uint8_t kPagerRegIntStat = 0x02;
constexpr uint8_t kPagerRegKeyLckEc = 0x03;
constexpr uint8_t kPagerRegKeyEventA = 0x04;
constexpr uint8_t kPagerRegGpioIntEn1 = 0x1A;
constexpr uint8_t kPagerRegGpioIntEn2 = 0x1B;
constexpr uint8_t kPagerRegGpioIntEn3 = 0x1C;
constexpr uint8_t kPagerRegKpGpio1 = 0x1D;
constexpr uint8_t kPagerRegKpGpio2 = 0x1E;
constexpr uint8_t kPagerRegKpGpio3 = 0x1F;
constexpr uint8_t kPagerRegGpiEm1 = 0x20;
constexpr uint8_t kPagerRegGpiEm2 = 0x21;
constexpr uint8_t kPagerRegGpiEm3 = 0x22;
constexpr uint8_t kPagerRegGpioDir1 = 0x23;
constexpr uint8_t kPagerRegGpioDir2 = 0x24;
constexpr uint8_t kPagerRegGpioDir3 = 0x25;
constexpr uint8_t kPagerRegGpioIntLvl1 = 0x26;
constexpr uint8_t kPagerRegGpioIntLvl2 = 0x27;
constexpr uint8_t kPagerRegGpioIntLvl3 = 0x28;
constexpr uint8_t kPagerRegDebounceDis1 = 0x29;
constexpr uint8_t kPagerRegDebounceDis2 = 0x2A;
constexpr uint8_t kPagerRegDebounceDis3 = 0x2B;
constexpr uint8_t kPagerModShift = 0x01;
constexpr uint8_t kPagerModSym = 0x02;
constexpr uint32_t kPagerModTimeoutMs = 1500;

// Ben Buxton full-step rotary state table, same decoding model used by LilyGo.
constexpr uint8_t kRotaryDirCw = 0x10;
constexpr uint8_t kRotaryDirCcw = 0x20;
constexpr uint8_t kRotaryStart = 0x0;
constexpr uint8_t kRotaryCwFinal = 0x1;
constexpr uint8_t kRotaryCwBegin = 0x2;
constexpr uint8_t kRotaryCwNext = 0x3;
constexpr uint8_t kRotaryCcwBegin = 0x4;
constexpr uint8_t kRotaryCcwFinal = 0x5;
constexpr uint8_t kRotaryCcwNext = 0x6;

constexpr uint8_t kRotaryStateTable[7][4] = {
  {kRotaryStart, kRotaryCwBegin, kRotaryCcwBegin, kRotaryStart},
  {kRotaryCwNext, kRotaryStart, kRotaryCwFinal, static_cast<uint8_t>(kRotaryStart | kRotaryDirCw)},
  {kRotaryCwNext, kRotaryCwBegin, kRotaryStart, kRotaryStart},
  {kRotaryCwNext, kRotaryCwBegin, kRotaryCwFinal, kRotaryStart},
  {kRotaryCcwNext, kRotaryStart, kRotaryCcwBegin, kRotaryStart},
  {kRotaryCcwNext, kRotaryCcwFinal, kRotaryStart, static_cast<uint8_t>(kRotaryStart | kRotaryDirCcw)},
  {kRotaryCcwNext, kRotaryCcwFinal, kRotaryCcwBegin, kRotaryStart},
};

const uint8_t kPagerTapMap[31][3] = {
    {'q', 'Q', '1'}, {'w', 'W', '2'}, {'e', 'E', '3'}, {'r', 'R', '4'}, {'t', 'T', '5'},
    {'y', 'Y', '6'}, {'u', 'U', '7'}, {'i', 'I', '8'}, {'o', 'O', '9'}, {'p', 'P', '0'},
    {'a', 'A', '*'}, {'s', 'S', '/'}, {'d', 'D', '+'}, {'f', 'F', '-'}, {'g', 'G', '='},
    {'h', 'H', ':'}, {'j', 'J', '\''}, {'k', 'K', '"'}, {'l', 'L', '@'}, {'\n', 0x00, '\t'},
    {0x00, 0x00, 0x00}, {'z', 'Z', '_'}, {'x', 'X', '$'}, {'c', 'C', ';'}, {'v', 'V', '?'},
    {'b', 'B', '!'}, {'n', 'N', ','}, {'m', 'M', '.'}, {0x00, 0x00, 0x00}, {'\b', 0x00, 0x1B},
    {' ', 0x00, 0x00},
};

bool probeKeyboardAddress(uint8_t* out_addr) {
  if (!out_addr) {
    return false;
  }

  Wire.beginTransmission(kKeyboardAddrDefault);
  const int err = Wire.endTransmission();
  if (err == 0) {
    *out_addr = kKeyboardAddrDefault;
    return true;
  }
  return false;
}

void recoverKeyboardBus() {
  Wire.end();
  delay(kKeyboardBusRecoverDelayMs);
  Wire.begin(kKeyboardSda, kKeyboardScl, 100000UL);
  Wire.setClock(kKeyboardI2cClockHz);
}

void pagerWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(g_keyboard_addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t pagerReadReg(uint8_t reg) {
  Wire.beginTransmission(g_keyboard_addr);
  Wire.write(reg);
  Wire.endTransmission();

  const int got = Wire.requestFrom(g_keyboard_addr, static_cast<uint8_t>(1));
  if (got < 1 || !Wire.available()) {
    return 0;
  }
  return Wire.read();
}

void pagerResetKeyboardController() {
  // TCA8418 matrix setup used on TLora Pager.
  pagerWriteReg(kPagerRegGpioDir1, 0x00);
  pagerWriteReg(kPagerRegGpioDir2, 0x00);
  pagerWriteReg(kPagerRegGpioDir3, 0x00);

  pagerWriteReg(kPagerRegGpiEm1, 0xFF);
  pagerWriteReg(kPagerRegGpiEm2, 0xFF);
  pagerWriteReg(kPagerRegGpiEm3, 0xFF);

  pagerWriteReg(kPagerRegGpioIntLvl1, 0x00);
  pagerWriteReg(kPagerRegGpioIntLvl2, 0x00);
  pagerWriteReg(kPagerRegGpioIntLvl3, 0x00);

  pagerWriteReg(kPagerRegGpioIntEn1, 0xFF);
  pagerWriteReg(kPagerRegGpioIntEn2, 0xFF);
  pagerWriteReg(kPagerRegGpioIntEn3, 0xFF);

  // 4 matrix rows, 10 matrix columns.
  pagerWriteReg(kPagerRegKpGpio1, 0x0F);
  pagerWriteReg(kPagerRegKpGpio2, 0xFF);
  pagerWriteReg(kPagerRegKpGpio3, 0x03);

  pagerWriteReg(kPagerRegDebounceDis1, 0x00);
  pagerWriteReg(kPagerRegDebounceDis2, 0x00);
  pagerWriteReg(kPagerRegDebounceDis3, 0x00);

  while (pagerReadReg(kPagerRegKeyEventA) != 0) {
  }
  pagerWriteReg(kPagerRegIntStat, 0x03);
}

uint32_t pagerTranslateKey(uint8_t key_num) {
  const uint32_t now = millis();
  if (g_pager_mod_state != 0 && (now - g_pager_mod_set_ms) > kPagerModTimeoutMs) {
    g_pager_mod_state = 0;
  }

  // Key numbers are 1-based in the TCA8418 FIFO.
  if (key_num == 21) {
    g_pager_mod_state ^= kPagerModSym;
    g_pager_mod_set_ms = now;
    return 0;
  }
  if (key_num == 29) {
    g_pager_mod_state ^= kPagerModShift;
    g_pager_mod_set_ms = now;
    return 0;
  }

  if (key_num < 1 || key_num > 31) {
    return 0;
  }

  const uint8_t idx = static_cast<uint8_t>(key_num - 1);
  uint8_t mode = 0;
  if ((g_pager_mod_state & kPagerModSym) != 0) {
    mode = 2;
  } else if ((g_pager_mod_state & kPagerModShift) != 0) {
    mode = 1;
  }

  uint8_t mapped = kPagerTapMap[idx][mode];
  if (mapped == 0x00) {
    mapped = kPagerTapMap[idx][0];
  }
  g_pager_mod_state = 0;

  switch (mapped) {
    case '\n':
      return LV_KEY_ENTER;
    case '\b':
      return LV_KEY_BACKSPACE;
    case 0x1B:
      return LV_KEY_ESC;
    default:
      return static_cast<uint32_t>(mapped);
  }
}

uint32_t pagerReadMappedKey() {
  const uint8_t count = static_cast<uint8_t>(pagerReadReg(kPagerRegKeyLckEc) & 0x0F);
  if (count == 0) {
    return 0;
  }

  for (uint8_t i = 0; i < count; i++) {
    const uint8_t ev = pagerReadReg(static_cast<uint8_t>(kPagerRegKeyEventA + i));
    const bool pressed = (ev & 0x80) != 0;
    const uint8_t key_num = static_cast<uint8_t>(ev & 0x7F);
    if (!pressed) {
      continue;
    }

    const uint32_t mapped = pagerTranslateKey(key_num);
    if (mapped != 0) {
      return mapped;
    }
  }

  return 0;
}

void initKeyboardInterface() {
  recoverKeyboardBus();
  delay(kKeyboardStartupDelayMs);

  uint8_t detected_addr = kKeyboardAddrDefault;
  g_keyboard_available = probeKeyboardAddress(&detected_addr);
  g_keyboard_addr = detected_addr;
  g_pager_mod_state = 0;
  g_pager_mod_set_ms = 0;

  if (g_keyboard_available) {
    pinMode(kKeyboardBacklightPin, OUTPUT);
    digitalWrite(kKeyboardBacklightPin, HIGH);
    pagerResetKeyboardController();
  }
}

uint8_t pagerProcessRotaryStep() {
  static uint8_t rotary_state = kRotaryStart;
  const uint8_t pinstate = static_cast<uint8_t>((digitalRead(kScrollWheelDownPin) << 1) |
                                                digitalRead(kScrollWheelUpPin));
  rotary_state = kRotaryStateTable[rotary_state & 0x0F][pinstate];
  return static_cast<uint8_t>(rotary_state & 0x30);
}

bool pagerClickPressedEdge() {
  static uint8_t button_state = HIGH;
  static uint8_t last_button_state = HIGH;
  static uint32_t last_debounce_ms = 0;

  const uint8_t reading = static_cast<uint8_t>(digitalRead(kScrollWheelPressPin));
  if (reading != last_button_state) {
    last_debounce_ms = millis();
  }

  bool pressed = false;
  if (millis() - last_debounce_ms > kWheelButtonDebounceMs) {
    if (reading != button_state) {
      button_state = reading;
      if (button_state == LOW) {
        pressed = true;
      }
    }
  }

  last_button_state = reading;
  return pressed;
}

void pagerWheelTask(void* /*arg*/) {
  while (true) {
    PagerWheelEvent ev{0, false};
    const uint8_t rotary_result = pagerProcessRotaryStep();
    if (rotary_result == kRotaryDirCw) {
      ev.dir = 1;
    } else if (rotary_result == kRotaryDirCcw) {
      ev.dir = -1;
    }

    ev.click = pagerClickPressedEdge();

    if ((ev.dir != 0 || ev.click) && g_pager_wheel_queue != nullptr) {
      xQueueSend(g_pager_wheel_queue, &ev, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(kWheelPollIntervalMs));
  }
}

void initPagerWheelInput() {
  if (g_pager_wheel_queue == nullptr) {
    g_pager_wheel_queue = xQueueCreate(kWheelQueueDepth, sizeof(PagerWheelEvent));
  }
  if (g_pager_wheel_task == nullptr && g_pager_wheel_queue != nullptr) {
    xTaskCreate(pagerWheelTask, "pager_wheel", 2048, nullptr, 10, &g_pager_wheel_task);
  }
}

bool pagerPopWheelEvent(PagerWheelEvent* ev) {
  if (ev == nullptr || g_pager_wheel_queue == nullptr) {
    return false;
  }
  return xQueueReceive(g_pager_wheel_queue, ev, 0) == pdPASS;
}
#endif

#if defined(DEVICE_TDECK)
uint32_t mapKeyboardRawHid(uint8_t raw) {
  switch (raw) {
    case 0x28:
      return LV_KEY_ENTER;
    case 0x29:
      return LV_KEY_ESC;
    case 0x2A:
      return LV_KEY_BACKSPACE;
    case 0x4F:
      return LV_KEY_RIGHT;
    case 0x50:
      return LV_KEY_LEFT;
    case 0x51:
      return LV_KEY_DOWN;
    case 0x52:
      return LV_KEY_UP;
    case 0x2C:
      return ' ';
    case 0x27:
      return '0';
    default:
      break;
  }

  // Some keyboards emit USB HID usage IDs for alphanumerics.
  if (raw >= 0x04 && raw <= 0x1D) {
    return static_cast<uint32_t>('a' + (raw - 0x04));
  }
  if (raw >= 0x1E && raw <= 0x26) {
    return static_cast<uint32_t>('1' + (raw - 0x1E));
  }

  return static_cast<uint32_t>(raw);
}

uint32_t mapKeyboardRawAscii(uint8_t raw) {
  switch (raw) {
    case 0x0D:
    case 0x0A:
      return LV_KEY_ENTER;
    case 0x1B:
      return LV_KEY_ESC;
    case 0x08:
    case 0x7F:
      return LV_KEY_BACKSPACE;
    default:
      break;
  }

  return static_cast<uint32_t>(raw);
}

uint32_t mapKeyboardRaw(uint8_t raw) {
  if (g_keyboard_map_mode == KeyboardMapMode::Unknown) {
    if (raw >= 0x20 && raw <= 0x7E) {
      g_keyboard_map_mode = KeyboardMapMode::Ascii;
    } else if (raw == 0x28 || raw == 0x29 || raw == 0x2A || raw == 0x2B || raw == 0x2C || raw == 0x4F ||
               raw == 0x50 || raw == 0x51 || raw == 0x52 ||
               ((raw >= 0x04 && raw <= 0x27) && raw != 0x08 && raw != 0x0A && raw != 0x0D)) {
      g_keyboard_map_mode = KeyboardMapMode::Hid;
    }
#if PLUMERIA_KEY_DEBUG
    if (g_keyboard_map_mode == KeyboardMapMode::Ascii) {
      if (false) Serial.printf("[KEYHAL] map mode=ASCII raw=0x%02X\n", static_cast<unsigned>(raw));
    } else if (g_keyboard_map_mode == KeyboardMapMode::Hid) {
      if (false) Serial.printf("[KEYHAL] map mode=HID raw=0x%02X\n", static_cast<unsigned>(raw));
    }
#endif
  }

  if (g_keyboard_map_mode == KeyboardMapMode::Hid) {
    return mapKeyboardRawHid(raw);
  }

  return mapKeyboardRawAscii(raw);
}
#endif

void flush_lcd(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
  int32_t width = area->x2 - area->x1 + 1;
  int32_t height = area->y2 - area->y1 + 1;

  g_lcd.pushImage(area->x1, area->y1, width, height,
                  reinterpret_cast<lgfx::rgb565_t*>(&color_p->full));

  lv_disp_flush_ready(disp_drv);
}

void read_scroll_wheel(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  (void)drv;

  static uint32_t last_scroll_event_ms = 0;
  static uint32_t last_click_event_ms = 0;
#if defined(DEVICE_TLORA_PAGER_TFT)
#else
  static bool prev_click_pressed = false;
  static bool prev_up_pressed = false;
  static bool prev_down_pressed = false;
  static int8_t last_wheel_dir = 0;
  static uint32_t last_wheel_dir_ms = 0;
  static bool wheel_detent_latched = false;
  static uint32_t wheel_released_ms = 0;
#endif
#if defined(DEVICE_TDECK)
  static uint32_t last_keyboard_probe_ms = 0;
  static uint32_t last_keyboard_recover_probe_ms = 0;
#elif defined(DEVICE_TLORA_PAGER_TFT)
  static uint32_t last_keyboard_probe_ms = 0;
#endif

  data->state = LV_INDEV_STATE_RELEASED;

#if defined(DEVICE_TDECK)
  // Prioritize keyboard keys so Enter/backspace/text are delivered independently
  // of trackball motion and click behavior.
  const uint32_t kb_now_ms = millis();
  bool irq_active = (digitalRead(kKeyboardInt) == LOW);
  bool should_probe = false;

  if (!g_keyboard_available) {
    // Keyboard missing/busy: probe slowly for a readable key controller.
    if (kb_now_ms - last_keyboard_recover_probe_ms >= kKeyboardRecoveryProbeMs) {
      last_keyboard_recover_probe_ms = kb_now_ms;
      uint8_t detected_addr = g_keyboard_addr;
      if (probeKeyboardAddress(&detected_addr)) {
        g_keyboard_addr = detected_addr;
        g_keyboard_available = true;
        g_keyboard_fail_streak = 0;
        initKeyboardBacklightDefaults();
        should_probe = true;
      } else {
        recoverKeyboardBus();
        should_probe = false;
      }
#if PLUMERIA_KEY_DEBUG
      if ((kb_now_ms - g_last_keyboard_diag_ms) >= 1000) {
        if (false) Serial.println("[KEYHAL] recover probe failed");
        g_last_keyboard_diag_ms = kb_now_ms;
      }
#endif
    }
  } else {
    should_probe = irq_active;
    if (!should_probe && kKeyboardIdleProbeMs > 0 &&
        (kb_now_ms - last_keyboard_probe_ms >= kKeyboardIdleProbeMs)) {
      should_probe = true;
    }
  }

  if (should_probe) {
    last_keyboard_probe_ms = kb_now_ms;
    const int got = static_cast<int>(Wire.requestFrom(g_keyboard_addr, kKeyboardReadBytes));
    const bool emit_diag = (kb_now_ms - g_last_keyboard_diag_ms) >= 1000;
    if (got < 0) {
      if (g_keyboard_fail_streak < 255) {
        g_keyboard_fail_streak++;
      }
      if (g_keyboard_fail_streak >= kKeyboardMaxFailStreak) {
        g_keyboard_available = false;
        recoverKeyboardBus();
      }
#if PLUMERIA_KEY_DEBUG
      if (emit_diag) {
        if (false) Serial.printf("[KEYHAL] requestFrom fail addr=0x%02X got=%d irq=%d streak=%u kb_avail=%d\n",
                      static_cast<unsigned>(g_keyboard_addr), got, irq_active ? 1 : 0,
                      static_cast<unsigned>(g_keyboard_fail_streak), g_keyboard_available ? 1 : 0);
      }
#endif
    } else if (got >= 1 && Wire.available()) {
      const uint8_t raw = Wire.read();

      const bool high_bit_set = (raw & 0x80) != 0;
      const uint8_t raw_code = static_cast<uint8_t>(raw & 0x7F);
      if (raw != 0x00 && raw != 0xFF) {
        g_keyboard_fail_streak = 0;
        bool treat_as_release = false;
        if (high_bit_set && raw_code == g_last_keyboard_press_code &&
            (kb_now_ms - g_last_keyboard_press_ms) <= 120) {
          treat_as_release = true;
        }

        if (treat_as_release) {
#if PLUMERIA_KEY_DEBUG
          if (emit_diag) {
            if (false) Serial.printf("[KEYHAL] raw=0x%02X (release)\n", static_cast<unsigned>(raw));
          }
#endif
          return;
        }

        const uint32_t mapped = mapKeyboardRaw(raw_code);
      #if PLUMERIA_KEY_DEBUG
        if (false) Serial.printf("[KEYHAL] raw=0x%02X code=0x%02X mapped=%lu irq=%d\n",
          static_cast<unsigned>(raw), static_cast<unsigned>(raw_code),
                static_cast<unsigned long>(mapped), irq_active ? 1 : 0);
      #endif

        data->state = LV_INDEV_STATE_PRESSED;
        data->key = mapped;
        g_last_keyboard_press_code = raw_code;
        g_last_keyboard_press_ms = kb_now_ms;
        return;
      }
#if PLUMERIA_KEY_DEBUG
      if (emit_diag) {
        if (false) Serial.printf("[KEYHAL] probe got raw=0x%02X (no key) irq=%d mode=%u\n", static_cast<unsigned>(raw),
                      irq_active ? 1 : 0, static_cast<unsigned>(g_keyboard_map_mode));
      }
#endif
    } else if (got == 0) {
      // Idle poll with no key available; do not treat as an I2C failure.
      g_keyboard_fail_streak = 0;
    } else {
      if (g_keyboard_fail_streak < 255) {
        g_keyboard_fail_streak++;
      }
      if (g_keyboard_fail_streak >= kKeyboardMaxFailStreak) {
        g_keyboard_available = false;
      }
#if PLUMERIA_KEY_DEBUG
      if (emit_diag) {
        if (false) Serial.printf("[KEYHAL] probe fail addr=0x%02X got=%d avail=%d irq=%d streak=%u kb_avail=%d\n",
                      static_cast<unsigned>(g_keyboard_addr), got,
                      Wire.available() ? 1 : 0, irq_active ? 1 : 0,
                      static_cast<unsigned>(g_keyboard_fail_streak), g_keyboard_available ? 1 : 0);
      }
#endif
    }
#if PLUMERIA_KEY_DEBUG
    if (emit_diag) {
      g_last_keyboard_diag_ms = kb_now_ms;
    }
#endif
#if PLUMERIA_KEY_DEBUG
  } else if ((kb_now_ms - g_last_keyboard_diag_ms) >= 2000) {
    if (false) Serial.printf("[KEYHAL] idle avail=%d should_probe=%d irq=%d\n", g_keyboard_available ? 1 : 0,
                  should_probe ? 1 : 0, irq_active ? 1 : 0);
    g_last_keyboard_diag_ms = kb_now_ms;
#endif
  }
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
  const uint32_t kb_now_ms = millis();

  if (!g_keyboard_available) {
    if (kb_now_ms - last_keyboard_probe_ms >= kKeyboardRecoveryProbeMs) {
      last_keyboard_probe_ms = kb_now_ms;
      initKeyboardInterface();
    }
  } else {
    bool irq_active = (digitalRead(kKeyboardInt) == LOW);
    bool should_probe = irq_active;
    if (!should_probe && (kb_now_ms - last_keyboard_probe_ms >= kKeyboardIdleProbeMs)) {
      should_probe = true;
    }

    if (should_probe) {
      last_keyboard_probe_ms = kb_now_ms;
      const uint32_t mapped = pagerReadMappedKey();
      if (mapped != 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = mapped;
        return;
      }
    }
  }
#endif

  uint32_t now_ms = millis();

#if defined(DEVICE_TLORA_PAGER_TFT)
  PagerWheelEvent ev{0, false};
  if (pagerPopWheelEvent(&ev)) {
    if (ev.dir != 0) {
      if ((now_ms - last_scroll_event_ms) < kInputDebounceMs ||
          (now_ms - last_click_event_ms) < kWheelReleaseGuardMs) {
        return;
      }

      int8_t emit_dir = ev.dir;
      if (kWheelInvertDirection) {
        emit_dir = static_cast<int8_t>(-emit_dir);
      }
      data->state = LV_INDEV_STATE_PRESSED;
      data->key = emit_dir > 0 ? LV_KEY_DOWN : LV_KEY_UP;
      last_scroll_event_ms = now_ms;
      return;
    }

    if (ev.click) {
      if ((now_ms - last_click_event_ms) < kClickDebounceMs) {
        return;
      }
      if ((now_ms - last_scroll_event_ms) < kClickAfterScrollGuardMs) {
        return;
      }

      last_click_event_ms = now_ms;
      data->state = LV_INDEV_STATE_PRESSED;
      data->key = LV_KEY_ENTER;
      return;
    }
  }
  return;
#else
  bool up_pressed = digitalRead(kScrollWheelUpPin) == kInputActiveLevel;
  bool down_pressed = digitalRead(kScrollWheelDownPin) == kInputActiveLevel;
  bool click_pressed = digitalRead(kScrollWheelPressPin) == kInputActiveLevel;

  bool click_edge = click_pressed && !prev_click_pressed;

  prev_click_pressed = click_pressed;

  // Trackball click is intentionally not mapped to Enter.
  (void)click_edge;

  bool up_edge = up_pressed && !prev_up_pressed;
  bool down_edge = down_pressed && !prev_down_pressed;

  prev_up_pressed = up_pressed;
  prev_down_pressed = down_pressed;

  const bool wheel_contacts_released = !up_pressed && !down_pressed;
  if (wheel_contacts_released) {
    if (wheel_detent_latched) {
      wheel_detent_latched = false;
      wheel_released_ms = now_ms;
    }
  } else if (wheel_detent_latched) {
    return;
  }

  if ((now_ms - wheel_released_ms) < kWheelReleaseGuardMs) {
    return;
  }

  // Ignore ambiguous simultaneous pulses from wheel contacts.
  if (up_edge && !down_pressed) {
    if ((now_ms - last_scroll_event_ms) < kInputDebounceMs) {
      return;
    }
    if (last_wheel_dir < 0 && (now_ms - last_wheel_dir_ms) < kWheelDirectionHoldMs) {
      return;
    }
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_UP;
    last_scroll_event_ms = now_ms;
    last_wheel_dir = 1;
    last_wheel_dir_ms = now_ms;
    wheel_detent_latched = true;
    return;
  }

  if (down_edge && !up_pressed) {
    if ((now_ms - last_scroll_event_ms) < kInputDebounceMs) {
      return;
    }
    if (last_wheel_dir > 0 && (now_ms - last_wheel_dir_ms) < kWheelDirectionHoldMs) {
      return;
    }
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_DOWN;
    last_scroll_event_ms = now_ms;
    last_wheel_dir = -1;
    last_wheel_dir_ms = now_ms;
    wheel_detent_latched = true;
  }
#endif
}

void read_touch(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  (void)drv;

  int32_t tx = 0;
  int32_t ty = 0;
  if (g_lcd.getTouch(&tx, &ty)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = tx;
    data->point.y = ty;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

bool g_started = false;

}  // namespace

namespace plumeria {
namespace hal {

bool DeviceLvgl::begin() {
  if (g_started) {
    return true;
  }

  pinMode(kScrollWheelUpPin, INPUT_PULLUP);
  pinMode(kScrollWheelDownPin, INPUT_PULLUP);
  pinMode(kScrollWheelPressPin, INPUT_PULLUP);

#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
  pinMode(kKeyboardInt, INPUT_PULLUP);
#endif

  g_lcd.init();
  g_lcd.setRotation(kDisplayRotation);
  g_lcd.setBrightness(kDisplayBrightness);
  g_lcd.fillScreen(TFT_BLACK);

#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
  initKeyboardInterface();
#if defined(DEVICE_TLORA_PAGER_TFT)
  initPagerWheelInput();
#endif
#if defined(DEVICE_TDECK)
#if PLUMERIA_KEY_DEBUG
  if (false) Serial.printf("[KEYHAL] debug=1 addr=0x%02X idle_probe_ms=%lu available=%d\n",
                static_cast<unsigned>(g_keyboard_addr), static_cast<unsigned long>(kKeyboardIdleProbeMs),
                g_keyboard_available ? 1 : 0);
#endif
  if (!g_keyboard_available) {
    if (false) Serial.println("[HAL] keyboard not detected on known addresses, probing in recovery mode");
  } else {
    if (false) Serial.printf("[HAL] keyboard detected at 0x%02X\n", static_cast<unsigned>(g_keyboard_addr));
  }
#endif
#endif

  lv_disp_draw_buf_init(&g_draw_buf, g_draw_pixels, nullptr, kLvglMaxHorRes * kLvglBufferLines);

  int32_t panel_w = g_lcd.width();
  int32_t panel_h = g_lcd.height();
  if (panel_w <= 0 || panel_h <= 0) {
    panel_w = kLvglFallbackHorRes;
    panel_h = kLvglFallbackVerRes;
  }

  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res = static_cast<uint16_t>(panel_w);
  g_disp_drv.ver_res = static_cast<uint16_t>(panel_h);
  g_disp_drv.flush_cb = flush_lcd;
  g_disp_drv.draw_buf = &g_draw_buf;
  lv_disp_drv_register(&g_disp_drv);

  lv_indev_drv_init(&g_indev_drv);
  g_indev_drv.type = LV_INDEV_TYPE_KEYPAD;
  g_indev_drv.read_cb = read_scroll_wheel;
  lv_indev_drv_register(&g_indev_drv);

#if defined(DEVICE_TDECK)
  if (kEnableTouchInput) {
    lv_indev_drv_init(&g_touch_indev_drv);
    g_touch_indev_drv.type = LV_INDEV_TYPE_POINTER;
    g_touch_indev_drv.read_cb = read_touch;
    lv_indev_drv_register(&g_touch_indev_drv);
  }
#endif

  g_started = true;
  if (false) Serial.printf("[HAL] LVGL display + scroll-wheel input ready (%ldx%ld)\n", static_cast<long>(panel_w),
                static_cast<long>(panel_h));
  return true;
}

void DeviceLvgl::loop() {
  // Reserved for display-side periodic hooks.
}

}  // namespace hal
}  // namespace plumeria
