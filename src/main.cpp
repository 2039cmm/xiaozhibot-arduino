// ESP-SparkBot 屏幕最小显示 Demo（Arduino + Arduino_GFX）
// 硬件：ST7789 1.3" 240x240 RGB565，SPI3_HOST，无复位脚，背光 GPIO46
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ---- 引脚（来自原厂 BSP esp_sparkbot_bsp.h）----
#define TFT_SCK   21
#define TFT_MOSI  47
#define TFT_CS    44
#define TFT_DC    43
#define TFT_RST   GFX_NOT_DEFINED   // 原板未接复位，靠上电复位
#define TFT_BL    46

#ifndef HSPI
#define HSPI SPI3_HOST              // ESP32-S3: HSPI = SPI3
#endif

// 先用 40MHz 保守跑；稳定后可试 80MHz（原厂是 80MHz）
Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED, HSPI);

// 240x240，offset 全 0；若显示方向/颜色不对，先调 rotation / ips
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, TFT_RST, 0 /* rotation */, true /* IPS */, 240, 240, 0, 0, 0, 0);

static void drawColorBars() {
  const uint16_t colors[] = {RED, GREEN, BLUE, YELLOW, MAGENTA, CYAN, WHITE, BLACK};
  int h = gfx->height() / 8;
  for (int i = 0; i < 8; i++) {
    gfx->fillRect(0, i * h, gfx->width(), h, colors[i]);
  }
}

void setup() {
  // 背光先开
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP-SparkBot ST7789 demo start");

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(BLACK);

  // 简单彩条，确认整屏都能刷到
  drawColorBars();
  delay(800);

  // 文字 + 图形
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(3);
  gfx->setCursor(38, 90);
  gfx->println("SparkBot");

  gfx->setTextSize(2);
  gfx->setTextColor(GREEN);
  gfx->setCursor(60, 140);
  gfx->println("240x240 OK");

  gfx->drawCircle(120, 120, 100, BLUE);
  gfx->drawRect(10, 10, 220, 220, WHITE);

  Serial.println("draw done");
}

void loop() {
  // 背光闪烁，顺便确认 loop 正常
  static uint32_t last = 0;
  static bool on = true;
  if (millis() - last > 1000) {
    last = millis();
    on = !on;
    digitalWrite(TFT_BL, on ? HIGH : LOW);
  }
}
