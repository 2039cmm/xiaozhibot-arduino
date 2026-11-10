# xiaozhibot-arduino

把 **ESP-SparkBot**（ESP32-S3 桌面 AI 机器人）的主体固件，从官方 ESP-IDF 版本迁移到 **Arduino 框架**（PlatformIO）。

> 硬件开源设计：[ESP-SparkBot](https://oshwhub.com/esp-college/esp-sparkbot)
> 官方固件参考：[esp-friends/esp_sparkbot](https://gitee.com/esp-friends/esp_sparkbot)（本地参考副本见 `../esp_sparkbot/example/factory_demo_v1`）

---

## 一、项目目标

### 总目标

在 Arduino 框架下复刻机器人主体的全部功能，保持与原厂固件一致的交互和视觉效果：

- 240x240 ST7789 屏 + LVGL 界面（天气时钟 / 表情 / 木鱼 / 骰子 / 2048 / 摄像头预览）
- 三个触摸按键交互（顶部确认、左右翻页），长按 2 秒
- BMI270 六轴传感器（摇骰子、2048 手势）
- ES8311 音频编解码（录音 + 放音）
- 电量检测与显示
- WiFi 联网：SNTP 校时、天气、大模型对话
- OV2640 摄像头
- 4P 磁吸口通过 UART 控制履带底盘

### 不做什么（当前阶段的边界）

- **不重写界面**。官方 `ui/` 是 SquareLine Studio 1.4.2 生成的 LVGL 8.3.11 纯 C 代码，图片和字体全部是 C 数组（含 OPPOSans 中文字体），可原样搬进 Arduino，只接一个刷屏后端。用 Arduino_GFX 图元重画界面等于白扔几周的 UI 工作量。
- **暂不在本地跑 esp-sr 唤醒词**。Arduino 框架下引入 esp-sr 预编译库成本高，语音先走云端 ASR，本地唤醒词后置。
- **不搬 thorvg/Lottie 渲染器**。动画资源离线转成帧序列或 GIF 再播放。

---

## 二、硬件资源速查

| 模块 | 引脚 / 配置 |
|---|---|
| LCD ST7789 240x240 | SCK=21 MOSI=47 CS=44 DC=43 RST=无 BL=46，SPI3_HOST，原厂 80MHz / SPI mode 2，IPS 反色 |
| I2C（ES8311 + BMI270 + OV2640 SCCB 共用） | SCL=5 SDA=4，400kHz |
| I2S | MCLK=45 BCLK=39 LRCK=41 DOUT=42（→codec） DIN=40（←codec） |
| 触摸按键 | T1/T2/T3 = GPIO1 / GPIO2 / GPIO3（顶 / 左 / 右），灵敏度 0.035 / 0.08 / 0.08 |
| 电量检测 | ADC2_CH3，分压比 2.0 |
| OV2640 | XCLK=15 PCLK=13 VSYNC=6 HREF=7，D0~D7=11,9,8,10,12,18,17,16，RGB565 240x240 |
| 磁吸 4P | 5V / GPIO48 / GPIO38 / GND（UART 接 ESP32-C2 履带底盘） |

### 待实测确认

- GPIO1 同时被官方用作 4 键 ADC 按键梯（`BSP_BUTTONS_IO`）和触摸通道 T1，物理上不可能同时有效。移植时**以 3 个触摸键为准**，ADC 按键组先不搬。
- GPIO46 是 ESP32-S3 的 strapping 引脚，上电瞬间可能有抖动，背光闪烁属正常。

---

## 三、当前进度

### 已完成

- [x] **工程骨架**：PlatformIO + Arduino 框架，`esp32-s3-devkitc-1`，16MB Flash / qio_opi 配置
- [x] **点屏 demo**：ST7789 点亮，彩条 + 文字 + 圆/矩形绘制，确认整屏刷新正常（`src/main.cpp`）
- [x] **硬件调研**：读通官方 BSP（`esp_sparkbot_bsp`）、音频（`bsp_extra`）、IMU、触摸、电量各模块的引脚与初始化流程
- [x] **移植方案定型**：确定"保留 LVGL UI + 重写 BSP 适配层"的路线，见下方对照表
- [x] **代码托管**：本仓库建立，`main` 分支已同步
- [x] **烧录点亮**：确认硬件通路，串口日志与画面均正常

- [x] **P1 代码落地**：LVGL 8.3.11 + 官方 SquareLine UI（title / home）编译，RAM 29.2%、Flash 16.3%

### 未开始

- [ ] P1 上板验证（串口未枚举，等设备接回）
- [ ] 触摸 / IMU / 电量
- [ ] 音频
- [ ] 联网功能
- [ ] 摄像头 / 动画 / 小游戏

### 当前工具链

| 项 | 版本 | 备注 |
|---|---|---|
| PlatformIO | espressif32 6.9.0 | 官方平台，对应 arduino-esp32 2.0.17 / IDF 4.4 |
| Arduino core | 2.0.17 | 偏老，PSRAM 只能跑到 80MHz，缺 IDF5 新 API |
| 显示库 | GFX Library for Arduino 1.4.9 | 1.6.x 需要 arduino-esp32 3.x，故钉 1.4.9 |

---

## 四、组件对照：IDF → Arduino

| 原 IDF 组件 | Arduino 侧实现 | 难度 |
|---|---|---|
| `esp_lcd` + `esp_lvgl_port` | LVGL 8.3.11 + 刷屏后端（GFX 或 LovyanGFX） | 中 |
| `esp_codec_dev` · ES8311 | `driver/i2s_std` + Wire 写寄存器 | 高 |
| `bmi270` (I2C) | Bosch BMI270 SensorAPI + Wire | 低 |
| `touch_element` + `iot_button` | `touchRead` + 自写阈值与消抖 | 中 |
| `adc_oneshot` · 电量 | `analogRead` + 分压换算 | 低 |
| `esp32-camera` · OV2640 | 同一份驱动，Arduino 下可直接用 | 低 |
| `esp-sr` · AFE 唤醒词 | 云端 ASR，本地暂缓 | 高 |
| `ui/`（SquareLine 生成） | 原样编译，只改 `lv_conf.h` | 低 |

---

## 五、实施计划

| 阶段 | 内容 | 验收标准 | 状态 |
|---|---|---|---|
| P0 | 点屏 + 工程骨架 | 彩条无残影，文字清晰 | ✅ 完成 |
| P1 | LVGL + UI 迁移 | title / home 正常显示 | 代码完成，待上板 |
| P2 | 输入与传感器 | 三键翻页、BMI270 数据、电量百分比正确 | ⬜ 待开始 |
| P3 | 音频 | 能放 WAV，能录 PCM | ⬜ 待开始 |
| P4 | 联网 | WiFi 连接、SNTP 校时、天气显示、大模型对话 | ⬜ 待开始 |
| P5 | 摄像头 / 动画 / 游戏 | 摄像头预览、开机动画、木鱼/骰子/2048 可玩 | ⬜ 待开始 |

---

## 六、已知隐患（P1 前需处理）

1. **`HSPI` 宏陷阱**：`src/main.cpp` 里 `#ifndef HSPI / #define HSPI SPI3_HOST` 实际不生效——arduino-esp32 2.0.17 已把 S3 的 `HSPI` 定义为 `1`，而 GFX 1.4.9 把这个参数当 `_spi_bus_array[]` 的下标用，恰好下标 1 就是 SPI3，属于"碰巧正确"。升级到 GFX 1.6 / arduino 3.x 语义会变，需要复核。
2. **SPI mode 不一致**：原厂是 mode 2，Arduino_GFX 固定 mode 0。若出现偶发花屏或首帧错位，第一嫌疑在此。
3. **反色与偏移**：原厂 `esp_lcd_panel_invert_color(true)`，GFX 用 `ips=true` 近似。颜色反了就把 `ips` 改 `false`；整幅图偏 80px 说明是 240x320 玻璃。
4. **PSRAM 未校验**：`board_build.arduino.memory_type = qio_opi` 是否真正生效，需在 setup 里打印 `ESP.getPsramSize()` 确认。
5. **分区表未规划**：16MB Flash 仍用默认分区表。后续要放动画帧、音频、字库，必须自定义 `partitions.csv` + `data/` 资源镜像（官方是 4M app + ~11M 数据分区）。

---

## 七、待决策

1. **框架版本**：留在 PlatformIO 官方 espressif32 6.9（arduino 2.0.17，省事但 IDF 4.4 太老）还是换 pioarduino（arduino 3.x / IDF5，`esp_codec_dev`、LVGL9 可直接用，但库要重调）？
2. **显示后端**：继续用 Arduino_GFX，还是换 LovyanGFX（S3 上 DMA 刷屏更快，LVGL 集成示例更多）？
3. **语音方案**：本地 esp-sr 唤醒词是否必须保留？

---

## 八、目录结构

```
xiaozhibot/
├── platformio.ini      # 板级配置、库依赖
├── src/main.cpp        # P0 点屏 demo，后续拆分为 bsp / ui / app 多模块
├── include/            # 公共头文件（后续放 lv_conf.h、bsp_pins.h）
├── lib/                # 本地库（后续放 ui 移植代码、第三方驱动）
└── docs/               # 设计文档
```

本地参考源码位于仓库外一层：`../esp_sparkbot/`（官方 Gitee 全量代码，含 7 个 example）。
