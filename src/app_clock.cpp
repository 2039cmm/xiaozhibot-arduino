// app_clock.cpp -- 把系统时间 / 天气灌进 SquareLine 生成的标签
//
// 对应官方 app/app_animation.c 里的 ui_clock_update()，差异有三处：
//   1) 官方每秒无脑 lv_label_set_text 六个标签，这里先比字符串再设，
//      避免每分钟 59 次无意义的重排 + 重绘（SPI 屏刷一次不便宜）。
//   2) 没有电池，电量文字和滑块先隐藏，等 P2 拿到 ADC 再恢复。
//   3) WiFi 图标按真实连接状态切换，官方那里被 if(1) 写死了。

#include "app_log.h"
#include "app_clock.h"
#include "app_net.h"
#include "app_weather.h"
#include "app_weather_icon.h"

#include "ui.h"
#include <time.h>
#include <string.h>

// 仅当内容真的变了才写，省掉无谓的重绘
static void set_text(lv_obj_t *label, const char *s)
{
  if (label == NULL || s == NULL) return;

  const char *cur = lv_label_get_text(label);
  if (cur != NULL && strcmp(cur, s) == 0) return;

  lv_label_set_text(label, s);
}

static const char *const k_weekday[] = {
  "周日", "周一", "周二", "周三", "周四", "周五", "周六"
};

// ---------------- 字形自检 ----------------
//
// SquareLine 生成的是子集字体：只包含设计稿里出现过的字，缺字时 LVGL
// 不报错、只静默画一个方框。真实踩过：WMO 1 的「少云」，「多」在字库里
// 而「少」不在，显示成「□云」。
//
// 现在中文改用自己用 lv_font_conv 生成的 ui_font_zh25
// （GB2312 一级 3755 字 + ASCII + 常用标点，bpp=1，约 266KB）。
// 常用字全覆盖，但自检保留：以后若有人换回子集字体，或写入生僻字、
// emoji，这里会第一时间把缺的字打到串口，而不是让人盯着方框猜。

static int utf8_decode(const char *s, uint32_t *cp)
{
  const uint8_t *p = (const uint8_t *)s;

  if (p[0] < 0x80) {
    *cp = p[0];
    return 1;
  }
  if ((p[0] & 0xE0) == 0xC0) {
    *cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    return 2;
  }
  if ((p[0] & 0xF0) == 0xE0) {
    *cp = ((uint32_t)(p[0] & 0x0F) << 12) |
          ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return 3;
  }
  if ((p[0] & 0xF8) == 0xF0) {
    *cp = ((uint32_t)(p[0] & 0x07) << 18) |
          ((uint32_t)(p[1] & 0x3F) << 12) |
          ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    return 4;
  }
  *cp = p[0];
  return 1;
}

static void check_text(const lv_font_t *font, const char *tag, const char *s)
{
  if (font == NULL || s == NULL) return;

  bool bad = false;
  for (const char *p = s; *p; ) {
    uint32_t cp = 0;
    const int n = utf8_decode(p, &cp);
    p += n;

    lv_font_glyph_dsc_t dsc;
    if (lv_font_get_glyph_dsc(font, &dsc, cp, 0)) {
      continue;
    }
    if (!bad) {
      LOG.printf("[font] \"%s\" 缺字形:", tag);
      bad = true;
    }
    LOG.printf(" U+%04lX", (unsigned long)cp);
  }
  if (bad) LOG.println();
}

static void check_weather_table(void)
{
  const char *seen[16];
  int nseen = 0;

  for (int code = -1; code <= 99; code++) {
    const char *s = app_weather_code_text(code);
    if (s == NULL) continue;

    bool dup = false;
    for (int i = 0; i < nseen; i++) {
      if (seen[i] == s) {          // 同一个字面量，只查一次
        dup = true;
        break;
      }
    }
    if (dup) continue;
    if (nseen < (int)(sizeof(seen) / sizeof(seen[0]))) {
      seen[nseen++] = s;
    }

    check_text(&ui_font_zh25, s, s);
  }
}

static void clock_tick(lv_timer_t *timer)
{
  (void)timer;

  static bool     s_wifi_shown = false;
  static bool     s_offline_ui = false;

  char hour_str[4]  = "--";
  char min_str[4]   = "--";
  char time_str[8]  = "--:--";
  char date_str[16] = "--/--";
  char weekday_str[16] = "---";
  char temp_str[8]  = "--°";

  const bool timed = app_net_time_valid();

  if (timed) {
    const time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);

    snprintf(hour_str, sizeof(hour_str), "%02d", ti.tm_hour);
    snprintf(min_str,  sizeof(min_str),  "%02d", ti.tm_min);
    snprintf(time_str, sizeof(time_str), "%02d:%02d", ti.tm_hour, ti.tm_min);
    snprintf(date_str, sizeof(date_str), "%02d/%02d", ti.tm_mon + 1, ti.tm_mday);
    snprintf(weekday_str, sizeof(weekday_str), "%s", k_weekday[ti.tm_wday]);
  }

  // ---- 首页主体 ----
  set_text(ui_hour,    hour_str);
  set_text(ui_min,     min_str);
  set_text(ui_date,    date_str);
  set_text(ui_weekday, weekday_str);

  // ---- 顶栏时间 ----
  set_text(title_timestate, time_str);

  // ---- 天气 ----
  weather_state_t wx;
  app_weather_get(&wx);
  if (wx.valid) {
    snprintf(temp_str, sizeof(temp_str), "%d°", wx.temp_c);
    set_text(ui_temp, temp_str);
    set_text(ui_weather, wx.text);
  }
  app_weather_icon_update(wx.code, wx.valid);

  // ---- 顶栏 WiFi 图标 ----
  const bool conn = app_net_wifi_connected();
  if (conn != s_wifi_shown) {
    s_wifi_shown = conn;
    lv_img_set_src(title_wifistate,
                   conn ? &ui_img_wifi_png : &ui_img_wifi_disconnection_png);
  }

  // ---- 未联网时把顶栏电池区隐藏（没有电池硬件）----
  if (!s_offline_ui) {
    s_offline_ui = true;
    lv_obj_add_flag(title_batterytxt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(title_powerstate, LV_OBJ_FLAG_HIDDEN);
  }

  // 每分钟打一行状态，方便串口侧确认时钟在走
  if (timed) {
    static char last_log[8] = {0};
    if (strcmp(last_log, min_str) != 0) {
      strncpy(last_log, min_str, sizeof(last_log) - 1);
      last_log[sizeof(last_log) - 1] = '\0';
      LOG.printf("[clk] %s %s %s  wifi=%d  heap=%lu\n",
                 time_str, date_str, weekday_str,
                 (int)conn, (unsigned long)ESP.getFreeHeap());
    }
  }
}

void app_clock_init(void)
{
  // 先做字形自检，缺字会在串口里直接列出来
  check_weather_table();
  for (unsigned i = 0; i < sizeof(k_weekday) / sizeof(k_weekday[0]); i++) {
    check_text(&ui_font_zh25, k_weekday[i], k_weekday[i]);
  }
  check_text(&ui_font_OPPOSansL70, "--°", "--°");   // ui_temp 占位
  check_text(&ui_font_OPPOSansH18, "--/--", "--/--"); // ui_date 占位
  LOG.println("[font] glyph check done");

  lv_timer_t *t = lv_timer_create(clock_tick, 1000, NULL);
  if (t == NULL) {
    LOG.println("[clk] timer create failed");
    return;
  }
  clock_tick(t);          // 立刻刷一次，别等第一秒
  LOG.println("[clk] timer started");
}
