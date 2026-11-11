#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// 初始化：设置时区、启动 WiFi STA（非阻塞，立即返回）
void app_net_init(void);

// 主循环里调用：状态打印 + 断线重连 + 联网成功后启动 SNTP
void app_net_loop(void);

// WiFi 是否已连接
bool app_net_wifi_connected(void);

// 系统时间是否已经对过时（早于 2024-01-01 视为未对时）
bool app_net_time_valid(void);

#ifdef __cplusplus
}
#endif
