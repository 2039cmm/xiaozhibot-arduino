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
- [x] **P1 上板验证**：时钟首页完整渲染（Comfortaa 大数字 + OPPOSans 中文星期），`heap` 稳定不泄漏
- [x] **冒号闪烁**：补回 `blink_Animation(ui_colon, 0)` 调用，1Hz 呼吸已确认

- [x] **P4 代码落地**：WiFi STA + SNTP 对时 + Open-Meteo 天气（免 key），编译 RAM 33.9%、Flash 27.4%
- [x] **P4 上板验证**：WiFi 连上（rssi -39）、SNTP 对时、时区 CST-8 正确、天气取到 13°C 少云、堆稳定不降
- [x] **界面逐项核对**：按 ①–⑧ 与实机核对，顶栏 / 中文 / 裁切 / 冒号闪烁全部一致，唯一缺口是天气插画区全黑
- [x] **天气图标 + 动画落地**：从官方 61 张 weather PNG 挑 16 张转成 LVGL 静态资源补齐 `ui_weathershow`；加入滑入、持续漂浮、换图弹跳。编译 RAM 37.4%、Flash 60.2%（RAM 无增长，图标全在 Flash）
- [x] **图标离线验证 + 语义修正**：反解 C 数组与官方 PNG 逐像素比对，16 张小端误差全为 0，确认没串色；发现官方 `104` 实际画的是「太阳被云遮」，把「阴」改指 `102` 纯厚云，避免阴天出太阳

### 未开始

- [ ] 触摸（板上无触摸焊盘，待定：接铜箔还是改轻触按键）
- [ ] IMU（无 BMI270 硬件，跳过）
- [ ] 电量（无电池，跳过）
- [ ] 音频（有喇叭，待定 I2S 直推还是补 ES8311）
- [ ] 摄像头 / 其余动画（Lottie 表情）/ 小游戏（天气图标动画已完成）

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
| P1 | LVGL + UI 迁移 | title / home 正常显示 | ✅ 完成 |
| P2 | 输入与传感器 | 三键翻页、BMI270 数据、电量百分比正确 | ⏸ 硬件缺失暂缓 |
| P3 | 音频 | 能放 WAV，能录 PCM | ⬜ 待开始 |
| P4 | 联网 | WiFi 连接、SNTP 校时、天气显示、大模型对话 | ✅ 前三项完成 |
| P5 | 摄像头 / 动画 / 游戏 | 摄像头预览、开机动画、木鱼/骰子/2048 可玩 | 🔨 天气图标动画已完成，其余待开始 |

---

## 六、已知隐患（P1 前需处理）

1. **`HSPI` 宏陷阱**：`src/main.cpp` 里 `#ifndef HSPI / #define HSPI SPI3_HOST` 实际不生效——arduino-esp32 2.0.17 已把 S3 的 `HSPI` 定义为 `1`，而 GFX 1.4.9 把这个参数当 `_spi_bus_array[]` 的下标用，恰好下标 1 就是 SPI3，属于"碰巧正确"。升级到 GFX 1.6 / arduino 3.x 语义会变，需要复核。
2. **SPI mode 不一致**：原厂是 mode 2，Arduino_GFX 固定 mode 0。若出现偶发花屏或首帧错位，第一嫌疑在此。
3. **反色与偏移**：原厂 `esp_lcd_panel_invert_color(true)`，GFX 用 `ips=true` 近似。颜色反了就把 `ips` 改 `false`；整幅图偏 80px 说明是 240x320 玻璃。
4. **PSRAM 实测为 0**：启动日志 `mode:DIO`，说明 `qio_opi` 没真正生效；`ESP.getPsramSize()` 返回 0。后续放动画帧/音频前必须补 `-DBOARD_HAS_PSRAM` 并复核 bootloader 配置。
5. **分区表未规划**：16MB Flash 仍用默认分区表。后续要放动画帧、音频、字库，必须自定义 `partitions.csv` + `data/` 资源镜像（官方是 4M app + ~11M 数据分区）。
6. **隐藏屏动画陷阱**：SquareLine 会把部分动画（如首页冒号的 `blink_Animation`）挂在 `ui____initial_actions0` 这个隐藏屏的 `SCREEN_LOAD_START` 上，而不是目标屏自己。只搬 `ui_home.c` 会得到"画面正确但不动"的假象，需在 `ui_port.c` 里补调用。
7. **`configTime()` 会覆写时区**：`configTime(0, 0, ...)` 末尾无条件调 `setTimeZone(0,0)`，把 TZ 改写成 `UTC0DST0`，对时后时间会早 8 小时。必须用 `configTzTime(APP_TZ, ...)`。见 `cores/esp32/esp32-hal-time.c:48`。
8. **软复位不清 RTC 时间**：ESP-IDF 把系统时间放在 `RTC_DATA_ATTR` 区，`Hard resetting via RTS pin` 之类的软复位不会清空。烧录后第一次启动若已对时，之后抓日志时时间就是对的——排查时区问题时容易误判。
9. **SquareLine 字体是子集，缺字静默变方框**：`lib/ui/fonts/*.c` 只包含设计稿里出现过的字符，缺字时 LVGL **不报错**，只画一个方框。实测子集字符数：

   | 字体 | 用途 | 汉字数 | 现状 |
   |---|---|---|---|
   | ~~`OPPOSansH25`~~ | 天气文字、星期 | 30 | ✅ 已换成 `ui_font_zh25`（3755 字） |
   | `OPPOSansH18` | 日期 | 24 | 仅用数字，暂无影响 |
   | `OPPOSansL70` | 温度大字 | 0（仅 ASCII + `°`） | 仅用数字和 `°`，暂无影响 |

   往标签里写中文字面量前必须确认字在字库中。`app_clock_init()` 里有开机字形自检，缺字会打 `[font] "xxx" 缺字形: U+XXXX`。

10. **图片资源也有字节序问题**：SquareLine 导出的 `TRUE_COLOR_ALPHA` 图片是大端 RGB565，本工程 `LV_COLOR_16_SWAP=0` 需要小端。直接复用官方 `ui_img_*.c` 会串色，必须从 PNG 重新转换，见「天气图标与动画」。
11. **官方天气资源走独立分区**：`partitions.csv` 里 `weather` / `animation` / `storage` 三个 mmap 分区共约 11MB。Arduino 侧目前把图标编进 app 分区，Flash 已用到 60.2%；后续要搬 Lottie 表情、音频等大资源时，自定义分区表仍然绕不开。

---

## 七、待决策

1. **框架版本**：留在 PlatformIO 官方 espressif32 6.9（arduino 2.0.17，省事但 IDF 4.4 太老）还是换 pioarduino（arduino 3.x / IDF5，`esp_codec_dev`、LVGL9 可直接用，但库要重调）？
2. **显示后端**：继续用 Arduino_GFX，还是换 LovyanGFX（S3 上 DMA 刷屏更快，LVGL 集成示例更多）？
3. **语音方案**：本地 esp-sr 唤醒词是否必须保留？

---

## 八、目录结构

```
xiaozhibot/
├── platformio.ini         # 板级配置、库依赖、build_flags
├── src/
│   ├── main.cpp           # 启动流程：LCD -> LVGL -> UI -> 联网 -> 时钟定时器
│   ├── app_net.cpp/.h     # WiFi STA 状态机 + SNTP 对时（非阻塞）
│   ├── app_weather.cpp/.h      # Open-Meteo 天气抓取（独立 FreeRTOS 任务）
│   ├── app_weather_icon.cpp/.h # WMO -> 官方天气图标映射 + 漂浮/弹跳动画
│   ├── app_clock.cpp/.h        # LVGL 1s 定时器，刷时间/日期/星期/天气/WiFi 图标
│   └── app_log.h               # 日志宏，统一走 USBSerial
├── include/
│   ├── lv_conf.h          # LVGL 配置（LV_COLOR_16_SWAP 必须为 0）
│   ├── app_config.h       # 时区、经纬度、NTP、刷新周期
│   └── app_secrets.h      # WiFi 账号密码（本地文件，已 gitignore）
├── lib/ui/
│   ├── ui_port.c                       # Arduino 移植层：全局对象、动画、屏加载事件
│   └── images/ui_img_weather_icons.c   # 16 张 110x110 官方天气图标（自动生成）
├── tools/
│   ├── gen_weather_icons.py            # 官方 PNG -> LVGL C 数组（纯标准库）
│   ├── verify_weather_icons.py         # 反解 C 数组验证字节序 + 黑底预览图
│   └── zh_charset_gb2312_l1.txt        # 中文字库字符集
└── docs/                  # 设计文档
```

本地参考源码位于仓库外一层：`../esp_sparkbot/`（官方 Gitee 全量代码，含 7 个 example）。

### WiFi 配置

新建 `include/app_secrets.h`（该文件不入库）：

```c
#define APP_WIFI_SSID  "你的SSID"
#define APP_WIFI_PASS  "你的密码"
```

不建这个文件也能编译，只是不联网，时钟停在 `--:--`。

### 天气数据来源

用 [Open-Meteo](https://open-meteo.com/)，**免费、免注册、不需要 API key**。官方固件用的是和风天气，需要注册账号申请 key，这里换掉了。位置在 `app_config.h` 里改 `APP_LATITUDE_S` / `APP_LONGITUDE_S` 两个字符串。

### 天气图标与动画

官方把 61 张 110×110 PNG 在构建期转成 QOI，塞进独立的 `weather` mmap 分区，运行时靠 `esp_mmap_assets` + `esp_lv_decoder` 按和风天气图标代码动态加载（`main/app/app_animation.c` 的 `updata_weather_icon()`）。Arduino 工程没有自定义分区表、没有那套解码器，PSRAM 也还没起来，所以改成**编译期静态资源**：

- `tools/gen_weather_icons.py`（纯标准库，不需要 Pillow）从官方 `weather/*.png` 解码 RGBA8，转成 `LV_IMG_CF_TRUE_COLOR_ALPHA`（小端 RGB565 + alpha，3 字节/像素），输出 `lib/ui/images/ui_img_weather_icons.c`
- 挑 16 张覆盖全部 Open-Meteo WMO code，共 580800 字节 ≈ 567KB Flash，**RAM 增加 0**（`const` 数据只进 Flash）
- **按图形观感映射，不盲信和风代码命名**：这套 PNG 里 `104` 画的是「太阳被云遮住」，并不是阴天。WMO 0/1/2/3 分别对应 `100`（纯太阳）/ `103`（大太阳 + 薄云）/ `104`（太阳被云遮）/ `102`（纯厚云、无太阳），避免「阴」天却出太阳
- `src/app_weather_icon.cpp` 做 WMO → 和风代码映射，天气代码变化时换图；每秒由 `app_clock` 调用，内部去重
- 动画三层：① 开机 `weather_Animation()` 从 x=-200 滑到 -60（原本就在 `ui_port.c`，之前没设 src 完全看不见）；② 持续 ±4px 上下漂浮，4.4s 一个来回；③ 换图时 190→255 的 zoom 弹跳
- 天气尚未拉到时显示官方 `999`（N/A）图标，和官方 `find_icon_index()` 的回退行为一致

重新生成：

```bash
python tools/gen_weather_icons.py --sheet        # 61 张官方图标拼图，核对语义用
python tools/gen_weather_icons.py                # 按 DEFAULT_CODES 生成 C 资源
python tools/gen_weather_icons.py 100 101 ...    # 自定义图标集合
python tools/verify_weather_icons.py             # 反解 C 数组，验证字节序 + 生成黑底预览图
```

**字节序陷阱**：官方 SquareLine 导出的 `ui_img_clody_png.c` 是**大端** RGB565（高字节在前），本工程 `LV_COLOR_16_SWAP=0` 要求**小端**。直接搬官方 C 数组会整体串色（橙太阳变紫、白云变绿），必须从原始 PNG 重算。

`tools/verify_weather_icons.py` 可以在**不上板**的情况下排掉这个雷：把生成的 C 数组反解回图片，分别按小端 / 大端还原，与「源 PNG 经过同样 RGB565 量化」的参考图逐像素比对。字节序正确时小端误差必须为 **0**，大端对照误差约 60–90 / 通道；同时输出 `../tmp_weather_icons_verify.png`，16 张图标合成到黑底（模拟实机 LCD），可直接核对颜色与透明区。

### 中文字体（重要）

SquareLine 导出的字体是**子集**，只含设计稿里出现过的字符。`ui_font_OPPOSansH25` 只有 30 个汉字，写别的字会静默变成方框。

因此中文文本改用自己的 `lib/ui/fonts/ui_font_zh25.c`：GB2312 一级字库 3755 字 + ASCII + 常用标点，25px、**bpp=1**（与原始设计一致，纯黑白不做抗锯齿）、RLE 压缩，编译后约 **266KB**，只占 Flash 不占 RAM。

重新生成（需要 Node）：

```bash
# 只装一次
npm install lv_font_conv

# 生成：字符集见 tools/zh_charset_gb2312_l1.txt
npx lv_font_conv --size 25 --bpp 1 --format lvgl \
  --font "C:/Windows/Fonts/Deng.ttf" \
  --symbols "$(cat tools/zh_charset_gb2312_l1.txt)" \
  --lv-font-name ui_font_zh25 \
  -o lib/ui/fonts/ui_font_zh25.c
```

要点：

- `--lv-font-name` 必须和代码里的符号名一致
- 生成文件里是 `#include "lvgl/lvgl.h"`，PlatformIO 找不到这个相对路径，所以 `platformio.ini` 里加了 `-DLV_LVGL_H_INCLUDE_SIMPLE` 让它改用 `#include "lvgl.h"`
- 想换字形就换 `--font`：本机可选 `Deng.ttf`（等线）、`msyh.ttc`（微软雅黑）、`simhei.ttf`（黑体）
- 字号与 bpp 对体积影响很大：同字符集下 bpp=1 约 266KB、bpp=2 约 422KB、bpp=4 约 736KB

如果哪天要换成含生僻字或 emoji 的字库，注意 `ui_font_OPPOSansH18`（日期，24 字）和 `ui_font_OPPOSansL70`（温度大字，仅 ASCII + `°`）仍是子集，需要按同样方式重新生成。
