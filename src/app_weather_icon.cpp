// app_weather_icon.cpp -- 官方天气图标 + Arduino 侧动画
//
// 官方 ESP-IDF 固件把 61 张 110x110 PNG 转成 QOI 存进独立 mmap 分区，
// 靠 esp_lv_decoder / esp_mmap_assets 按和风天气代码动态加载。Arduino
// 工程没有分区表也没有那套解码器，所以这里改用编译期静态资源：
//   tools/gen_weather_icons.py 从官方 weather/*.png 挑 16 张转成
//   LV_IMG_CF_TRUE_COLOR_ALPHA 的 C 数组，直接 lv_img_set_src。
//
// 动画三层（官方只有第一层）：
//   1) 开机 weather_Animation() 把图标从 x=-200 滑到 -60（ui_port.c 里已有，
//      之前因为没设 src 完全看不见，现在能看到了）
//   2) 持续缓慢上下漂浮，4.4s 一个来回，不随天气切换重启，避免 y 跳变
//   3) 天气代码变化时 190 -> 255 的 zoom 弹跳 + 换图

#include "app_log.h"
#include "app_weather_icon.h"
#include "ui.h"

// 由 tools/gen_weather_icons.py 生成的静态资源
LV_IMG_DECLARE(ui_img_wx_100);   // 晴
LV_IMG_DECLARE(ui_img_wx_103);   // 晴间多云
LV_IMG_DECLARE(ui_img_wx_101);   // 多云
LV_IMG_DECLARE(ui_img_wx_104);   // 阴
LV_IMG_DECLARE(ui_img_wx_501);   // 雾
LV_IMG_DECLARE(ui_img_wx_300);   // 阵雨
LV_IMG_DECLARE(ui_img_wx_302);   // 雷阵雨
LV_IMG_DECLARE(ui_img_wx_304);   // 雷阵雨伴冰雹
LV_IMG_DECLARE(ui_img_wx_305);   // 小雨
LV_IMG_DECLARE(ui_img_wx_306);   // 中雨
LV_IMG_DECLARE(ui_img_wx_307);   // 大雨
LV_IMG_DECLARE(ui_img_wx_313);   // 冻雨
LV_IMG_DECLARE(ui_img_wx_400);   // 小雪
LV_IMG_DECLARE(ui_img_wx_401);   // 中雪
LV_IMG_DECLARE(ui_img_wx_402);   // 大雪
LV_IMG_DECLARE(ui_img_wx_999);   // 未知 / 未就绪

typedef struct {
  const lv_img_dsc_t *img;
  const char         *name;      // 官方和风天气代码，方便串口对账
} wx_icon_t;

static const wx_icon_t k_sunny   = { &ui_img_wx_100, "100" };
static const wx_icon_t k_partly  = { &ui_img_wx_103, "103" };
static const wx_icon_t k_cloudy  = { &ui_img_wx_101, "101" };
static const wx_icon_t k_overcast= { &ui_img_wx_104, "104" };
static const wx_icon_t k_fog     = { &ui_img_wx_501, "501" };
static const wx_icon_t k_shower  = { &ui_img_wx_300, "300" };
static const wx_icon_t k_thunder = { &ui_img_wx_302, "302" };
static const wx_icon_t k_hail    = { &ui_img_wx_304, "304" };
static const wx_icon_t k_rain_s  = { &ui_img_wx_305, "305" };
static const wx_icon_t k_rain_m  = { &ui_img_wx_306, "306" };
static const wx_icon_t k_rain_l  = { &ui_img_wx_307, "307" };
static const wx_icon_t k_freeze  = { &ui_img_wx_313, "313" };
static const wx_icon_t k_snow_s  = { &ui_img_wx_400, "400" };
static const wx_icon_t k_snow_m  = { &ui_img_wx_401, "401" };
static const wx_icon_t k_snow_l  = { &ui_img_wx_402, "402" };
static const wx_icon_t k_unknown = { &ui_img_wx_999, "999" };

// Open-Meteo WMO weather_code -> 官方和风天气图标
static const wx_icon_t *pick_icon(int code)
{
  switch (code) {
    case 0:                    return &k_sunny;
    case 1:                    return &k_partly;
    case 2:                    return &k_cloudy;
    case 3:                    return &k_overcast;
    case 45: case 48:          return &k_fog;
    case 51: case 53: case 55: return &k_rain_s;   // 毛毛雨
    case 56: case 57:          return &k_freeze;   // 冻毛毛雨
    case 61:                   return &k_rain_s;
    case 63:                   return &k_rain_m;
    case 65:                   return &k_rain_l;
    case 66: case 67:          return &k_freeze;
    case 71:                   return &k_snow_s;
    case 73:                   return &k_snow_m;
    case 75:                   return &k_snow_l;
    case 77:                   return &k_snow_s;   // 雪粒
    case 80: case 81: case 82: return &k_shower;
    case 85: case 86:          return &k_snow_s;   // 阵雪
    case 95:                   return &k_thunder;
    case 96: case 99:          return &k_hail;
    default:                   return &k_unknown;
  }
}

static const wx_icon_t *s_current = NULL;

// lv_anim 的 exec 回调签名是 void (*)(void *, int32_t)，
// 这里包一层，避免把 lv_obj_set_y / lv_img_set_zoom 直接强转。
static void set_y_cb(void *obj, int32_t v)
{
  lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)v);
}

static void set_zoom_cb(void *obj, int32_t v)
{
  lv_img_set_zoom((lv_obj_t *)obj, (uint16_t)v);
}

// ui_weathershow 在 ui_home.c 里是 LV_ALIGN_CENTER + y=-54。
// 漂浮范围 [-58, -50]，即上下各 4px，绕设计位置对称。
static void start_float_anim(void)
{
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ui_weathershow);
  lv_anim_set_exec_cb(&a, set_y_cb);
  lv_anim_set_values(&a, -58, -50);
  lv_anim_set_time(&a, 2200);
  lv_anim_set_playback_time(&a, 2200);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

static void pop_zoom_anim(void)
{
  // 天气 15 分钟才变一次，但保险起见先撤掉同对象上未结束的 zoom 动画
  lv_anim_del(ui_weathershow, set_zoom_cb);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ui_weathershow);
  lv_anim_set_exec_cb(&a, set_zoom_cb);
  lv_anim_set_values(&a, 190, 255);
  lv_anim_set_time(&a, 380);
  lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
  lv_anim_start(&a);
}

void app_weather_icon_init(void)
{
  if (ui_weathershow == NULL) {
    LOG.println("[wxicon] FATAL: ui_weathershow is NULL");
    return;
  }

  s_current = &k_unknown;
  lv_img_set_src(ui_weathershow, s_current->img);
  lv_obj_invalidate(ui_weathershow);
  start_float_anim();

  LOG.println("[wxicon] init, default icon 999, float anim on");
}

void app_weather_icon_update(int wmo_code, bool valid)
{
  if (ui_weathershow == NULL || s_current == NULL) return;

  const wx_icon_t *next = valid ? pick_icon(wmo_code) : &k_unknown;
  if (next == s_current) return;          // 同一张图，不做任何重绘

  s_current = next;
  lv_img_set_src(ui_weathershow, next->img);
  lv_obj_invalidate(ui_weathershow);
  pop_zoom_anim();

  LOG.printf("[wxicon] wmo=%d valid=%d -> icon %s\n",
             wmo_code, (int)valid, next->name);
}
