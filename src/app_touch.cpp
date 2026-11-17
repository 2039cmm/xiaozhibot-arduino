// app_touch.cpp -- 电容触摸键（3 键翻页 / 长按回首页）
//
// 官方 ESP-IDF BSP 用 touch_element + iot_button：通道 TOUCH_PAD_NUM1/2/3，
// 灵敏度 0.035/0.08/0.08，长按 2000ms。Arduino 框架没有 touch_element，
// 这里用 touchRead() 采样 + 自适应阈值复刻同样的交互：
//   T1(顶/GPIO1) 短按 = 回首页；T2(左/GPIO2) = 上一页；T3(右/GPIO3) = 下一页
//   任意键长按 2 秒 -> 回首页
//
// 【读数方向】touchRead() 走 touch_pad_read_raw_data()；ESP32-S3 上电容增大
// 时读数上升。这里上升/下降超阈值都算按下，兼容两种方向。
//
// 【诊断开关】TOUCH_DIAG=1 时每 350ms 打印各通道实时值，定位用完后改回 0。

#include <Arduino.h>

#include "app_log.h"
#include "app_touch.h"
#include "app_audio.h"
#include "ui.h"

#define NUM_KEYS          3
#define BASELINE_SAMPLES  24
#define DEBOUNCE_SAMPLES   3
#define LONGPRESS_MS    2000
#define MIN_DELTA       200          // 至少变化这么多计数（避免小漂移误判）
#define TOUCH_DIAG        0          // 1=打印实时读数（定位用），0=正式

static const uint8_t  k_pins[NUM_KEYS]  = { 1, 2, 3 };
static const char    *k_names[NUM_KEYS] = { "T1(top)", "T2(left)", "T3(right)" };
// 灵敏度：按实机实测调。T1 静止 32145、按到 58914(+83%)；T2 静止 27726、
// 按到 74994(+170%)。T1 用 15% 阈值避开轻触误触（轻触只到 +6%），
// T2/T3 用 10%。数值越大越不灵敏。
static const float    k_sens[NUM_KEYS]  = { 0.15f, 0.10f, 0.10f };

typedef struct {
  uint32_t base, cur;
  uint8_t  below;
  bool     pressed, fired_long;
  uint32_t press_at;
} touch_state_t;

static touch_state_t s_k[NUM_KEYS];
static int s_page_idx = 0;

// ---------------- 屏幕提示（按哪片显示哪片）----------------
// 用 lv_layer_top() 上的一个标签做 toast，1 秒后自动隐藏。
static lv_obj_t *s_toast = NULL;
static uint32_t  s_toast_hide_at = 0;

static void toast_show(const char *txt)
{
  if (!s_toast) {
    s_toast = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_color(s_toast, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_toast, 4, 0);
    lv_obj_set_style_radius(s_toast, 4, 0);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -10);
  }
  lv_label_set_text(s_toast, txt);
  lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  s_toast_hide_at = millis() + 900;
}

static void toast_tick(void)
{
  if (s_toast && !lv_obj_has_flag(s_toast, LV_OBJ_FLAG_HIDDEN)
      && (int32_t)(millis() - s_toast_hide_at) >= 0) {
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---------------- 页面切换 ----------------

static int page_count(void)
{
  int n = 0;
  while (n < 7 && ui_pages[n].page != NULL) n++;
  return n;
}

static void goto_page(int idx)
{
  const int n = page_count();
  if (n <= 0) return;
  idx = ((idx % n) + n) % n;
  s_page_idx = idx;
  lv_disp_load_scr(ui_pages[idx].page);
  LOG.printf("[touch] page -> %d/%d (%s)\n", idx + 1, n, ui_pages[idx].name);
}

static void on_short_press(int i)
{
  LOG.printf("[touch] %s short press\n", k_names[i]);
  toast_show(k_names[i]);                       // 屏幕上显示按了哪片
  if (app_audio_ready()) {
    app_audio_tone(1800, 22);                   // 短促「嗒」声
  }
  switch (i) {
    case 0: goto_page(0); break;                 // T1 回首页
    case 1: goto_page(s_page_idx - 1); break;    // T2 上一页
    case 2: goto_page(s_page_idx + 1); break;    // T3 下一页
  }
}

static void on_long_press(int i)
{
  LOG.printf("[touch] %s LONG press -> home\n", k_names[i]);
  toast_show("LONG");
  if (app_audio_ready()) {
    app_audio_tone(1100, 40);                   // 长按低音区分
  }
  goto_page(0);
}

// ---------------- 初始化 ----------------

void app_touch_init(void)
{
  LOG.println("[touch] baseline sample:");
  for (int i = 0; i < NUM_KEYS; i++) {
    uint32_t sum = 0;
    for (int n = 0; n < BASELINE_SAMPLES; n++) {
      sum += touchRead(k_pins[i]);
      delay(4);
    }
    s_k[i].base       = sum / BASELINE_SAMPLES;
    s_k[i].cur        = s_k[i].base;
    s_k[i].below      = 0;
    s_k[i].pressed    = false;
    s_k[i].fired_long = false;
    s_k[i].press_at   = 0;
    LOG.printf("[touch] %s GPIO%d base=%lu\n",
               k_names[i], k_pins[i], (unsigned long)s_k[i].base);
  }
  LOG.println("[touch] ready");
}

// ---------------- 轮询 ----------------

void app_touch_loop(void)
{
  const uint32_t now = millis();

#if TOUCH_DIAG
  static uint32_t s_diag_at = 0;
  if (now - s_diag_at >= 350) {
    s_diag_at = now;
    LOG.print("[diag]");
    for (int i = 0; i < NUM_KEYS; i++) {
      const int32_t d = (int32_t)s_k[i].cur - (int32_t)s_k[i].base;
      const int pct = s_k[i].base ? (int)(d * 100 / s_k[i].base) : 0;
      LOG.printf(" %s=%lu(%+d%%)", k_names[i], (unsigned long)s_k[i].cur, pct);
    }
    LOG.println();
  }
#endif

  for (int i = 0; i < NUM_KEYS; i++) {
    touch_state_t *t = &s_k[i];
    t->cur = touchRead(k_pins[i]);

    const uint32_t th_lo = (uint32_t)(t->base * (1.0f - k_sens[i]));
    const uint32_t th_hi = (uint32_t)(t->base * (1.0f + k_sens[i]));
    const bool drop = (t->cur < th_lo) && ((t->base - t->cur) >= MIN_DELTA);
    const bool rise = (t->cur > th_hi) && ((t->cur - t->base) >= MIN_DELTA);
    const bool touched = drop || rise;

    if (touched) {
      if (t->below < 255) t->below++;
    } else {
      t->below = 0;
    }

    if (!t->pressed && t->below >= DEBOUNCE_SAMPLES) {
      t->pressed = true;
      t->fired_long = false;
      t->press_at = now;
      LOG.printf("[touch] %s PRESSED base=%lu cur=%lu\n",
                 k_names[i], (unsigned long)t->base, (unsigned long)t->cur);
    } else if (t->pressed && !touched) {
      t->pressed = false;
      if (!t->fired_long) on_short_press(i);
    } else if (t->pressed && !t->fired_long &&
               (now - t->press_at) >= LONGPRESS_MS) {
      t->fired_long = true;
      on_long_press(i);
    }
  }

  toast_tick();                                 // 驱动提示标签的自动隐藏
}

void app_touch_debug(void)
{
  LOG.print("[touch] ");
  for (int i = 0; i < NUM_KEYS; i++) {
    LOG.printf("%s=%lu/%lu  ", k_names[i],
               (unsigned long)touchRead(k_pins[i]),
               (unsigned long)s_k[i].base);
  }
  LOG.println();
}
