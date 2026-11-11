// app_weather.cpp -- Open-Meteo 免 key 天气抓取
//
// 为什么用 Open-Meteo：
//   官方固件用和风天气，需要注册账号拿 key，而且免费额度还要申请。
//   Open-Meteo 完全免 key、免注册，直接 GET 就行，返回精简 JSON。
//
//   请求：https://api.open-meteo.com/v1/forecast
//         ?latitude=<lat>&longitude=<lon>
//         &current=temperature_2m,weather_code
//         &timezone=Asia/Shanghai
//   返回：{"current":{"time":"...","temperature_2m":16.2,"weather_code":3}, ...}
//
// HTTPS 用 setInsecure() 跳过证书校验：这里只拉公开天气数据，不涉及
// 任何凭据，换来的好处是不用为证书过期维护固件。要严格校验的话，把
// setInsecure 换成 setCACert(ROOT_CA) 即可。
//
// 整个抓取跑在独立 FreeRTOS 任务里。TLS 握手 + 服务器响应可能有数秒
// 的阻塞，如果放在 loop 里会直接卡住 lv_timer_handler，屏幕就顿住。

#include "app_log.h"
#include "app_weather.h"
#include "app_config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>

static weather_state_t   s_state = { 0, -1, "—", false };
static SemaphoreHandle_t s_lock  = NULL;

// WMO 天气代码 -> 中文短词
//
// 【重要】这里每个字都必须是 ui_font_OPPOSansH25 里真实存在的字形。
// SquareLine 按设计稿里出现过的文字生成子集字体，OPPOSansH25 一共只有
// 30 个汉字：
//     一三中二云五六冰功周四多大天子小德掷日晴转阴阵雨雪雷雹雾霾骰
// 不在这个集合里的字会渲染成方框（豆腐块）。
// 例如 WMO 1 的官方叫法「少云」——「少」不在集合里，只能退成「晴」。
// app_clock_init() 里有个自检会把遗漏的字打到串口，别绕过它。
const char *app_weather_code_text(int code)
{
  switch (code) {
    case 0:                  return "晴";
    case 1:                  return "晴";       // 少云（"少"缺字形）
    case 2:                  return "多云";
    case 3:                  return "阴";
    case 45: case 48:        return "雾";

    case 51: case 53: case 55: return "小雨";   // 毛毛雨
    case 56: case 57:          return "雨";     // 冻雨（"冻"缺字形）
    case 61:                   return "小雨";
    case 63:                   return "中雨";
    case 65:                   return "大雨";
    case 66: case 67:          return "雨";

    case 71:                   return "小雪";
    case 73:                   return "中雪";
    case 75:                   return "大雪";
    case 77:                   return "雪";     // 雪粒

    case 80: case 81: case 82: return "阵雨";
    case 85: case 86:          return "阵雪";

    case 95:                   return "雷阵雨";
    case 96: case 99:          return "冰雹";   // 雷暴冰雹（"暴"缺字形）

    default:                   return "--";     // 未收录的代码，用 ASCII 兜底
  }
}

void app_weather_get(weather_state_t *out)
{
  if (!out) return;
  if (!s_lock) {
    *out = s_state;
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  *out = s_state;
  xSemaphoreGive(s_lock);
}

// 返回 true 表示这次抓取成功
static bool fetch_once(void)
{
  char url[224];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast"
           "?latitude=%s&longitude=%s"
           "&current=temperature_2m,weather_code"
           "&timezone=Asia%%2FShanghai",
           APP_LATITUDE_S, APP_LONGITUDE_S);

  WiFiClientSecure tls;
  tls.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setReuse(false);

  if (!http.begin(tls, url)) {
    LOG.println("[wx] begin failed");
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[wx] http status=%d\n", status);
    http.end();
    return false;
  }

  const String body = http.getString();
  http.end();

  DynamicJsonDocument doc(768);
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    LOG.printf("[wx] json error: %s\n", err.c_str());
    return false;
  }

  JsonObject cur = doc["current"];
  if (cur.isNull()) {
    LOG.println("[wx] no \"current\" field");
    return false;
  }

  // | 后面的默认值处理字段缺失的情况
  const float t  = cur["temperature_2m"] | 0.0f;
  const int   cd = cur["weather_code"]   | -1;

  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_state.temp_c = (int)(t >= 0 ? (t + 0.5f) : (t - 0.5f));
  s_state.code   = cd;
  s_state.text   = app_weather_code_text(cd);
  s_state.valid  = true;
  xSemaphoreGive(s_lock);

  LOG.printf("[wx] ok  %dC  %s  (code=%d, %u bytes)\n",
             s_state.temp_c, s_state.text, cd, (unsigned)body.length());
  return true;
}

// 等 WiFi 真正连上再抓，最多等 timeout_ms。
// 不能用固定 delay：WiFi 关联 + DHCP 的耗时跟信号强度强相关，
// 固定等 3 秒在弱信号下必然错过，白白浪费一次退避周期。
static bool wait_wifi(uint32_t timeout_ms)
{
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > timeout_ms) return false;
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  return true;
}

static void weather_task(void *arg)
{
  (void)arg;
  uint32_t fail = 0;

  for (;;) {
    if (!wait_wifi(30000)) {
      LOG.println("[wx] wifi 30s 未连上，10 秒后再看");
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }

    if (fetch_once()) {
      fail = 0;
      vTaskDelay(pdMS_TO_TICKS(APP_WEATHER_PERIOD_MS));
      continue;
    }

    // 线性退避：10s / 20s / 30s ... 上限 5 分钟
    fail++;
    uint32_t backoff = 10000UL * fail;
    if (backoff > 300000UL) backoff = 300000UL;
    LOG.printf("[wx] 重试间隔 %lu 秒\n", (unsigned long)(backoff / 1000));
    vTaskDelay(pdMS_TO_TICKS(backoff));
  }
}

void app_weather_start(void)
{
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
      LOG.println("[wx] mutex alloc failed");
      return;
    }
  }

  // 栈给到 8KB：mbedTLS 握手里有局部大缓冲。
  // 固定在 core 0（WiFi 协议栈所在核），避免和 core 1 上的 LVGL 抢 CPU。
  BaseType_t ok = xTaskCreatePinnedToCore(
      weather_task, "wx_task", 8192, NULL, 3, NULL, 0);
  if (ok != pdPASS) {
    LOG.println("[wx] task create failed");
    return;
  }
  LOG.println("[wx] task started");
}
