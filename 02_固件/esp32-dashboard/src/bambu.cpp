#include "bambu.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <esp_log.h>

#include "audio.h"
#include "config.h"
#include "secrets.h"
#include "state.h"
#include "tls.h"
#include "ui.h"

struct PrinterConfig {
  const char* name;
  const char* ip;
  const char* serial;
  const char* accessCode;
  bool hasChamber;  // A1 等开放式机型上报的 chamber_temper 是无效值
};

static const PrinterConfig kConfigs[kPrinterCount] = {
    {PRINTER_NAME, BAMBU_IP, BAMBU_SERIAL, BAMBU_ACCESS_CODE, true},
    {PRINTER2_NAME, BAMBU2_IP, BAMBU2_SERIAL, BAMBU2_ACCESS_CODE, false},
};

struct PrinterConn {
  int idx = 0;
  WiFiClientSecure net;
  PubSubClient mqtt{net};
  String reportTopic, requestTopic;
  String prevState;
  bool startPlayed = false;
  uint32_t lastAttemptMs = 0;
  uint32_t retryMs = 0;
  uint32_t lastPushallMs = 0;
};

static PrinterConn s_conns[kPrinterCount];
static JsonDocument s_filter;
static volatile bool s_connecting = false;

static constexpr uint32_t kReconnectMinMs = 5000;
static constexpr uint32_t kReconnectMaxMs = 60000;
static constexpr uint32_t kPushallMs = 10UL * 60 * 1000;

static void setConnected(PrinterConn& c, bool connected) {
  StateLock lock;
  g_printers[c.idx].connected = connected;
}

static void requestPushall(PrinterConn& c) {
  c.mqtt.publish(c.requestTopic.c_str(), R"({"pushing":{"sequence_id":"0","command":"pushall"}})");
  c.lastPushallMs = millis();
}

// H2 系列把温度打包成 uint32：低 16 位当前值，高 16 位目标值
static void unpackTemp(JsonVariantConst v, float& cur, float* target) {
  const uint32_t raw = v.as<uint32_t>();
  cur = raw & 0xFFFF;
  if (target) *target = (raw >> 16) & 0xFFFF;
}

static void handleStateChange(PrinterConn& c, const String& next) {
  if (next == c.prevState) return;
  const String prev = c.prevState;
  c.prevState = next;
  Serial.printf("[bambu:%s] state %s -> %s\n", kConfigs[c.idx].name, prev.c_str(), next.c_str());
  if (prev.isEmpty()) {
    c.startPlayed = next == "PREPARE" || next == "RUNNING" || next == "PAUSE";
    return;
  }

  if ((next == "PREPARE" || next == "RUNNING") && !c.startPlayed) {
    audioPlay(Sound::PrintStart);
    c.startPlayed = true;
  } else if (next == "FINISH") {
    if (c.startPlayed) audioPlay(Sound::PrintFinish);
    c.startPlayed = false;
  } else if (next == "FAILED") {
    audioPlay(Sound::PrintFailed);
    c.startPlayed = false;
  } else if (next == "IDLE") {
    c.startPlayed = false;
  }
}

static void onMessage(PrinterConn& c, byte* payload, unsigned int len) {
  JsonDocument doc;
  const auto err = deserializeJson(doc, payload, len, DeserializationOption::Filter(s_filter));
  if (err) {
    Serial.printf("[bambu:%s] json error: %s (len=%u)\n", kConfigs[c.idx].name, err.c_str(), len);
    return;
  }
  JsonObjectConst p = doc["print"];
  if (p.isNull()) return;

  String newState;
  {
    StateLock lock;
    PrinterState& s = g_printers[c.idx];
    s.hasData = true;
    s.lastUpdateMs = millis();

    if (p["gcode_state"].is<const char*>()) {
      s.gcodeState = p["gcode_state"].as<const char*>();
      newState = s.gcodeState;
    }
    if (p["mc_percent"].is<int>()) s.percent = p["mc_percent"];
    if (p["mc_remaining_time"].is<int>()) s.remainingMin = p["mc_remaining_time"];
    if (p["layer_num"].is<int>()) s.layer = p["layer_num"];
    if (p["total_layer_num"].is<int>()) s.totalLayers = p["total_layer_num"];
    if (p["subtask_name"].is<const char*>()) s.jobName = p["subtask_name"].as<const char*>();

    if (p["nozzle_temper"].is<float>()) s.nozzleTemp = p["nozzle_temper"];
    if (p["nozzle_target_temper"].is<float>()) s.nozzleTarget = p["nozzle_target_temper"];
    if (p["bed_temper"].is<float>()) s.bedTemp = p["bed_temper"];
    if (p["bed_target_temper"].is<float>()) s.bedTarget = p["bed_target_temper"];
    if (p["chamber_temper"].is<float>()) s.chamberTemp = p["chamber_temper"];

    JsonObjectConst dev = p["device"];
    if (!dev.isNull()) {
      JsonArrayConst ext = dev["extruder"]["info"];
      if (ext.size() > 0 && !ext[0]["temp"].isNull()) unpackTemp(ext[0]["temp"], s.nozzleTemp, &s.nozzleTarget);
      if (ext.size() > 1 && !ext[1]["temp"].isNull()) unpackTemp(ext[1]["temp"], s.nozzle2Temp, nullptr);
      if (!dev["bed"]["info"]["temp"].isNull()) unpackTemp(dev["bed"]["info"]["temp"], s.bedTemp, &s.bedTarget);
      if (!dev["ctc"]["info"]["temp"].isNull()) unpackTemp(dev["ctc"]["info"]["temp"], s.chamberTemp, nullptr);
    }
  }

  if (newState.length()) handleStateChange(c, newState);
}

void bambuBegin() {
  JsonObject f = s_filter["print"].to<JsonObject>();
  for (const char* key : {"gcode_state", "mc_percent", "mc_remaining_time", "layer_num",
                          "total_layer_num", "subtask_name", "nozzle_temper",
                          "nozzle_target_temper", "bed_temper", "bed_target_temper",
                          "chamber_temper"}) {
    f[key] = true;
  }
  f["device"]["extruder"]["info"][0]["temp"] = true;
  f["device"]["bed"]["info"]["temp"] = true;
  f["device"]["ctc"]["info"]["temp"] = true;

  for (int i = 0; i < kPrinterCount; i++) {
    PrinterConn& c = s_conns[i];
    const PrinterConfig& cfg = kConfigs[i];
    c.idx = i;
    c.reportTopic = String("device/") + cfg.serial + "/report";
    c.requestTopic = String("device/") + cfg.serial + "/request";
    {
      StateLock lock;
      g_printers[i].name = cfg.name;
      g_printers[i].ip = cfg.ip;
      g_printers[i].hasChamber = cfg.hasChamber;
    }

    c.net.setInsecure();  // 打印机使用自签名证书
    // setTimeout / setHandshakeTimeout 的单位是秒，库内部会再乘 1000。
    // 之前传了 4000，实际变成约 66 分钟。一台握手卡住时，另一台和网页请求都会跟着失败。
    c.net.setTimeout(12);
    c.net.setHandshakeTimeout(10);
    c.mqtt.setServer(cfg.ip, 8883);
    c.mqtt.setBufferSize(60000);  // pushall 完整报文可达数十 KB，分配在 PSRAM
    c.mqtt.setKeepAlive(30);
    c.mqtt.setSocketTimeout(10);
    c.mqtt.setCallback([&c](char*, byte* payload, unsigned int len) { onMessage(c, payload, len); });
  }
}

static void loopOne(PrinterConn& c) {
  const PrinterConfig& cfg = kConfigs[c.idx];
  if (uiPage() != c.idx) {
    if (c.mqtt.connected()) c.mqtt.disconnect();
    setConnected(c, false);
    return;
  }
  if (!c.mqtt.connected()) {
    setConnected(c, false);
    if (c.lastAttemptMs && millis() - c.lastAttemptMs < c.retryMs) return;
    c.lastAttemptMs = millis();

    const String clientId = "esp32dash-" + String((uint32_t)ESP.getEfuseMac(), HEX) + "-" + c.idx;
    ESP_LOGE("bambu", "%s -> %s heap=%u largest=%u", cfg.name, cfg.ip, ESP.getFreeHeap(),
             (unsigned)tlsLargestBlock());
    Serial.printf("[bambu:%s] connecting to %s ...\n", cfg.name, cfg.ip);
    bool ok = false;
    s_connecting = true;
    {
      TlsLock tls;
      ok = c.mqtt.connect(clientId.c_str(), "bblp", cfg.accessCode);
    }
    s_connecting = false;
    if (uiPage() != c.idx) {
      if (c.mqtt.connected()) c.mqtt.disconnect();
      setConnected(c, false);
      return;
    }
    if (ok) {
      noteNetOk();
      ESP_LOGE("bambu", "%s connected", cfg.name);
      Serial.printf("[bambu:%s] connected\n", cfg.name);
      c.mqtt.subscribe(c.reportTopic.c_str());
      requestPushall(c);
      setConnected(c, true);
      {
        StateLock lock;
        g_printers[c.idx].failCode = 0;
      }
      c.retryMs = kReconnectMinMs;
    } else {
      c.retryMs = constrain(c.retryMs * 2, kReconnectMinMs, kReconnectMaxMs);
      const int rc = c.mqtt.state();
      {
        StateLock lock;
        g_printers[c.idx].failCode = rc;
      }
      ESP_LOGE("bambu", "%s failed rc=%d heap=%u largest=%u", cfg.name, rc, ESP.getFreeHeap(),
               (unsigned)tlsLargestBlock());
      Serial.printf("[bambu:%s] connect failed, rc=%d, retry in %lus\n", cfg.name, rc,
                    (unsigned long)(c.retryMs / 1000));
    }
    return;
  }

  c.mqtt.loop();
  if (millis() - c.lastPushallMs > kPushallMs) requestPushall(c);
}

void bambuLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    for (auto& c : s_conns) setConnected(c, false);
    return;
  }
  const int page = uiPage();
  for (auto& c : s_conns) {
    if (page != c.idx) {
      if (c.mqtt.connected()) {
        c.mqtt.disconnect();
        setConnected(c, false);
      }
      continue;
    }
    loopOne(c);
    if (c.mqtt.connected()) noteNetOk();
  }
}

bool bambuBusy() {
  if (s_connecting) return true;
  for (auto& c : s_conns)
    if (c.mqtt.connected()) return true;
  return false;
}
