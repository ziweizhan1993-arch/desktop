#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <lvgl.h>

#include "audio.h"
#include "bambu.h"
#include "config.h"
#include "cursor_usage.h"
#include "display.h"
#include "secrets.h"
#include "state.h"
#include "ui.h"
#include "weather.h"
#include "web.h"

PrinterState g_printers[kPrinterCount];
CursorState g_cursor;
WeatherState g_weather;
SemaphoreHandle_t g_stateMutex;

static void netTask(void*) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTzTime(TIMEZONE, "ntp.aliyun.com", "pool.ntp.org");
  bambuBegin();

  uint32_t lastWifiRetry = millis();
  for (;;) {
    if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetry > 15000) {
      lastWifiRetry = millis();
      Serial.println("[wifi] reconnecting");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    bambuLoop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// 天气和 Cursor 的 HTTPS 会占住调用它的任务。单独放一个任务，避免打印机连接被拖死。
static void fetchTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(8000));
  for (;;) {
    cursorLoop();
    weatherLoop();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void printStatus() {
  PrinterState printers[kPrinterCount];
  CursorState c;
  {
    StateLock lock;
    for (int i = 0; i < kPrinterCount; i++) printers[i] = g_printers[i];
    c = g_cursor;
  }
  Serial.printf("[wifi] %s  rssi=%d  preview: http://%s/\n",
                WiFi.status() == WL_CONNECTED ? "connected" : "offline", WiFi.RSSI(),
                WiFi.localIP().toString().c_str());
  for (const PrinterState& p : printers) {
    Serial.printf("[%s] mqtt=%s state=%s %d%% remain=%dmin layer=%d/%d nozzle=%.0f/%.0f bed=%.0f/%.0f job=%s\n",
                  p.name, p.connected ? "on" : "off", p.gcodeState.c_str(), p.percent, p.remainingMin, p.layer,
                  p.totalLayers, p.nozzleTemp, p.nozzleTarget, p.bedTemp, p.bedTarget, p.jobName.c_str());
  }
  if (c.requestMode) {
    Serial.printf("[cursor] ok=%d plan=%s %d/%d requests (%.0f%%) %s\n", c.ok, c.membership.c_str(),
                  c.requests, c.maxRequests, c.percent, c.error.c_str());
  } else {
    Serial.printf("[cursor] ok=%d plan=%s $%.2f/$%.2f (%.0f%%) on-demand=$%.2f %s\n", c.ok,
                  c.membership.c_str(), c.usedUsd, c.limitUsd, c.percent, c.onDemandUsd, c.error.c_str());
  }
  Serial.printf("[sys] volume=%d%% heap=%luKB psram=%luKB\n", audioGetVolume(),
                (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getFreePsram() / 1024));
}

static void handleSerial() {
  static String line;
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch != '\n' && ch != '\r') {
      line += ch;
      continue;
    }
    line.trim();
    if (line.isEmpty()) continue;

    if (line == "start") audioPlay(Sound::PrintStart);
    else if (line == "finish") audioPlay(Sound::PrintFinish);
    else if (line == "fail") audioPlay(Sound::PrintFailed);
    else if (line == "boot") audioPlay(Sound::Boot);
    else if (line == "status") printStatus();
    else if (line.startsWith("vol ")) {
      const int v = constrain(line.substring(4).toInt(), 0, 100);
      audioSetVolume(v);
      Preferences prefs;
      prefs.begin("dash", false);
      prefs.putUChar("vol", v);
      prefs.end();
      audioPlay(Sound::Click);
      Serial.printf("volume = %d%%\n", v);
    } else {
      Serial.println("commands: start | finish | fail | boot | status | vol <0-100>");
    }
    line = "";
  }
}

void setup() {
  Serial.begin(115200);
  g_stateMutex = xSemaphoreCreateMutex();

  Preferences prefs;
  prefs.begin("dash", false);
  uint8_t vol = prefs.getUChar("vol", 60);
  uint8_t bri = prefs.getUChar("bri", 200);
  // 假触摸会把滑条拖到最低并写入存储器，重启后屏幕就一直几乎全黑
  if (bri < 80) {
    bri = 200;
    prefs.putUChar("bri", bri);
  }
  if (vol < 30) {
    vol = 60;
    prefs.putUChar("vol", vol);
  }
  prefs.end();

  audioBegin();
  audioSetVolume(vol);
  displayBegin();
  displaySetBrightness(bri);
  uiBegin();
#if !ENABLE_DISPLAY
  Serial.println("no display attached, UI is rendered for web preview only; type 'help' for commands");
#endif

  xTaskCreatePinnedToCore(netTask, "net", 20480, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(fetchTask, "fetch", 32768, nullptr, 1, nullptr, 0);
  webBegin();
  audioPlay(Sound::Boot);
}

void loop() {
  handleSerial();
  lv_timer_handler();
#if !ENABLE_DISPLAY
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    lastStatus = millis();
    printStatus();
  }
#endif
  delay(5);
}
