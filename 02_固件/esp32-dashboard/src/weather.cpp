#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "state.h"

static uint32_t s_nextFetchMs = 0;

const char* weatherText(int code) {
  switch (code) {
    case 0: return "晴";
    case 1: return "晴间多云";
    case 2: return "多云";
    case 3: return "阴";
    case 45: case 48: return "雾";
    case 51: case 53: case 55: return "毛毛雨";
    case 56: case 57: case 66: case 67: return "冻雨";
    case 61: return "小雨";
    case 63: return "中雨";
    case 65: return "大雨";
    case 71: return "小雪";
    case 73: return "中雪";
    case 75: return "大雪";
    case 77: return "雪粒";
    case 80: case 81: return "阵雨";
    case 82: return "强阵雨";
    case 85: case 86: return "阵雪";
    case 95: return "雷阵雨";
    case 96: case 99: return "雷阵雨伴冰雹";
    default: return "--";
  }
}

static bool fetchWeather(WeatherState& out) {
  const String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + WEATHER_LAT +
                     "&longitude=" + WEATHER_LON +
                     "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,"
                     "wind_speed_10m,wind_direction_10m,is_day"
                     "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
                     "&wind_speed_unit=ms&timezone=Asia%2FShanghai&forecast_days=3";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8);
  client.setHandshakeTimeout(8000);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    out.error = "天气请求失败";
    return false;
  }
  const int code = http.GET();
  if (code != 200) {
    out.error = "天气接口 HTTP " + String(code);
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();
  JsonDocument doc;
  const auto err = deserializeJson(doc, body);
  if (err) {
    out.error = "天气数据解析失败";
    return false;
  }

  JsonObjectConst cur = doc["current"];
  out.temp = cur["temperature_2m"] | 0.0f;
  out.feelsLike = cur["apparent_temperature"] | 0.0f;
  out.humidity = cur["relative_humidity_2m"] | 0;
  out.windSpeed = cur["wind_speed_10m"] | 0.0f;
  out.windDir = cur["wind_direction_10m"] | 0;
  out.code = cur["weather_code"] | -1;
  out.isDay = (cur["is_day"] | 1) == 1;

  JsonObjectConst daily = doc["daily"];
  for (int i = 0; i < 3; i++) {
    out.days[i].code = daily["weather_code"][i] | -1;
    out.days[i].tempMax = daily["temperature_2m_max"][i] | 0.0f;
    out.days[i].tempMin = daily["temperature_2m_min"][i] | 0.0f;
    out.days[i].rainProb = daily["precipitation_probability_max"][i] | 0;
  }
  return true;
}

void weatherLoop() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (s_nextFetchMs && (int32_t)(millis() - s_nextFetchMs) < 0) return;

  WeatherState next;
  {
    StateLock lock;
    next = g_weather;
  }
  next.error = "";
  const bool ok = fetchWeather(next);
  if (ok) {
    next.ok = true;
    next.lastUpdateMs = millis();
  }
  Serial.printf("[weather] ok=%d %.1fC code=%d %s\n", ok, next.temp, next.code, next.error.c_str());
  s_nextFetchMs = millis() + (ok ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS);

  StateLock lock;
  g_weather = next;
}
