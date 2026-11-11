// ui_port.c -- Arduino port layer for the SquareLine UI.
//
// 官方 ui.c 里有两类东西被生成的屏文件依赖：
//   1) lv_obj_t* 全局变量（ui_title / title_panel / ui_home / ui_hour ...）
//   2) 关键帧动画函数（riseup_Animation / falldown_Animation ...）
// 这里把这两部分搬过来，并去掉全部 IDF 专有依赖（bsp / lottie / camera 等）。
// 官方动画函数原样保留在 _anim_body.inc 中，本文件 include 进来。

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

#include "_anim_body.inc"


///////////////////// 事件与启动 ////////////////////

void ui_event_home(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SCREEN_LOAD_START) {
        riseup_Animation(ui_hour, 0);
        riseup_Animation(ui_min, 0);
        weather_Animation(ui_weathershow, 0);
        opaon_Animation(ui_weather, 0);
        opaon_Animation(ui_temp, 0);
        opaon_Animation(ui_weekday, 0);
        opaon_Animation(ui_date, 0);
        falldown_Animation(title_panel, 0);
        // 官方挂在 ui____initial_actions0 的 LOAD_START 上；移植时省掉了那个
        // 隐藏屏，直接并入 home 的加载事件，效果一致且少一次切屏。
        blink_Animation(ui_colon, 0);
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
    lv_theme_t *theme = lv_theme_default_init(
        disp,
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
