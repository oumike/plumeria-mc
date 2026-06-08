#include "hal/tlora_pager_lvgl.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lvgl.h>

namespace {

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
constexpr int kInputActiveLevel = LOW;
constexpr uint32_t kInputDebounceMs = 70;

constexpr uint16_t kLvglMaxHorRes = 480;
constexpr uint16_t kLvglFallbackHorRes = 480;
constexpr uint16_t kLvglFallbackVerRes = 222;
constexpr uint16_t kLvglBufferLines = 20;

class LGFX_TloraPager : public lgfx::LGFX_Device {
 public:
  LGFX_TloraPager() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
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
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }

    setPanel(&panel_);
  }

 private:
  lgfx::Panel_ST7796 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
};

LGFX_TloraPager g_lcd;

lv_disp_draw_buf_t g_draw_buf;
lv_color_t g_draw_pixels[kLvglMaxHorRes * kLvglBufferLines];
lv_disp_drv_t g_disp_drv;
lv_indev_drv_t g_indev_drv;

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

  data->state = LV_INDEV_STATE_RELEASED;

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

  if (click_edge) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->key = LV_KEY_ENTER;
    last_event_ms = now_ms;
    return;
  }

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

bool g_started = false;

}  // namespace

namespace plumeria {
namespace hal {

bool TloraPagerLvgl::begin() {
  if (g_started) {
    return true;
  }

  pinMode(kScrollWheelUpPin, INPUT_PULLUP);
  pinMode(kScrollWheelDownPin, INPUT_PULLUP);
  pinMode(kScrollWheelPressPin, INPUT_PULLUP);

  g_lcd.init();
  g_lcd.setRotation(3);
  g_lcd.setBrightness(130);
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

  g_started = true;
  Serial.printf("[HAL] LVGL display + scroll-wheel input ready (%ldx%ld)\n", static_cast<long>(panel_w),
                static_cast<long>(panel_h));
  return true;
}

void TloraPagerLvgl::loop() {
  // Reserved for display-side periodic hooks.
}

}  // namespace hal
}  // namespace plumeria
