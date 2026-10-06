#include "tls.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_mtx = nullptr;
static volatile uint32_t s_lastOkMs = 0;

void tlsInit() {
  if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
}

void tlsLock() {
  tlsInit();
  xSemaphoreTake(s_mtx, portMAX_DELAY);
}

void tlsUnlock() { xSemaphoreGive(s_mtx); }

void noteNetOk() { s_lastOkMs = millis(); }

uint32_t lastNetOkMs() { return s_lastOkMs; }

size_t tlsLargestBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }

bool tlsHeapOk() { return tlsLargestBlock() >= 40000; }
