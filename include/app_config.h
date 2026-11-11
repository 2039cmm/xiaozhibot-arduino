#pragma once

// ============================================================
// P4 联网配置
//
// WiFi 账号密码不要写在本文件里（本文件进版本控制）。
// 新建 include/app_secrets.h（已在 .gitignore 中忽略），内容：
//
//     #define APP_WIFI_SSID  "你的SSID"
//     #define APP_WIFI_PASS  "你的密码"
//
// 有这个文件就用它，没有就退回下面的占位符（此时不联网）。
// ============================================================

#if __has_include("app_secrets.h")
#include "app_secrets.h"
#endif

#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID   "YOUR_SSID"
#endif

#ifndef APP_WIFI_PASS
#define APP_WIFI_PASS   "YOUR_PASSWORD"
#endif

// 天气位置：默认上海。改成你所在城市即可，不需要注册任何 API key。
// 注意写成字符串，避免 printf 依赖浮点格式化（newlib-nano 可能被裁剪）。
#define APP_LATITUDE_S   "31.2304"
#define APP_LONGITUDE_S  "121.4737"

// 时区。POSIX 的符号是反的：CST-8 表示 UTC+8（东八区）。
#define APP_TZ           "CST-8"

// NTP 服务器，按顺序尝试
#define APP_NTP1         "ntp.aliyun.com"
#define APP_NTP2         "ntp.tencent.com"
#define APP_NTP3         "pool.ntp.org"

// 天气刷新间隔：15 分钟
#define APP_WEATHER_PERIOD_MS  (15UL * 60UL * 1000UL)

// 时间有效性阈值：2024-01-01 00:00:00 UTC。早于此值说明还没对时。
#define APP_TIME_VALID_EPOCH   1704067200LL

// WiFi 断线重连间隔
#define APP_WIFI_RETRY_MS      10000UL
