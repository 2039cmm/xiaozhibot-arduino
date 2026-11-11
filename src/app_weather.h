#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef struct {
  int         temp_c;    // 温度，摄氏度，已取整
  int         code;      // WMO 天气代码，-1 = 未知
  const char *text;      // 中文描述，如 "多云"
  bool        valid;     // 是否拿到过有效数据
} weather_state_t;

// 启动后台抓取任务（FreeRTOS，独立栈，不阻塞 LVGL）
void app_weather_start(void);

// 取当前天气快照（内部加锁拷贝，线程安全）
void app_weather_get(weather_state_t *out);

// WMO 代码 -> 中文
const char *app_weather_code_text(int code);

#ifdef __cplusplus
}
#endif
