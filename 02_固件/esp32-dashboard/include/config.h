#pragma once

#define ENABLE_DISPLAY 1

// ---------------- 屏幕 ST7796 (SPI) ----------------
// GPIO 35/36/37 被 N16R8 的 OPI PSRAM 占用，19/20 是原生 USB，43/44 是串口，都不要用
#define PIN_LCD_SCLK 12
#define PIN_LCD_MOSI 11
#define PIN_LCD_MISO 13  // 屏幕没有 MISO/SDO 引脚时改成 -1
#define PIN_LCD_CS   10
#define PIN_LCD_DC   9
#define PIN_LCD_RST  14
#define PIN_LCD_BL   21

// 杜邦线在 40MHz 下不稳。8MHz 配合改驱动能力会把屏刷黑，保持 16MHz
#define LCD_SPI_FREQ   16000000
#define LCD_INVERT     true   // IPS 屏通常需要反色；颜色反了就改成 false
#define LCD_RGB_ORDER  false  // 红蓝颠倒就改成 true
#define LCD_ROTATION   1      // 1 / 3 为横屏 480x320

// ---------------- 触摸 (I2C) ----------------
// 二选一：FT6336/FT5x06 (地址 0x38) 或 GT911 (地址 0x5D / 0x14)
#define TOUCH_FT6336 1
#define TOUCH_GT911  0

#define PIN_TOUCH_SDA 4   // 电阻触摸时作 T_SDI (MOSI)
#define PIN_TOUCH_SCL 5   // 电阻触摸时作 T_CLK
#define PIN_TOUCH_INT 6
#define PIN_TOUCH_RST 1   // 电阻触摸时作 T_CS
#define PIN_TOUCH_MISO 2  // 转接板第 13 针 T_SDO
#define TOUCH_OFFSET_ROTATION 0  // 触摸方向不对时尝试 0~7

// ---------------- 功放 MAX98357A (I2S) ----------------
#define PIN_I2S_BCLK 15
#define PIN_I2S_LRC  16
#define PIN_I2S_DIN  7

// ---------------- 其他 ----------------
#define PRINTER_NAME "H2C"
#define PRINTER2_NAME "A1"
#define TIMEZONE "CST-8"
#define CURSOR_REFRESH_MS (5UL * 60 * 1000)
#define CURSOR_RETRY_MS   (60UL * 1000)

// 天气（Open-Meteo，免费无需 Key）
#define WEATHER_CITY "重庆"
#define WEATHER_LAT  "29.56"
#define WEATHER_LON  "106.55"
#define WEATHER_REFRESH_MS (15UL * 60 * 1000)
#define WEATHER_RETRY_MS   (60UL * 1000)
