// app_touch.h -- 三个电容触摸键（T1/T2/T3 = GPIO1/2/3）
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化三个电容触摸键并采样基准值。必须在 ui_init() 之后调用（翻页依赖 ui_pages）。
void app_touch_init(void);

// 在 loop() 里轮询：消抖 + 短按/长按识别，并驱动 UI 翻页。
void app_touch_loop(void);

// 调试用：打印三个通道的实时读数与基准值。
void app_touch_debug(void);

#ifdef __cplusplus
}
#endif
