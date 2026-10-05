#include "display.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Wire.h>
#include <esp_log.h>
#include <lvgl.h>

#include "config.h"

static int s_touchSda = PIN_TOUCH_SDA;
static int s_touchScl = PIN_TOUCH_SCL;

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_FT5x06 _touchFt;
  lgfx::Touch_GT911 _touchGt;
  lgfx::Touch_CST816S _touchCst;
  lgfx::Touch_XPT2046 _touchXpt;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = LCD_SPI_FREQ;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = 0;  // 关掉 DMA，避免和 WiFi 抢总线时画面被写花
      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = PIN_LCD_MISO;
      cfg.pin_dc = PIN_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = PIN_LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = PIN_LCD_MISO >= 0;
      cfg.invert = LCD_INVERT;
      cfg.rgb_order = LCD_RGB_ORDER;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }

  // addr 为扫描到的 I2C 地址；-1 表示总线上没有触摸芯片
  void useTouch(int addr) {
    lgfx::ITouch::config_t cfg = _touchFt.config();
    cfg.x_min = 0;
    cfg.x_max = 319;
    cfg.y_min = 0;
    cfg.y_max = 479;
    cfg.pin_int = -1;  // 不依赖中断脚，直接轮询，避免中断电平不对时永远读不到触摸
    cfg.pin_rst = PIN_TOUCH_RST;
    cfg.bus_shared = false;
    cfg.offset_rotation = TOUCH_OFFSET_ROTATION;
    cfg.i2c_port = 1;
    cfg.pin_sda = s_touchSda;
    cfg.pin_scl = s_touchScl;
    cfg.freq = 400000;
    if (addr == 0x5D || addr == 0x14) {
      cfg.i2c_addr = addr;
      _touchGt.config(cfg);
      _panel.setTouch(&_touchGt);
    } else if (addr == 0x15) {
      cfg.i2c_addr = addr;
      _touchCst.config(cfg);
      _panel.setTouch(&_touchCst);
    } else if (addr > 0) {
      cfg.i2c_addr = addr;
      _touchFt.config(cfg);
      _panel.setTouch(&_touchFt);
    } else {
      // 电阻触摸 XPT2046：T_CLK=GPIO5，T_SDI=GPIO4，T_SDO=GPIO2，T_CS=GPIO1
      auto xpt = _touchXpt.config();
      xpt.x_min = 300;
      xpt.x_max = 3900;
      xpt.y_min = 400;
      xpt.y_max = 3900;
      xpt.pin_int = -1;
      xpt.pin_rst = -1;
      xpt.bus_shared = false;
      xpt.offset_rotation = TOUCH_OFFSET_ROTATION;
      xpt.spi_host = -1;
      xpt.pin_sclk = PIN_TOUCH_SCL;
      xpt.pin_mosi = PIN_TOUCH_SDA;
      xpt.pin_miso = PIN_TOUCH_MISO;
      xpt.pin_cs = PIN_TOUCH_RST;
      xpt.freq = 1000000;
      _touchXpt.config(xpt);
      _panel.setTouch(&_touchXpt);
      ESP_LOGE("touch", "XPT2046 clk=%d mosi=%d miso=%d cs=%d", PIN_TOUCH_SCL, PIN_TOUCH_SDA,
               PIN_TOUCH_MISO, PIN_TOUCH_RST);
    }
  }
};

static int scanBus(int sda, int scl, char* buf, size_t len) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  int pos = 0;
  int pick = -1;
  buf[0] = '\0';
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      pos += snprintf(buf + pos, len - pos, "%s0x%02X", pos ? "," : "", addr);
      if (pick < 0) pick = addr;
    }
  }
  Wire.end();
  return pick;
}

// 触摸芯片上电后扫 I2C。两种脚对调都试，杜邦线接反也能认出来
static int scanTouch() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(80);

  char buf[96];
  int pick = scanBus(PIN_TOUCH_SDA, PIN_TOUCH_SCL, buf, sizeof(buf));
  const char* how = "sda4/scl5";
  if (pick < 0) {
    pick = scanBus(PIN_TOUCH_SCL, PIN_TOUCH_SDA, buf, sizeof(buf));
    if (pick >= 0) {
      s_touchSda = PIN_TOUCH_SCL;
      s_touchScl = PIN_TOUCH_SDA;
      how = "swapped";
    }
  }
  ESP_LOGE("touch", "%s scan %s -> 0x%02X", how, pick < 0 ? "none" : buf, pick);
  return pick;
}

#if ENABLE_DISPLAY
static LGFX tft;
#endif

static constexpr int kVirtualW = 480;
static constexpr int kVirtualH = 320;

static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t s_dispDrv;
static lv_indev_drv_t s_indevDrv;
static uint8_t s_brightness = 200;

static uint16_t* s_fb;
static int s_fbW, s_fbH;
static volatile uint32_t s_fbFrame;

static volatile bool s_vtPressed;
static volatile bool s_vtPressLatch;  // 保证极短的点击也至少被 LVGL 读到一次按下
static volatile int16_t s_vtX, s_vtY;

static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
#if ENABLE_DISPLAY
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels((lgfx::rgb565_t*)&px->full, w * h);
  tft.endWrite();
#endif
  if (s_fb) {
    for (uint32_t row = 0; row < h; row++) {
      memcpy(&s_fb[(area->y1 + row) * s_fbW + area->x1], &px[row * w], w * sizeof(uint16_t));
    }
    s_fbFrame++;
  }
  lv_disp_flush_ready(drv);
}

static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
#if ENABLE_DISPLAY
  uint16_t x, y;
  static int16_t lastX = -1, lastY = -1;
  static uint32_t stillSince = 0;
  if (tft.getTouch(&x, &y)) {
    const bool still = lastX >= 0 && abs((int)x - lastX) < 8 && abs((int)y - lastY) < 8;
    if (!still) stillSince = millis();
    lastX = x;
    lastY = y;
    // 电阻屏悬空时会一直报同一个点，时间一长界面就被按住不动
    if (millis() - stillSince < 10000) {
      data->state = LV_INDEV_STATE_PR;
      data->point.x = x;
      data->point.y = y;
      return;
    }
  } else {
    lastX = -1;
  }
#endif
  const bool pressed = s_vtPressed || s_vtPressLatch;
  s_vtPressLatch = false;
  data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  data->point.x = s_vtX;
  data->point.y = s_vtY;
}

void displayBegin() {
#if ENABLE_DISPLAY
  tft.useTouch(scanTouch());
  tft.init();
  tft.setRotation(LCD_ROTATION);
  tft.setBrightness(s_brightness);
  tft.fillScreen(TFT_BLACK);
  const uint32_t w = tft.width();
  const uint32_t h = tft.height();
#else
  const uint32_t w = kVirtualW;
  const uint32_t h = kVirtualH;
#endif

  s_fbW = w;
  s_fbH = h;
  s_fb = (uint16_t*)heap_caps_calloc(w * h, sizeof(uint16_t), MALLOC_CAP_SPIRAM);

  lv_init();

  const uint32_t bufPixels = w * 8;  // 省内部内存，留给 WiFi 加密连接
  auto* buf1 = (lv_color_t*)heap_caps_malloc(bufPixels * sizeof(lv_color_t),
                                             MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  auto* buf2 = (lv_color_t*)heap_caps_malloc(bufPixels * sizeof(lv_color_t),
                                             MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_disp_draw_buf_init(&s_drawBuf, buf1, buf2, bufPixels);

  lv_disp_drv_init(&s_dispDrv);
  s_dispDrv.hor_res = w;
  s_dispDrv.ver_res = h;
  s_dispDrv.flush_cb = flushCb;
  s_dispDrv.draw_buf = &s_drawBuf;
  lv_disp_drv_register(&s_dispDrv);

  lv_indev_drv_init(&s_indevDrv);
  s_indevDrv.type = LV_INDEV_TYPE_POINTER;
  s_indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&s_indevDrv);
}

void displaySetBrightness(uint8_t value) {
  s_brightness = max<uint8_t>(value, 10);
#if ENABLE_DISPLAY
  tft.setBrightness(s_brightness);
#endif
}

uint8_t displayGetBrightness() { return s_brightness; }

void displayFramebuffer(const uint16_t** buf, int* w, int* h, uint32_t* frame) {
  *buf = s_fb;
  *w = s_fbW;
  *h = s_fbH;
  *frame = s_fbFrame;
}

void displayVirtualTouch(int x, int y, bool pressed) {
  s_vtX = constrain(x, 0, s_fbW - 1);
  s_vtY = constrain(y, 0, s_fbH - 1);
  if (pressed && !s_vtPressed) s_vtPressLatch = true;
  s_vtPressed = pressed;
}

