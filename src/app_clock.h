#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 创建 LVGL 定时器（1 秒一次），刷新首页时钟 / 日期 / 星期 / 天气，
// 以及顶栏的 WiFi 状态。必须在 ui_init() 之后调用。
void app_clock_init(void);

#ifdef __cplusplus
}
#endif
