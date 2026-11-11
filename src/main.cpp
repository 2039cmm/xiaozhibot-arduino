// ESP-SparkBot P1: LVGL 8.3.11 + 官方 SquareLine UI（title / home 两个屏）
//
// 硬件：ST7789 240x240，DC=43 CS=44 SCL=21 SDA=47 BLK=46，SPI mode 0
// 注意：43/44 与 UART0 复用，全程禁止 Serial，日志走 USBSerial。

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "lvgl.h"
#include "ui.h"

#define LOG USBSerial

// ---------------- 面板 ----------------
#define TFT_SCK   21
#define TFT_MOSI  47
#define TFT_CS    44
#define TFT_DC    43
#define TFT_RST   GFX_NOT_DEFINED
#define TFT_BL    46

#define TFT_SPI_INDEX HSPI
#define TFT_SPI_FREQ  40000000

#define SCREEN_W 240
#define SCREEN_H 240

// 这块屏只认 SPI mode 0；GFX 1.4.9 在 S3 上强制 mode 3，需子类改回
class ST7789Mode0 : public Arduino_ST7789 {
 public:
  using Arduino_ST7789::Arduino_ST7789;

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    _override_datamode = SPI_MODE0;
    return Arduino_TFT::begin(speed);
  }
};

static Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED, TFT_SPI_INDEX);

static Arduino_GFX *gfx = new ST7789Mode0(
    bus, TFT_RST, 0, true, SCREEN_W, SCREEN_H, 0, 0, 0, 0);

// ---------------- LVGL 缓冲区 ----------------
// 20 行一条，240*20*2 = 9600 字节，SRAM 够用
#define BUF_LINES 20
static lv_color_t lv_buf[SCREEN_W * BUF_LINES];

static void lv_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;

  // LV_COLOR_16_SWAP=0：LVGL 输出小端，由 GFX 的 writePixels 负责转大端
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);

  lv_disp_flush_ready(drv);
}

void setup()
{
  // 背光先开，确认这一路正常
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  LOG.begin();
  delay(600);

  LOG.println();
  LOG.println("========================================");
  LOG.println("  ESP-SparkBot P1 : LVGL + UI");
  LOG.println("========================================");
  LOG.printf("  chip  : %s\n", ESP.getChipModel());
  LOG.printf("  flash : %lu MB\n", (unsigned long)(ESP.getFlashChipSize() >> 20));
  LOG.printf("  psram : %lu\n", (unsigned long)ESP.getPsramSize());
  LOG.printf("  heap  : %lu\n", (unsigned long)ESP.getFreeHeap());
  LOG.flush();

  LOG.println("[1] lcd init");
  if (!gfx->begin(TFT_SPI_FREQ)) {
    LOG.println("    FAILED, halt");
    while (true) {
      digitalWrite(TFT_BL, LOW); delay(120);
      digitalWrite(TFT_BL, HIGH); delay(880);
    }
  }
  gfx->fillScreen(BLACK);
  LOG.println("    ok");

  LOG.println("[2] lvgl init");
  lv_init();

  static lv_disp_draw_buf_t draw_buf;
  lv_disp_draw_buf_init(&draw_buf, lv_buf, NULL, SCREEN_W * BUF_LINES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = lv_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  LOG.println("    ok");

  LOG.println("[3] ui init");
  ui_init();
  LOG.println("    ok");

  LOG.println("[4] ready");
}

void loop()
{
  static uint32_t last = 0;
  lv_timer_handler();
  delay(5);

  if (millis() - last > 5000) {
    last = millis();
    LOG.printf("[hb] heap=%lu\n", (unsigned long)ESP.getFreeHeap());
  }
}
