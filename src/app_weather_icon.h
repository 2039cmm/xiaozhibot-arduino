#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 开机设默认图标并启动持续漂浮动画。必须在 ui_init() 之后调用。
void app_weather_icon_init(void);

// 每秒由 app_clock 调用。内部做去重，只有 WMO 代码真的变了才换图。
// valid=false（天气还没拉到）时显示官方 999 未知图标。
void app_weather_icon_update(int wmo_code, bool valid);

#ifdef __cplusplus
}
#endif
