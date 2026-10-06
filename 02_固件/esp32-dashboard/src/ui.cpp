#include "ui.h"

#include <Preferences.h>
#include <WiFi.h>
#include <lvgl.h>
#include <time.h>

#include "audio.h"
#include "config.h"
#include "display.h"
#include "secrets.h"
#include "state.h"
#include "weather.h"

#define C_BG lv_color_hex(0x0E1014)
#define C_CARD lv_color_hex(0x1A1D24)
#define C_TEXT lv_color_hex(0xF2F4F8)
#define C_DIM lv_color_hex(0x8A909C)
#define C_GREEN lv_color_hex(0x00AE42)
#define C_BLUE lv_color_hex(0x4C8DFF)
#define C_ORANGE lv_color_hex(0xFF9F1C)
#define C_RED lv_color_hex(0xFF4D4F)
#define C_YELLOW lv_color_hex(0xF5C542)
#define C_CURSOR lv_color_hex(0xA78BFA)

LV_FONT_DECLARE(font_cn_14);
LV_FONT_DECLARE(font_cn_20);

static constexpr int kPages = kPrinterCount + 3;
static lv_obj_t* s_dots[kPages];
static volatile int s_page = 0;

struct PrinterPage {
  lv_obj_t *badge, *arc, *pct, *remain, *eta, *job;
  lv_obj_t *layer, *nozzle, *bed, *chamber, *conn;
};
static PrinterPage s_printerPages[kPrinterCount];
static const char* const kPrinterNames[kPrinterCount] = {PRINTER_NAME, PRINTER2_NAME};

static lv_obj_t *c_badge, *c_pct, *c_amount, *c_bar, *c_ondemand, *c_reset, *c_status;

static lv_obj_t *s_wifi, *s_ip, *s_uptime, *s_mem, *s_volLbl, *s_briLbl;

static lv_obj_t *w_badge, *w_icon, *w_temp, *w_desc, *w_feels, *w_hum, *w_wind, *w_rain, *w_status;
static lv_obj_t *w_dayIcon[3], *w_dayTemp[3], *w_dayDesc[3];

// ---------------------------------------------------------------- helpers

static lv_obj_t* mkLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color,
                         const char* txt = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, txt);
  return l;
}

static lv_obj_t* mkCard(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, C_CARD, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, 12, 0);
  lv_obj_set_style_pad_all(c, 12, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

static lv_obj_t* mkRow(lv_obj_t* card, int y, const char* key) {
  lv_obj_t* k = mkLabel(card, &font_cn_14, C_DIM, key);
  lv_obj_set_pos(k, 0, y + 3);
  lv_obj_t* v = mkLabel(card, &font_cn_20, C_TEXT, "--");
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, y);
  return v;
}

static lv_obj_t* mkBadge(lv_obj_t* parent) {
  lv_obj_t* b = mkLabel(parent, &font_cn_14, C_BG, "--");
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, C_DIM, 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 4, 0);
  lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -16, 12);
  return b;
}

static void setBadge(lv_obj_t* b, const char* text, lv_color_t color) {
  lv_label_set_text(b, text);
  lv_obj_set_style_bg_color(b, color, 0);
}

static bool timeValid() { return time(nullptr) > 1700000000; }

// ---------------------------------------------------------------- pages

static void buildPrinterPage(lv_obj_t* t, PrinterPage& pg, const char* name) {
  lv_obj_t*& p_badge = pg.badge;
  lv_obj_t*& p_arc = pg.arc;
  lv_obj_t*& p_pct = pg.pct;
  lv_obj_t*& p_remain = pg.remain;
  lv_obj_t*& p_eta = pg.eta;
  lv_obj_t*& p_job = pg.job;
  lv_obj_t*& p_layer = pg.layer;
  lv_obj_t*& p_nozzle = pg.nozzle;
  lv_obj_t*& p_bed = pg.bed;
  lv_obj_t*& p_chamber = pg.chamber;
  lv_obj_t*& p_conn = pg.conn;

  lv_obj_t* title = mkLabel(t, &font_cn_20, C_TEXT, "");
  lv_label_set_text_fmt(title, "拓竹 %s", name);
  lv_obj_set_pos(title, 16, 12);
  p_badge = mkBadge(t);

  p_arc = lv_arc_create(t);
  lv_obj_set_size(p_arc, 180, 180);
  lv_obj_set_pos(p_arc, 16, 48);
  lv_arc_set_rotation(p_arc, 135);
  lv_arc_set_bg_angles(p_arc, 0, 270);
  lv_arc_set_range(p_arc, 0, 100);
  lv_arc_set_value(p_arc, 0);
  lv_obj_remove_style(p_arc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(p_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(p_arc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(p_arc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(p_arc, C_CARD, LV_PART_MAIN);
  lv_obj_set_style_arc_color(p_arc, C_GREEN, LV_PART_INDICATOR);

  p_pct = mkLabel(p_arc, &lv_font_montserrat_48, C_TEXT, "--");
  lv_obj_align(p_pct, LV_ALIGN_CENTER, 0, -8);
  p_remain = mkLabel(p_arc, &font_cn_14, C_DIM, "");
  lv_obj_align(p_remain, LV_ALIGN_CENTER, 0, 32);

  p_eta = mkLabel(t, &font_cn_20, C_TEXT, "");
  lv_obj_set_width(p_eta, 180);
  lv_obj_set_style_text_align(p_eta, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(p_eta, 16, 232);

  p_job = mkLabel(t, &font_cn_20, C_TEXT, "");
  lv_label_set_long_mode(p_job, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(p_job, 252);
  lv_obj_set_pos(p_job, 212, 52);

  lv_obj_t* card = mkCard(t, 212, 86, 252, 176);
  p_layer = mkRow(card, 0, "层数");
  p_nozzle = mkRow(card, 38, "喷嘴");
  p_bed = mkRow(card, 76, "热床");
  p_chamber = mkRow(card, 114, "仓温");

  p_conn = mkLabel(t, &font_cn_14, C_DIM, "");
  lv_obj_set_pos(p_conn, 16, 272);
}

static void buildCursorPage(lv_obj_t* t) {
  mkLabel(t, &font_cn_20, C_TEXT, "Cursor 用量");
  lv_obj_set_pos(lv_obj_get_child(t, -1), 16, 12);
  c_badge = mkBadge(t);

  c_pct = mkLabel(t, &lv_font_montserrat_48, C_CURSOR, "--");
  lv_obj_set_pos(c_pct, 16, 50);
  c_amount = mkLabel(t, &font_cn_20, C_TEXT, "");
  lv_obj_set_pos(c_amount, 16, 108);

  c_bar = lv_bar_create(t);
  lv_obj_set_pos(c_bar, 16, 140);
  lv_obj_set_size(c_bar, 448, 18);
  lv_bar_set_range(c_bar, 0, 100);
  lv_obj_set_style_bg_color(c_bar, C_CARD, LV_PART_MAIN);
  lv_obj_set_style_bg_color(c_bar, C_CURSOR, LV_PART_INDICATOR);

  lv_obj_t* card = mkCard(t, 16, 172, 448, 92);
  c_ondemand = mkRow(card, 0, "超额用量");
  c_reset = mkRow(card, 38, "距离重置");

  c_status = mkLabel(t, &font_cn_14, C_DIM, "加载中...");
  lv_label_set_long_mode(c_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(c_status, 448);
  lv_obj_set_pos(c_status, 16, 272);
}

// ---------------------------------------------------------------- weather

static lv_obj_t* mkShape(lv_obj_t* parent, float x, float y, float w, float h, int s, lv_color_t color,
                         bool round = true) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, (int)(x * s), (int)(y * s));
  lv_obj_set_size(o, (int)(w * s), (int)(h * s));
  lv_obj_set_style_radius(o, round ? LV_RADIUS_CIRCLE : (int)(h * s / 2), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, color, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static void drawCloud(lv_obj_t* box, int s, float dy, lv_color_t color) {
  mkShape(box, 0.12f, 0.40f + dy, 0.36f, 0.36f, s, color);
  mkShape(box, 0.30f, 0.22f + dy, 0.46f, 0.46f, s, color);
  mkShape(box, 0.52f, 0.34f + dy, 0.36f, 0.36f, s, color);
  mkShape(box, 0.12f, 0.52f + dy, 0.76f, 0.24f, s, color, false);
}

static void drawWeatherIcon(lv_obj_t* box, int s, int code, bool day, lv_color_t bg) {
  const intptr_t key = code * 2 + (day ? 1 : 0) + 1000;
  if ((intptr_t)lv_obj_get_user_data(box) == key) return;
  lv_obj_set_user_data(box, (void*)key);
  lv_obj_clean(box);

  const lv_color_t sun = lv_color_hex(0xF5C542);
  const lv_color_t cloud = lv_color_hex(0xC8CDD6);
  if (code == 0) {
    if (day) {
      mkShape(box, 0.20f, 0.20f, 0.60f, 0.60f, s, sun);
    } else {
      mkShape(box, 0.20f, 0.20f, 0.60f, 0.60f, s, lv_color_hex(0xE8EAF0));
      mkShape(box, 0.38f, 0.10f, 0.56f, 0.56f, s, bg);
    }
  } else if (code == 1 || code == 2) {
    mkShape(box, 0.42f, 0.08f, 0.46f, 0.46f, s, day ? sun : lv_color_hex(0xE8EAF0));
    drawCloud(box, s, 0.10f, cloud);
  } else if (code == 45 || code == 48) {
    for (int i = 0; i < 3; i++) mkShape(box, 0.14f, 0.30f + i * 0.16f, 0.72f, 0.09f, s, cloud, false);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    drawCloud(box, s, -0.12f, cloud);
    for (int i = 0; i < 3; i++) mkShape(box, 0.24f + i * 0.20f, 0.74f, 0.11f, 0.11f, s, lv_color_hex(0xFFFFFF));
  } else if (code >= 95) {
    drawCloud(box, s, -0.12f, lv_color_hex(0x9AA0AC));
    lv_obj_t* bolt = mkLabel(box, s >= 60 ? &lv_font_montserrat_28 : &lv_font_montserrat_14, sun, LV_SYMBOL_CHARGE);
    lv_obj_align(bolt, LV_ALIGN_BOTTOM_MID, 0, 0);
  } else if (code >= 51) {
    drawCloud(box, s, -0.12f, cloud);
    for (int i = 0; i < 3; i++) mkShape(box, 0.28f + i * 0.18f, 0.70f, 0.07f, 0.20f, s, C_BLUE, false);
  } else {
    drawCloud(box, s, 0, cloud);
  }
}

static lv_obj_t* mkIconBox(lv_obj_t* parent, int x, int y, int s) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_pos(box, x, y);
  lv_obj_set_size(box, s, s);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
  return box;
}

static void buildWeatherPage(lv_obj_t* t) {
  mkLabel(t, &font_cn_20, C_TEXT, "天气 · " WEATHER_CITY);
  lv_obj_set_pos(lv_obj_get_child(t, -1), 16, 12);
  w_badge = mkBadge(t);

  w_icon = mkIconBox(t, 16, 50, 96);
  w_temp = mkLabel(t, &lv_font_montserrat_48, C_TEXT, "--");
  lv_obj_set_pos(w_temp, 124, 46);
  w_desc = mkLabel(t, &font_cn_20, C_TEXT, "");
  lv_obj_set_pos(w_desc, 124, 104);
  w_feels = mkLabel(t, &font_cn_14, C_DIM, "");
  lv_obj_set_pos(w_feels, 124, 132);

  lv_obj_t* card = mkCard(t, 280, 48, 184, 116);
  w_hum = mkRow(card, 0, "湿度");
  w_wind = mkRow(card, 32, "风");
  w_rain = mkRow(card, 64, "降水");

  static const char* kDays[3] = {"今天", "明天", "后天"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t* c = mkCard(t, 16 + i * 152, 176, 144, 88);
    mkLabel(c, &font_cn_14, C_DIM, kDays[i]);
    w_dayIcon[i] = mkIconBox(c, 84, -4, 36);
    w_dayTemp[i] = mkLabel(c, &font_cn_20, C_TEXT, "--");
    lv_obj_set_pos(w_dayTemp[i], 0, 22);
    w_dayDesc[i] = mkLabel(c, &font_cn_14, C_DIM, "");
    lv_obj_set_pos(w_dayDesc[i], 0, 48);
  }

  w_status = mkLabel(t, &font_cn_14, C_DIM, "加载中...");
  lv_obj_set_pos(w_status, 16, 272);
}

static void updateWeather(const WeatherState& w) {
  if (w.ok) {
    drawWeatherIcon(w_icon, 96, w.code, w.isDay, C_BG);
    setBadge(w_badge, weatherText(w.code), C_BLUE);
    lv_label_set_text_fmt(w_temp, "%d°", (int)roundf(w.temp));
    lv_label_set_text(w_desc, weatherText(w.code));
    lv_label_set_text_fmt(w_feels, "体感 %d°", (int)roundf(w.feelsLike));
    lv_label_set_text_fmt(w_hum, "%d%%", w.humidity);
    static const char* kDirs[8] = {"北", "东北", "东", "东南", "南", "西南", "西", "西北"};
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %.1fm/s", kDirs[((w.windDir + 22) % 360) / 45], w.windSpeed);
    lv_label_set_text(w_wind, buf);
    lv_label_set_text_fmt(w_rain, "%d%%", w.days[0].rainProb);

    for (int i = 0; i < 3; i++) {
      const WeatherDay& d = w.days[i];
      drawWeatherIcon(w_dayIcon[i], 36, d.code, true, C_CARD);
      lv_label_set_text_fmt(w_dayTemp[i], "%d° / %d°", (int)roundf(d.tempMax), (int)roundf(d.tempMin));
      lv_label_set_text_fmt(w_dayDesc[i], "%s  %d%%", weatherText(d.code), d.rainProb);
    }
  }

  if (w.error.length()) {
    lv_obj_set_style_text_color(w_status, C_RED, 0);
    lv_label_set_text_fmt(w_status, LV_SYMBOL_WARNING " %s", w.error.c_str());
  } else if (w.lastUpdateMs) {
    lv_obj_set_style_text_color(w_status, C_DIM, 0);
    lv_label_set_text_fmt(w_status, LV_SYMBOL_OK " 已更新 · %lu 分钟前 · Open-Meteo",
                          (unsigned long)((millis() - w.lastUpdateMs) / 60000));
  }
}

static void onVolume(lv_event_t* e) {
  const int v = lv_slider_get_value(lv_event_get_target(e));
  audioSetVolume(v);
  lv_label_set_text_fmt(s_volLbl, LV_SYMBOL_VOLUME_MAX "  音量 %d%%", v);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    Preferences prefs;
    prefs.begin("dash", false);
    prefs.putUChar("vol", v);
    prefs.end();
    audioPlay(Sound::Click);
  }
}

static void onBrightness(lv_event_t* e) {
  const int v = lv_slider_get_value(lv_event_get_target(e));
  displaySetBrightness(v);
  lv_label_set_text_fmt(s_briLbl, LV_SYMBOL_EYE_OPEN "  亮度 %d%%", v * 100 / 255);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    Preferences prefs;
    prefs.begin("dash", false);
    prefs.putUChar("bri", v);
    prefs.end();
  }
}

static void onTestSound(lv_event_t* e) {
  audioPlay((Sound)(uintptr_t)lv_event_get_user_data(e));
}

static lv_obj_t* mkSlider(lv_obj_t* parent, int y, int min, int max, int value, lv_event_cb_t cb) {
  lv_obj_t* s = lv_slider_create(parent);
  lv_obj_set_pos(s, 256, y);
  lv_obj_set_width(s, 200);
  lv_slider_set_range(s, min, max);
  lv_slider_set_value(s, value, LV_ANIM_OFF);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s, cb, LV_EVENT_RELEASED, nullptr);
  lv_event_send(s, LV_EVENT_VALUE_CHANGED, nullptr);
  return s;
}

static void mkSoundBtn(lv_obj_t* parent, int x, const char* text, Sound sound, lv_color_t color) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_pos(b, x, 214);
  lv_obj_set_size(b, 140, 44);
  lv_obj_set_style_bg_color(b, color, 0);
  lv_obj_add_event_cb(b, onTestSound, LV_EVENT_CLICKED, (void*)(uintptr_t)sound);
  lv_obj_t* l = mkLabel(b, &font_cn_14, C_TEXT, text);
  lv_obj_center(l);
}

static void buildSystemPage(lv_obj_t* t) {
  mkLabel(t, &font_cn_20, C_TEXT, "系统");
  lv_obj_set_pos(lv_obj_get_child(t, -1), 16, 12);

  lv_obj_t* card = mkCard(t, 16, 48, 224, 152);
  s_wifi = mkRow(card, 0, "WiFi");
  s_ip = mkRow(card, 32, "IP");
  s_uptime = mkRow(card, 64, "运行");
  s_mem = mkRow(card, 96, "内存");

  s_volLbl = mkLabel(t, &font_cn_14, C_TEXT, "");
  lv_obj_set_pos(s_volLbl, 256, 56);
  s_briLbl = mkLabel(t, &font_cn_14, C_TEXT, "");
  lv_obj_set_pos(s_briLbl, 256, 126);
  mkSlider(t, 86, 0, 100, audioGetVolume(), onVolume);
  mkSlider(t, 156, 10, 255, displayGetBrightness(), onBrightness);

  mkSoundBtn(t, 16, LV_SYMBOL_PLAY "  开始音", Sound::PrintStart, C_GREEN);
  mkSoundBtn(t, 170, LV_SYMBOL_OK "  完成音", Sound::PrintFinish, C_BLUE);
  mkSoundBtn(t, 324, LV_SYMBOL_WARNING "  失败音", Sound::PrintFailed, C_RED);
}

// ---------------------------------------------------------------- updates

static lv_color_t stateColor(const String& s) {
  if (s == "RUNNING") return C_GREEN;
  if (s == "PREPARE" || s == "SLICING") return C_YELLOW;
  if (s == "PAUSE") return C_ORANGE;
  if (s == "FAILED") return C_RED;
  if (s == "FINISH") return C_BLUE;
  return C_DIM;
}

static const char* stateText(const String& s) {
  if (s == "RUNNING") return "打印中";
  if (s == "PREPARE") return "准备中";
  if (s == "SLICING") return "切片中";
  if (s == "PAUSE") return "已暂停";
  if (s == "FAILED") return "打印失败";
  if (s == "FINISH") return "已完成";
  if (s == "IDLE") return "空闲";
  return "未知";
}

static void updatePrinter(const PrinterPage& pg, const PrinterState& p) {
  lv_obj_t* p_badge = pg.badge;
  lv_obj_t* p_arc = pg.arc;
  lv_obj_t* p_pct = pg.pct;
  lv_obj_t* p_remain = pg.remain;
  lv_obj_t* p_eta = pg.eta;
  lv_obj_t* p_job = pg.job;
  lv_obj_t* p_layer = pg.layer;
  lv_obj_t* p_nozzle = pg.nozzle;
  lv_obj_t* p_bed = pg.bed;
  lv_obj_t* p_chamber = pg.chamber;
  lv_obj_t* p_conn = pg.conn;

  const bool active = p.gcodeState == "RUNNING" || p.gcodeState == "PAUSE" || p.gcodeState == "PREPARE";

  if (!p.connected) setBadge(p_badge, "离线", C_DIM);
  else setBadge(p_badge, stateText(p.gcodeState), stateColor(p.gcodeState));

  lv_obj_set_style_arc_color(p_arc, stateColor(p.gcodeState), LV_PART_INDICATOR);
  lv_arc_set_value(p_arc, p.hasData ? p.percent : 0);
  if (p.hasData) lv_label_set_text_fmt(p_pct, "%d%%", p.percent);
  else lv_label_set_text(p_pct, "--");

  if (active && p.remainingMin > 0) {
    lv_label_set_text_fmt(p_remain, "剩余 %d小时%02d分", p.remainingMin / 60, p.remainingMin % 60);
    if (timeValid()) {
      const time_t eta = time(nullptr) + p.remainingMin * 60;
      struct tm tmEta, tmNow;
      localtime_r(&eta, &tmEta);
      const time_t now = time(nullptr);
      localtime_r(&now, &tmNow);
      lv_label_set_text_fmt(p_eta, "预计%s %02d:%02d 完成", tmEta.tm_yday != tmNow.tm_yday ? "明天" : "",
                            tmEta.tm_hour, tmEta.tm_min);
    } else {
      lv_label_set_text(p_eta, "");
    }
  } else {
    lv_label_set_text(p_remain, "");
    lv_label_set_text(p_eta, "");
  }

  lv_label_set_text(p_job, p.jobName.length() ? p.jobName.c_str() : "暂无打印任务");
  if (p.totalLayers > 0) lv_label_set_text_fmt(p_layer, "%d / %d", p.layer, p.totalLayers);
  else lv_label_set_text(p_layer, "--");

  char buf[48];
  if (!isnan(p.nozzle2Temp)) snprintf(buf, sizeof(buf), "%.0f° | %.0f°", p.nozzleTemp, p.nozzle2Temp);
  else snprintf(buf, sizeof(buf), "%.0f / %.0f°C", p.nozzleTemp, p.nozzleTarget);
  lv_label_set_text(p_nozzle, buf);
  snprintf(buf, sizeof(buf), "%.0f / %.0f°C", p.bedTemp, p.bedTarget);
  lv_label_set_text(p_bed, buf);
  if (!p.hasChamber || isnan(p.chamberTemp)) lv_label_set_text(p_chamber, "--");
  else lv_label_set_text_fmt(p_chamber, "%d°C", (int)p.chamberTemp);

  if (p.connected) {
    lv_label_set_text_fmt(p_conn, LV_SYMBOL_OK " 已连接打印机 · %lu 秒前更新",
                          (unsigned long)((millis() - p.lastUpdateMs) / 1000));
  } else if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(p_conn, LV_SYMBOL_WARNING " WiFi 未连接");
  } else if (p.failCode == -2) {
    lv_label_set_text(p_conn, LV_SYMBOL_WARNING " 连不上打印机");
  } else if (p.failCode == -4) {
    lv_label_set_text(p_conn, LV_SYMBOL_WARNING " 打印机无响应");
  } else if (p.failCode == 4 || p.failCode == 5) {
    lv_label_set_text(p_conn, LV_SYMBOL_WARNING " 访问码被拒绝");
  } else if (p.failCode != 0) {
    lv_label_set_text_fmt(p_conn, LV_SYMBOL_WARNING " 连接失败 %d", p.failCode);
  } else {
    lv_label_set_text_fmt(p_conn, LV_SYMBOL_REFRESH " WiFi 已连接 · 正在连接 %s ...", p.ip);
  }
}

static void updateCursor(const CursorState& c) {
  String plan = c.membership;
  plan.toUpperCase();
  setBadge(c_badge, plan.length() ? plan.c_str() : "--", C_CURSOR);

  if (c.lastUpdateMs) {
    lv_label_set_text_fmt(c_pct, "%d%%", (int)roundf(c.percent));
    lv_bar_set_value(c_bar, min(100, (int)roundf(c.percent)), LV_ANIM_ON);
    lv_obj_set_style_bg_color(c_bar, c.percent >= 90 ? C_RED : c.percent >= 70 ? C_ORANGE : C_CURSOR,
                              LV_PART_INDICATOR);
    char buf[64];
    if (c.requestMode) snprintf(buf, sizeof(buf), "已用 %d / %d 次请求", c.requests, c.maxRequests);
    else snprintf(buf, sizeof(buf), "已用 $%.2f / 套餐额度 $%.2f", c.usedUsd, c.limitUsd);
    lv_label_set_text(c_amount, buf);

    if (c.requestMode) lv_label_set_text(c_ondemand, "--");
    else if (c.onDemandEnabled) lv_label_set_text_fmt(c_ondemand, "$%.2f", c.onDemandUsd);
    else lv_label_set_text(c_ondemand, "未开启");
  }

  if (c.cycleEnd && timeValid()) {
    const long secs = (long)(c.cycleEnd - time(nullptr));
    if (secs <= 0) lv_label_set_text(c_reset, "即将重置");
    else if (secs >= 86400) lv_label_set_text_fmt(c_reset, "%ld 天", secs / 86400);
    else lv_label_set_text_fmt(c_reset, "%ld小时%02ld分", secs / 3600, (secs % 3600) / 60);
  }

  if (c.error.length()) {
    lv_obj_set_style_text_color(c_status, C_RED, 0);
    lv_label_set_text_fmt(c_status, LV_SYMBOL_WARNING " %s", c.error.c_str());
  } else if (c.lastUpdateMs) {
    lv_obj_set_style_text_color(c_status, C_DIM, 0);
    lv_label_set_text_fmt(c_status, LV_SYMBOL_OK " 已更新 · %lu 分钟前",
                          (unsigned long)((millis() - c.lastUpdateMs) / 60000));
  }
}

static void updateSystem() {
  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text_fmt(s_wifi, "%d dBm", WiFi.RSSI());
    lv_label_set_text(s_ip, WiFi.localIP().toString().c_str());
  } else {
    lv_label_set_text(s_wifi, "未连接");
    lv_label_set_text(s_ip, "--");
  }
  const uint32_t up = millis() / 1000;
  lv_label_set_text_fmt(s_uptime, "%lu小时%02lu分", (unsigned long)(up / 3600),
                        (unsigned long)((up % 3600) / 60));
  lv_label_set_text_fmt(s_mem, "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
}

static void uiTick(lv_timer_t*) {
  PrinterState printers[kPrinterCount];
  CursorState c;
  WeatherState w;
  {
    StateLock lock;
    for (int i = 0; i < kPrinterCount; i++) printers[i] = g_printers[i];
    c = g_cursor;
    w = g_weather;
  }
  for (int i = 0; i < kPrinterCount; i++) updatePrinter(s_printerPages[i], printers[i]);
  // 花掉的像素不会自己恢复，大约每 30 秒整屏重画一次
  static uint8_t repaint = 0;
  if (++repaint >= 60) {
    repaint = 0;
    lv_obj_invalidate(lv_scr_act());
  }
  updateCursor(c);
  updateWeather(w);
  updateSystem();
}

static void onTileChanged(lv_event_t* e) {
  lv_obj_t* tile = lv_tileview_get_tile_act(lv_event_get_target(e));
  const int idx = lv_obj_get_index(tile);
  s_page = idx;
  for (int i = 0; i < kPages; i++) {
    lv_obj_set_style_bg_color(s_dots[i], i == idx ? C_TEXT : C_DIM, 0);
    lv_obj_set_width(s_dots[i], i == idx ? 18 : 8);
  }
}

int uiPage() { return s_page; }

void uiBegin() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C_BG, 0);

  lv_obj_t* tv = lv_tileview_create(scr);
  lv_obj_set_style_bg_color(tv, C_BG, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t* tiles[kPages];
  for (int i = 0; i < kPages; i++) {
    lv_dir_t dir = LV_DIR_NONE;
    if (i > 0) dir |= LV_DIR_LEFT;
    if (i < kPages - 1) dir |= LV_DIR_RIGHT;
    tiles[i] = lv_tileview_add_tile(tv, i, 0, dir);
  }

  for (int i = 0; i < kPrinterCount; i++) buildPrinterPage(tiles[i], s_printerPages[i], kPrinterNames[i]);
  buildCursorPage(tiles[kPrinterCount]);
  buildWeatherPage(tiles[kPrinterCount + 1]);
  buildSystemPage(tiles[kPrinterCount + 2]);

  lv_obj_t* dots = lv_obj_create(scr);
  lv_obj_remove_style_all(dots);
  lv_obj_set_size(dots, 120, 12);
  lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dots, 6, 0);
  lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < kPages; i++) {
    s_dots[i] = lv_obj_create(dots);
    lv_obj_remove_style_all(s_dots[i]);
    lv_obj_set_size(s_dots[i], i == 0 ? 18 : 8, 8);
    lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_dots[i], i == 0 ? C_TEXT : C_DIM, 0);
  }
  lv_obj_add_event_cb(tv, onTileChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  uiTick(nullptr);
  lv_timer_create(uiTick, 500, nullptr);
}
