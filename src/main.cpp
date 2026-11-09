// ESP-SparkBot 屏幕点屏验证（Arduino + Arduino_GFX）
// 硬件：ST7789 1.3" 240x240 RGB565，SPI3_HOST，无复位脚，背光 GPIO46
//
// 【根因】本板 LCD 的 DC=GPIO43、CS=GPIO44，正好是 ESP32-S3 的 UART0 TXD/RXD。
// 之前用 Serial.begin(115200) 会把 43/44 从 GPIO 切成 UART 功能，LCD 的
// DC/CS 直接失效 -> 屏幕全黑；同时日志全从这两根 LCD 控制线跑出去，
// USB 口一个字都收不到，于是表现为"既没画面也没串口输出"。
// 结论：本板绝对不能用 Serial（UART0），日志改用 USBSerial（HWCDC）。

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// 日志通道：USB-Serial-JTAG，占用 GPIO19/20，与 LCD 无关
#define LOG USBSerial

// ---- 引脚（来自原厂 BSP esp_sparkbot_bsp.h）----
#define TFT_SCK   21
#define TFT_MOSI  47
#define TFT_CS    44   // 与 UART0 RXD 复用，禁止 Serial
#define TFT_DC    43   // 与 UART0 TXD 复用，禁止 Serial
#define TFT_RST   GFX_NOT_DEFINED   // 原板未接复位，靠上电复位
#define TFT_BL    46

// GFX 1.4.9 的 Arduino_ESP32SPI 用「总线索引」而非 host id：
// S3 上 HSPI == 1，对应 SPI3_HOST；不要写成 SPI3_HOST(=2)，那样会越界。
#define TFT_SPI_INDEX  HSPI
#define TFT_SPI_FREQ   40000000     // 原厂 80MHz，这里先保守跑 40MHz

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED, TFT_SPI_INDEX);

// 240x240，offset 全 0；若显示方向/颜色不对，先调 rotation / ips
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, TFT_RST, 0 /* rotation */, true /* IPS */, 240, 240, 0, 0, 0, 0);

static inline void backlight(bool on) {
  digitalWrite(TFT_BL, on ? HIGH : LOW);
}

// 失败时让背光明显闪烁，避免"黑屏但其实是跑挂了"无法区分
static void failBlink(const char *reason) {
  LOG.printf("    FAILED: %s\n", reason);
  while (true) {
    backlight(false);
    delay(120);
    backlight(true);
    delay(880);
  }
}

static void drawColorBars() {
  const uint16_t colors[] = {RED, GREEN, BLUE, YELLOW, MAGENTA, CYAN, WHITE, BLACK};
  int h = gfx->height() / 8;
  for (int i = 0; i < 8; i++) {
    gfx->fillRect(0, i * h, gfx->width(), h, colors[i]);
  }
}

void setup() {
  // 1) 最先开背光：即使后面 SPI 失败，也能确认背光这一路是通的
  pinMode(TFT_BL, OUTPUT);
  backlight(true);

  LOG.begin();
  delay(600);   // 等 USB 枚举/上位机打开

  LOG.println();
  LOG.println("========================================");
  LOG.println("  ESP-SparkBot ST7789 bring-up");
  LOG.println("========================================");
  LOG.printf("  chip      : %s  rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  LOG.printf("  cpu       : %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  LOG.printf("  flash     : %lu MB\n", (unsigned long)(ESP.getFlashChipSize() >> 20));
  LOG.printf("  psram     : %lu KB\n", (unsigned long)(ESP.getPsramSize() >> 10));
  LOG.printf("  free heap : %lu\n", (unsigned long)ESP.getFreeHeap());
  LOG.printf("  pins      : sck=%d mosi=%d cs=%d dc=%d bl=%d\n",
             TFT_SCK, TFT_MOSI, TFT_CS, TFT_DC, TFT_BL);

  // 2) 初始化面板
  LOG.println("[1] gfx->begin() ...");
  if (!gfx->begin(TFT_SPI_FREQ)) {
    failBlink("gfx->begin() returned false");
  }
  LOG.println("    ok");

  // 3) 彩条，确认整屏都能刷到
  LOG.println("[2] color bars");
  gfx->fillScreen(BLACK);
  drawColorBars();
  delay(1200);

  // 4) 文字 + 图形
  LOG.println("[3] text and shapes");
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(3);
  gfx->setCursor(38, 90);
  gfx->println("SparkBot");

  gfx->setTextSize(2);
  gfx->setTextColor(GREEN);
  gfx->setCursor(46, 140);
  gfx->println("240x240 OK");

  gfx->drawCircle(120, 120, 100, BLUE);
  gfx->drawRect(10, 10, 220, 220, WHITE);

  LOG.println("[4] draw done, entering loop");
}

void loop() {
  // 心跳：证明 loop 在跑；同时把运行秒数刷到屏幕底部
  static uint32_t last = 0;
  static uint32_t sec = 0;
  if (millis() - last > 1000) {
    last = millis();
    sec++;
    LOG.printf("[hb] %lu s, heap=%lu\n", (unsigned long)sec, (unsigned long)ESP.getFreeHeap());

    gfx->fillRect(0, 200, 240, 24, BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(CYAN);
    gfx->setCursor(60, 202);
    gfx->printf("%lu s", (unsigned long)sec);
  }
}
