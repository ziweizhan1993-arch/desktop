#pragma once
#include <Arduino.h>
#include <math.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

constexpr int kPrinterCount = 2;

struct PrinterState {
  const char* name = "";
  const char* ip = "";
  bool hasChamber = true;
  bool connected = false;
  bool hasData = false;
  int failCode = 0;  // MQTT 失败码，0 表示还没失败过
  String gcodeState = "UNKNOWN";
  int percent = 0;
  int remainingMin = 0;
  int layer = 0;
  int totalLayers = 0;
  float nozzleTemp = 0, nozzleTarget = 0;
  float nozzle2Temp = NAN;
  float bedTemp = 0, bedTarget = 0;
  float chamberTemp = NAN;
  String jobName;
  uint32_t lastUpdateMs = 0;
};

struct CursorState {
  bool ok = false;
  bool requestMode = false;  // 旧版按请求次数计费
  String error;
  String membership;
  float usedUsd = 0, limitUsd = 0;
  float percent = 0;
  bool onDemandEnabled = false;
  float onDemandUsd = 0;
  int requests = 0, maxRequests = 0;
  time_t cycleEnd = 0;
  uint32_t lastUpdateMs = 0;
};

struct WeatherDay {
  int code = -1;
  float tempMax = 0, tempMin = 0;
  int rainProb = 0;
};

struct WeatherState {
  bool ok = false;
  String error;
  float temp = 0, feelsLike = 0;
  int humidity = 0;
  float windSpeed = 0;
  int windDir = 0;
  int code = -1;
  bool isDay = true;
  WeatherDay days[3];
  uint32_t lastUpdateMs = 0;
};

extern PrinterState g_printers[kPrinterCount];
extern CursorState g_cursor;
extern WeatherState g_weather;
extern SemaphoreHandle_t g_stateMutex;

struct StateLock {
  StateLock() { xSemaphoreTake(g_stateMutex, portMAX_DELAY); }
  ~StateLock() { xSemaphoreGive(g_stateMutex); }
};
