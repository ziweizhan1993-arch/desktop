#include "cursor_usage.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "secrets.h"
#include "state.h"

// cursor.com 没有面向个人账号的公开 API，这里用的是网页仪表盘调用的内部接口，
// 依赖浏览器登录 Cookie，接口格式随时可能变化。

static uint32_t s_nextFetchMs = 0;

static time_t parseIso8601(const char* s) {
  int Y, M, D, h = 0, m = 0, sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 3) return 0;
  Y -= M <= 2;
  const int era = (Y >= 0 ? Y : Y - 399) / 400;
  const unsigned yoe = Y - era * 400;
  const unsigned doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = era * 146097L + (long)doe - 719468;
  return (time_t)days * 86400 + h * 3600 + m * 60 + sec;
}

static time_t parseTime(JsonVariantConst v) {
  if (v.is<const char*>()) return parseIso8601(v.as<const char*>());
  if (v.is<double>()) {
    double t = v.as<double>();
    if (t > 1e12) t /= 1000;
    return (time_t)t;
  }
  return 0;
}

static int httpGet(const String& url, String& body) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8);
  client.setHandshakeTimeout(8000);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return -1;
  http.addHeader("Cookie", String("WorkosCursorSessionToken=") + CURSOR_SESSION_TOKEN);
  http.addHeader("User-Agent",
                 "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                 "(KHTML, like Gecko) Chrome/126.0 Safari/537.36");
  http.addHeader("Accept", "application/json");
  http.addHeader("Referer", "https://cursor.com/dashboard");
  const int code = http.GET();
  if (code > 0) body = http.getString();
  http.end();
  return code;
}

static bool fetchSummary(CursorState& out) {
  String body;
  const int code = httpGet("https://cursor.com/api/usage-summary", body);
  if (code != 200) {
    out.error = "usage-summary HTTP " + String(code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    out.error = "usage-summary bad JSON";
    return false;
  }

  JsonObjectConst plan = doc["individualUsage"]["plan"];
  if (plan.isNull()) {
    out.error = "usage-summary: no plan";
    return false;
  }
  out.requestMode = false;
  out.membership = doc["membershipType"] | "";
  out.usedUsd = (plan["used"] | 0.0f) / 100.0f;
  out.limitUsd = (plan["limit"] | 0.0f) / 100.0f;
  if (plan["totalPercentUsed"].is<float>()) out.percent = plan["totalPercentUsed"];
  else out.percent = out.limitUsd > 0 ? out.usedUsd / out.limitUsd * 100.0f : 0;

  JsonObjectConst od = doc["individualUsage"]["onDemand"];
  out.onDemandEnabled = od["enabled"] | false;
  out.onDemandUsd = (od["used"] | 0.0f) / 100.0f;
  out.cycleEnd = parseTime(doc["billingCycleEnd"]);
  return true;
}

static bool fetchLegacy(CursorState& out) {
  String token = CURSOR_SESSION_TOKEN;
  int sep = token.indexOf("%3A%3A");
  if (sep < 0) sep = token.indexOf("::");
  if (sep <= 0) return false;

  String body;
  const int code = httpGet("https://cursor.com/api/usage?user=" + token.substring(0, sep), body);
  if (code != 200) {
    out.error += ", usage HTTP " + String(code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonObjectConst gpt4 = doc["gpt-4"];
  if (gpt4.isNull()) return false;

  out.requestMode = true;
  out.requests = gpt4["numRequests"] | 0;
  out.maxRequests = gpt4["maxRequestUsage"] | 0;
  out.percent = out.maxRequests > 0 ? 100.0f * out.requests / out.maxRequests : 0;
  const time_t start = parseTime(doc["startOfMonth"]);
  out.cycleEnd = start ? start + 30L * 86400 : 0;
  return true;
}

void cursorLoop() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (s_nextFetchMs && (int32_t)(millis() - s_nextFetchMs) < 0) return;

  CursorState next;
  {
    StateLock lock;
    next = g_cursor;
  }
  next.error = "";

  if (strlen(CURSOR_SESSION_TOKEN) == 0) {
    next.ok = false;
    next.error = "未设置 Cursor Cookie（secrets.h 中的 CURSOR_SESSION_TOKEN）";
    s_nextFetchMs = millis() + CURSOR_REFRESH_MS;
  } else {
    next.ok = fetchSummary(next) || fetchLegacy(next);
    if (next.ok) {
      next.error = "";
      next.lastUpdateMs = millis();
    }
    Serial.printf("[cursor] ok=%d %s\n", next.ok, next.error.c_str());
    s_nextFetchMs = millis() + (next.ok ? CURSOR_REFRESH_MS : CURSOR_RETRY_MS);
  }

  StateLock lock;
  g_cursor = next;
}
