#pragma once
#include <stdint.h>

void displayBegin();
void displaySetBrightness(uint8_t value);
uint8_t displayGetBrightness();

// LVGL 画面的 RGB565 镜像（PSRAM），供网页预览
void displayFramebuffer(const uint16_t** buf, int* w, int* h, uint32_t* frame);
void displayVirtualTouch(int x, int y, bool pressed);
