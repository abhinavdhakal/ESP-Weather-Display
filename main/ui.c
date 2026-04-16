/*
 * File: ui.c
 * Author: Abhinav Dhakal
 * Date: 2026-04-14
 * Notes: LVGL UI task for the weather screen.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "weather.h"

void lv_port_disp_init(void);
void lv_port_indev_init(void);

void display_task(void *pvParameters)
{
    weather_data_t data = {0};

    // LVGL setup lives in the display task.
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *temp_label = lv_label_create(lv_scr_act());
    lv_obj_align(temp_label, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_color(temp_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_14, LV_PART_MAIN);

    lv_obj_t *details_label = lv_label_create(lv_scr_act());
    lv_obj_align(details_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(details_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(details_label, &lv_font_montserrat_14, LV_PART_MAIN);

    lv_obj_t *desc_label = lv_label_create(lv_scr_act());
    lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_color(desc_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_14, LV_PART_MAIN);

    char temp_buf[64];
    char details_buf[96];

    while (1) {
        if (xQueueReceive(weather_queue, &data, pdMS_TO_TICKS(1000))) {
            snprintf(temp_buf, sizeof(temp_buf), "%.1f C", data.temperature);
            snprintf(details_buf, sizeof(details_buf), "RH %.0f%%  UV %.1f", data.humidity, data.uv_index);
            lv_label_set_text(temp_label, temp_buf);
            lv_label_set_text(details_label, details_buf);
            lv_label_set_text(desc_label, data.description);
        }

        // Keep LVGL ticking even if there is no new weather data.
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
