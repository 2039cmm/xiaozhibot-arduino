// ui_port.c -- Arduino port layer for the SquareLine UI.
// P1 scope: only ui_title (status bar) and ui_home (clock/weather).
//
// 原版 ui.c 里既声明了这些 lv_obj_t* 全局变量，又实现了动画与事件；
// 这里把那部分"被 ui_title.c / ui_home.c 依赖的"内容搬过来，
// 并去掉所有 IDF 专有依赖（esp_check / bsp / lottie / camera 等）。

#include "ui.h"

ui_page_name_t ui_pages[7];

// ---- SCREEN: ui_title ----
lv_obj_t *ui_title;
lv_obj_t *title_panel;
lv_obj_t *title_wifistate;
lv_obj_t *title_powerstate;
lv_obj_t *title_batterybor;
lv_obj_t *title_timestate;
lv_obj_t *title_batterytxt;

// ---- SCREEN: ui_home ----
lv_obj_t *ui_home;
lv_obj_t *ui_Panel1;
lv_obj_t *ui_hour;
lv_obj_t *ui_min;
lv_obj_t *ui_date;
lv_obj_t *ui_weekday;
lv_obj_t *ui_temp;
lv_obj_t *ui_weather;
lv_obj_t *ui_colon;
lv_obj_t *ui_weathershow;

int gongde_sum = 0;

// ---- 动画：P1 先置空，只保证接口存在 ----
void muyushow_Animation(lv_obj_t *t, int delay) { (void)t; (void)delay; }
void blink_Animation(lv_obj_t *t, int delay)    { (void)t; (void)delay; }
void falldown_Animation(lv_obj_t *t, int delay) { (void)t; (void)delay; }
void weather_Animation(lv_obj_t *t, int delay) { (void)t; (void)delay; }
void riseup_Animation(lv_obj_t *t, int delay)   { (void)t; (void)delay; }
void opaon_Animation(lv_obj_t *t, int delay)    { (void)t; (void)delay; }
void gonde_txt_Animation(lv_obj_t *t, int delay) { (void)t; (void)delay; }

// ---- 事件：P1 只有两个屏，按 NEXT 在两者间切换 ----
void ui_event_home(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SCREEN_LOAD_START) {
        lv_obj_set_parent(title_panel, ui_Panel1);
    }
}

void ui_send_sys_event(lv_obj_t *obj, lv_event_code_t code, void *user_data)
{
    if (!obj) {
        return;
    }
    lv_event_send(obj, code, user_data);
}

void ui_init(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp,
                                              lv_palette_main(LV_PALETTE_BLUE),
                                              lv_palette_main(LV_PALETTE_RED),
                                              false,
                                              LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, theme);

    ui_title_screen_init();
    ui_home_screen_init();

    ui_pages[0].page = ui_home;
    ui_pages[0].name = "home";
    ui_pages[1].page = ui_title;
    ui_pages[1].name = "title";

    for (int i = 2; i < 7; i++) {
        ui_pages[i].page = 0;
        ui_pages[i].name = "";
    }

    lv_disp_load_scr(ui_home);
}
