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

const char *app_weather_code_text(int code)
{
  if (code == 0)                   return "晴";
  if (code == 1)                   return "少云";
  if (code == 2)                   return "多云";
  if (code == 3)                   return "阴";
  if (code == 45 || code == 48)    return "雾";
  if (code >= 51 && code <= 57)    return "毛毛雨";
  if (code >= 61 && code <= 65)    return "雨";
  if (code == 66 || code == 67)    return "冻雨";
  if (code >= 71 && code <= 77)    return "雪";
  if (code >= 80 && code <= 82)    return "阵雨";
  if (code == 85 || code == 86)    return "阵雪";
  if (code == 95)                  return "雷阵雨";
  if (code == 96 || code == 99)    return "雷暴";
  return "未知";
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

static void weather_task(void *arg)
{
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(3000));      // 等 WiFi 先连上

  uint32_t fail_count = 0;

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (fetch_once()) {
        fail_count = 0;
        vTaskDelay(pdMS_TO_TICKS(APP_WEATHER_PERIOD_MS));
        continue;
      }
      fail_count++;
    } else {
      fail_count++;
    }

    // 失败退避：最短 30 秒，最长 5 分钟，避免断网时疯狂重试
    uint32_t backoff = 30000UL << (fail_count > 4 ? 4 : fail_count);
    if (backoff > 300000UL) backoff = 300000UL;
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
