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

    static const char *const k_weekday[] = {
      "周日", "周一", "周二", "周三", "周四", "周五", "周六"
    };
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
  lv_timer_t *t = lv_timer_create(clock_tick, 1000, NULL);
  if (t == NULL) {
    LOG.println("[clk] timer create failed");
    return;
  }
  clock_tick(t);          // 立刻刷一次，别等第一秒
  LOG.println("[clk] timer started");
}
