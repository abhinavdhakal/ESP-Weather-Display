/*
 * File: lvgl_port.c
 * Author: Abhinav Dhakal
 * Date: 2026-04-14
 * Notes: LVGL display + touch port for ILI9341 + XPT2046.
 */
#include "lvgl.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "weather.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "lvgl_port";

// VSPI wiring for a 2.4" ILI9341 + XPT2046 combo board.
#define LCD_PIN_MOSI 23
#define LCD_PIN_SCLK 18
#define LCD_PIN_MISO 19
#define LCD_PIN_CS   5
#define LCD_PIN_DC   21
#define LCD_PIN_RST  22
#define LCD_PIN_BL   4

#define TOUCH_PIN_CS  15
#define TOUCH_PIN_IRQ 34

#define SPI_HOST_USED VSPI_HOST

#define DISP_HOR_RES 240
#define DISP_VER_RES 320
#define DISP_BUF_PIXELS (DISP_HOR_RES * 40)

static spi_device_handle_t lcd_spi = NULL;
static spi_device_handle_t touch_spi = NULL;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;

static uint8_t s_cmd_buf[4];
static uint8_t s_data_buf[32];

static inline void spi_lock(void)
{
    if (spi_mutex) {
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
    }
}

static inline void spi_unlock(void)
{
    if (spi_mutex) {
        xSemaphoreGive(spi_mutex);
    }
}

static void lcd_cmd(uint8_t cmd)
{
    s_cmd_buf[0] = cmd;
    spi_transaction_t t = {0};
    t.length = 8;
    t.tx_buffer = s_cmd_buf;
    gpio_set_level(LCD_PIN_DC, 0);
    spi_device_transmit(lcd_spi, &t);
}

static void lcd_data(const uint8_t *data, int len)
{
    spi_transaction_t t = {0};
    t.length = len * 8;
    t.tx_buffer = data;
    gpio_set_level(LCD_PIN_DC, 1);
    spi_device_transmit(lcd_spi, &t);
}

static void lcd_write_u8(uint8_t cmd, const uint8_t *data, int len)
{
    lcd_cmd(cmd);
    if (len > 0) {
        if (len <= (int)sizeof(s_data_buf)) {
            memcpy(s_data_buf, data, len);
            lcd_data(s_data_buf, len);
        } else {
            lcd_data(data, len);
        }
    }
}

static void set_address_window(int x0, int y0, int x1, int y1)
{
    uint8_t data[4];
    lcd_cmd(0x2A);
    data[0] = (x0 >> 8) & 0xFF;
    data[1] = x0 & 0xFF;
    data[2] = (x1 >> 8) & 0xFF;
    data[3] = x1 & 0xFF;
    lcd_data(data, 4);

    lcd_cmd(0x2B);
    data[0] = (y0 >> 8) & 0xFF;
    data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF;
    data[3] = y1 & 0xFF;
    lcd_data(data, 4);

    lcd_cmd(0x2C);
}

static esp_err_t spi_bus_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISP_HOR_RES * DISP_VER_RES * 2,
    };

    esp_err_t ret = spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", ret);
        return ret;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = LCD_PIN_CS,
        .queue_size = 7,
    };
    ret = spi_bus_add_device(SPI_HOST_USED, &devcfg, &lcd_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device(lcd) failed: %d", ret);
        return ret;
    }

    spi_device_interface_config_t touchcfg = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = TOUCH_PIN_CS,
        .queue_size = 3,
    };
    ret = spi_bus_add_device(SPI_HOST_USED, &touchcfg, &touch_spi);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "spi_bus_add_device(touch) failed: %d", ret);
        touch_spi = NULL;
    }

    return ESP_OK;
}

static void ili9341_init(void)
{
    uint8_t data[16];

    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    data[0] = 0x01; data[1] = 0x00; data[2] = 0x34;
    lcd_write_u8(0xEF, data, 3);

    data[0] = 0x39; data[1] = 0x2C; data[2] = 0x00; data[3] = 0x34; data[4] = 0x02;
    lcd_write_u8(0xCF, data, 5);

    data[0] = 0x85; data[1] = 0x01; data[2] = 0x79;
    lcd_write_u8(0xED, data, 3);

    data[0] = 0x00; data[1] = 0xC1; data[2] = 0x30;
    lcd_write_u8(0xE8, data, 3);

    data[0] = 0x64; data[1] = 0x03; data[2] = 0x12; data[3] = 0x81; data[4] = 0xC0;
    lcd_write_u8(0xCB, data, 5);

    data[0] = 0x20;
    lcd_write_u8(0xF7, data, 1);

    data[0] = 0x00; data[1] = 0x00;
    lcd_write_u8(0xEA, data, 2);

    data[0] = 0x23;
    lcd_write_u8(0xC0, data, 1);

    data[0] = 0x10;
    lcd_write_u8(0xC1, data, 1);

    data[0] = 0x3E; data[1] = 0x28;
    lcd_write_u8(0xC5, data, 2);

    data[0] = 0x86;
    lcd_write_u8(0xC7, data, 1);

    data[0] = 0x48;
    lcd_write_u8(0x36, data, 1);

    data[0] = 0x55;
    lcd_write_u8(0x3A, data, 1);

    data[0] = 0x00; data[1] = 0x18;
    lcd_write_u8(0xB1, data, 2);

    data[0] = 0x08; data[1] = 0x82; data[2] = 0x27;
    lcd_write_u8(0xB6, data, 3);

    data[0] = 0x00;
    lcd_write_u8(0xF2, data, 1);

    data[0] = 0x01;
    lcd_write_u8(0x26, data, 1);

    lcd_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    int32_t w = x2 - x1 + 1;
    int32_t h = y2 - y1 + 1;

    spi_lock();
    set_address_window(x1, y1, x2, y2);
    gpio_set_level(LCD_PIN_DC, 1);

    spi_transaction_t t = {0};
    t.length = w * h * 16;
    t.tx_buffer = color_map;
    spi_device_transmit(lcd_spi, &t);
    spi_unlock();

    lv_disp_flush_ready(drv);
}

static int xpt_read_raw(uint8_t cmd)
{
    if (touch_spi == NULL) return -1;
    uint8_t tx[3] = {cmd, 0x00, 0x00};
    uint8_t rx[3] = {0};
    spi_transaction_t t = {0};
    t.length = 8 * 3;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    if (spi_device_transmit(touch_spi, &t) != ESP_OK) return -1;
    return ((rx[1] << 8) | rx[2]) >> 3;
}

static int map_val(int v, int in_min, int in_max, int out_min, int out_max)
{
    if (v < in_min) v = in_min;
    if (v > in_max) v = in_max;
    return (v - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->state = LV_INDEV_STATE_REL;
    if (touch_spi == NULL) return;
    if (gpio_get_level(TOUCH_PIN_IRQ) != 0) return;

    spi_lock();
    int rx = xpt_read_raw(0xD0);
    int ry = xpt_read_raw(0x90);
    spi_unlock();

    if (rx < 0 || ry < 0) return;

    // Basic raw->screen mapping; tune if your panel is rotated.
    int x = map_val(rx, 200, 3800, 0, DISP_HOR_RES - 1);
    int y = map_val(ry, 200, 3800, 0, DISP_VER_RES - 1);
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
}

static void lv_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

void lv_port_disp_init(void)
{
    gpio_config_t io_conf = {0};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_RST) | (1ULL << LCD_PIN_BL);
    gpio_config(&io_conf);

    gpio_config_t irq_conf = {0};
    irq_conf.mode = GPIO_MODE_INPUT;
    irq_conf.pin_bit_mask = (1ULL << TOUCH_PIN_IRQ);
    gpio_config(&irq_conf);

    gpio_set_level(LCD_PIN_BL, 1);

    if (spi_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "SPI init failed");
        return;
    }

    ili9341_init();

    // LVGL buffer must live in DMA-capable memory for SPI transfers.
    buf1 = heap_caps_malloc(sizeof(lv_color_t) * DISP_BUF_PIXELS, MALLOC_CAP_DMA);
    if (buf1 == NULL) {
        ESP_LOGE(TAG, "LVGL buffer alloc failed");
        return;
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DISP_BUF_PIXELS);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_HOR_RES;
    disp_drv.ver_res = DISP_VER_RES;
    disp_drv.flush_cb = lvgl_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static esp_timer_handle_t lv_tick_timer;
    const esp_timer_create_args_t tick_args = {
        .callback = lv_tick_cb,
        .name = "lv_tick",
    };
    if (esp_timer_create(&tick_args, &lv_tick_timer) == ESP_OK) {
        esp_timer_start_periodic(lv_tick_timer, 1000);
    }
}

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);
}

