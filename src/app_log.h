#pragma once

// 日志统一走 USB-Serial-JTAG（GPIO19/20）。
// 绝不能用 Serial：本板 LCD 的 DC=GPIO43、CS=GPIO44 与 UART0 复用。
#include <Arduino.h>

#define LOG USBSerial
