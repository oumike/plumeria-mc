#include "hal/device_lvgl.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Wire.h>
#include <lvgl.h>

namespace {

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
constexpr int kKeyboardAddr = 0x55;
constexpr uint32_t kKeyboardIdleProbeMs = 12;
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
#endif

  data->state = LV_INDEV_STATE_RELEASED;

#if defined(DEVICE_TDECK)
  // Prioritize keyboard keys so Enter/backspace/text are delivered independently
  // of trackball motion and click behavior.
  const uint32_t kb_now_ms = millis();
  bool irq_active = (digitalRead(kKeyboardInt) == LOW);
  if (irq_active || (kb_now_ms - last_keyboard_probe_ms >= kKeyboardIdleProbeMs)) {
    last_keyboard_probe_ms = kb_now_ms;
    Wire.requestFrom(static_cast<uint8_t>(kKeyboardAddr), static_cast<uint8_t>(1));
    if (Wire.available()) {
      uint8_t raw = Wire.read();
      if (raw != 0x00 && raw != 0xFF) {
        uint32_t mapped = 0;
        switch (raw) {
          case 0x0D:
          case 0x0A:
            mapped = LV_KEY_ENTER;
            break;
          case 0x1B:
            mapped = LV_KEY_ESC;
            break;
          case 0x08:
          case 0x7F:
            mapped = LV_KEY_BACKSPACE;
            break;
          default:
            mapped = raw;
            break;
        }

        data->state = LV_INDEV_STATE_PRESSED;
        data->key = mapped;
        return;
      }
    }
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
  Wire.beginTransmission(kKeyboardAddr);
  Wire.endTransmission();
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
  Serial.printf("[HAL] LVGL display + scroll-wheel input ready (%ldx%ld)\n", static_cast<long>(panel_w),
                static_cast<long>(panel_h));
  return true;
}

void DeviceLvgl::loop() {
  // Reserved for display-side periodic hooks.
}

}  // namespace hal
}  // namespace plumeria
