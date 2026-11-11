// app_net.cpp -- WiFi STA + SNTP 对时
//
// 设计要点：
//  1) 全部非阻塞。app_net_init() 只发起连接就返回，不在 setup 里死等。
//  2) 断线自动重连，10 秒一次。
//  3) 日志一律走 USBSerial。本板 DC=43 / CS=44 与 UART0 复用，
//     绝对不能碰 Serial，否则屏幕立刻失效。
//  4) WiFi 关掉省电（setSleep(false)），否则 DTIM 会让 LVGL 刷屏出现
//     一卡一卡的抖动。

#include "app_log.h"
#include "app_net.h"
#include "app_config.h"

#include <WiFi.h>
#include <time.h>
#include <string.h>

static bool     s_sntp_started  = false;
static bool     s_was_connected = false;
static bool     s_placeholder   = false;
static uint32_t s_last_retry    = 0;

bool app_net_wifi_connected(void)
{
  return WiFi.status() == WL_CONNECTED;
}

bool app_net_time_valid(void)
{
  return time(NULL) > (time_t)APP_TIME_VALID_EPOCH;
}

static void start_sntp(void)
{
  // 必须用 configTzTime，不能用 configTime。
  // 看 cores/esp32/esp32-hal-time.c：configTime() 末尾无条件调用
  //   setTimeZone(-gmtOffset_sec, daylightOffset_sec)
  // 传 (0, 0) 时会执行 setenv("TZ", "UTC0DST0")，把我们的 CST-8 覆写成
  // UTC —— 表现就是对时成功后屏幕时间比实际早 8 小时。
  // configTzTime() 则直接用传入的 TZ 字符串，正是我们要的。
  configTzTime(APP_TZ, APP_NTP1, APP_NTP2, APP_NTP3);
  s_sntp_started = true;
  LOG.printf("[net] sntp started, tz=%s\n", APP_TZ);
}

void app_net_init(void)
{
  setenv("TZ", APP_TZ, 1);
  tzset();

  if (strcmp(APP_WIFI_SSID, "YOUR_SSID") == 0) {
    s_placeholder = true;
    LOG.println("[net] !! WiFi 未配置：请改 include/app_config.h 里的");
    LOG.println("[net] !! APP_WIFI_SSID / APP_WIFI_PASS 后重新烧录");
    return;
  }

  WiFi.persistent(false);      // 不往 NVS 反复写，省 flash 寿命
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(APP_WIFI_SSID, APP_WIFI_PASS);

  LOG.printf("[net] connecting to \"%s\"\n", APP_WIFI_SSID);
}

void app_net_loop(void)
{
  if (s_placeholder) {
    return;
  }

  const bool conn = app_net_wifi_connected();

  if (conn != s_was_connected) {
    s_was_connected = conn;
    if (conn) {
      LOG.printf("[net] wifi ok  ip=%s  rssi=%d\n",
                 WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    } else {
      LOG.println("[net] wifi lost, retrying");
    }
  }

  if (conn && !s_sntp_started) {
    start_sntp();
  }

  if (!conn && millis() - s_last_retry > APP_WIFI_RETRY_MS) {
    s_last_retry = millis();
    WiFi.reconnect();
  }
}
