#include "hal/device_lvgl.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Wire.h>
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
constexpr uint8_t kKeyboardAddrDefault = 0x55;
constexpr uint8_t kKeyboardReadBytes = 2;
constexpr uint8_t kKeyboardAddrCandidates[] = {0x55, 0x5F, 0x56};
constexpr uint32_t kKeyboardIdleProbeMs = 12;  // Fallback polling for boards with unreliable keyboard IRQ.
constexpr uint32_t kKeyboardRecoveryProbeMs = 1000;
constexpr uint8_t kKeyboardMaxFailStreak = 4;
#else
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
constexpr uint32_t kInputDebounceMs = 70;
constexpr uint16_t kLvglBufferLines = 20;

#if defined(DEVICE_TDECK)
using DisplayPanel = lgfx::Panel_ST7789;
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
    {
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
uint8_t g_keyboard_addr_candidate_idx = 0;
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

  static bool prev_up_pressed = false;
  static bool prev_down_pressed = false;
  static bool prev_click_pressed = false;
  static uint32_t last_event_ms = 0;
#if defined(DEVICE_TDECK)
  static uint32_t last_keyboard_probe_ms = 0;
  static uint32_t last_keyboard_recover_probe_ms = 0;
#endif

  data->state = LV_INDEV_STATE_RELEASED;

#if defined(DEVICE_TDECK)
  // Prioritize keyboard keys so Enter/backspace/text are delivered independently
  // of trackball motion and click behavior.
  const uint32_t kb_now_ms = millis();
  bool irq_active = (digitalRead(kKeyboardInt) == LOW);
  bool should_probe = false;

  if (!g_keyboard_available) {
    // Keyboard missing/busy: probe slowly, but still attempt reads because some
    // firmwares may not ACK a write probe while still supporting read requests.
    if (kb_now_ms - last_keyboard_recover_probe_ms >= kKeyboardRecoveryProbeMs) {
      last_keyboard_recover_probe_ms = kb_now_ms;
      uint8_t detected_addr = g_keyboard_addr;
      if (probeKeyboardAddress(&detected_addr)) {
        g_keyboard_addr = detected_addr;
      } else {
        const size_t candidate_count = sizeof(kKeyboardAddrCandidates) / sizeof(kKeyboardAddrCandidates[0]);
        if (candidate_count > 0) {
          g_keyboard_addr = kKeyboardAddrCandidates[g_keyboard_addr_candidate_idx % candidate_count];
          g_keyboard_addr_candidate_idx = static_cast<uint8_t>((g_keyboard_addr_candidate_idx + 1) % candidate_count);
        }
      }
      should_probe = true;
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
    if (got >= 1 && Wire.available()) {
      if (!g_keyboard_available) {
        g_keyboard_available = true;
        g_keyboard_fail_streak = 0;
#if PLUMERIA_KEY_DEBUG
        if (false) Serial.println("[KEYHAL] keyboard became available");
#endif
        if (false) Serial.printf("[HAL] keyboard active at 0x%02X\n", static_cast<unsigned>(g_keyboard_addr));
      }
      const uint8_t raw0 = Wire.read();
      uint8_t raw1 = 0x00;
      if (Wire.available()) {
        raw1 = Wire.read();
      }

      // Some keyboard firmwares publish two-byte reports where the actual key code
      // is the second byte; prefer a non-empty second byte when present.
      uint8_t raw = raw0;
      if (raw1 != 0x00 && raw1 != 0xFF) {
        raw = raw1;
      }

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
  if (false) Serial.printf("[KEYHAL] raw0=0x%02X raw1=0x%02X raw=0x%02X code=0x%02X mapped=%lu irq=%d\n",
          static_cast<unsigned>(raw0), static_cast<unsigned>(raw1), static_cast<unsigned>(raw),
                      static_cast<unsigned>(raw_code),
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
                      Wire.available() ? 1 : 0, irq_active ? 1 : 0, static_cast<unsigned>(g_keyboard_fail_streak),
                      g_keyboard_available ? 1 : 0);
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

  bool up_pressed = digitalRead(kScrollWheelUpPin) == kInputActiveLevel;
  bool down_pressed = digitalRead(kScrollWheelDownPin) == kInputActiveLevel;
  bool click_pressed = digitalRead(kScrollWheelPressPin) == kInputActiveLevel;

  bool up_edge = up_pressed && !prev_up_pressed;
  bool down_edge = down_pressed && !prev_down_pressed;
  bool click_edge = click_pressed && !prev_click_pressed;

  prev_up_pressed = up_pressed;
  prev_down_pressed = down_pressed;
  prev_click_pressed = click_pressed;

  uint32_t now_ms = millis();
  if ((now_ms - last_event_ms) < kInputDebounceMs) {
    return;
  }

  // Trackball click is intentionally not mapped to Enter.
  (void)click_edge;

  // Ignore ambiguous simultaneous pulses from wheel contacts.
  if (up_edge && !down_pressed) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_UP;
    last_event_ms = now_ms;
    return;
  }

  if (down_edge && !up_pressed) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_DOWN;
    last_event_ms = now_ms;
  }
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

#if defined(DEVICE_TDECK)
  pinMode(kKeyboardInt, INPUT_PULLUP);
  Wire.begin(kKeyboardSda, kKeyboardScl, 100000UL);
  Wire.setClock(400000UL);
  delay(30);
  uint8_t detected_addr = kKeyboardAddrDefault;
  g_keyboard_available = probeKeyboardAddress(&detected_addr);
  g_keyboard_addr = detected_addr;
  g_keyboard_fail_streak = 0;
  g_keyboard_map_mode = KeyboardMapMode::Unknown;
  g_last_keyboard_diag_ms = 0;
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

  g_lcd.init();
  g_lcd.setRotation(kDisplayRotation);
  g_lcd.setBrightness(kDisplayBrightness);
  g_lcd.fillScreen(TFT_BLACK);

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
  lv_indev_drv_init(&g_touch_indev_drv);
  g_touch_indev_drv.type = LV_INDEV_TYPE_POINTER;
  g_touch_indev_drv.read_cb = read_touch;
  lv_indev_drv_register(&g_touch_indev_drv);
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
