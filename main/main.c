/*
 * File: main.c
 * Author: Abhinav Dhakal
 * Date: 2026-04-14
 * Notes: App entry point and task startup.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "config.h"
#include "weather.h"

QueueHandle_t weather_queue = NULL;
SemaphoreHandle_t spi_mutex = NULL;

void wifi_init(void);

void app_main(void)
{
    // Shared queue + SPI lock for the two tasks.
    weather_queue = xQueueCreate(5, sizeof(weather_data_t));
    spi_mutex = xSemaphoreCreateMutex();

    if (weather_queue == NULL || spi_mutex == NULL) {
        printf("Failed to create queue or mutex\n");
        return;
    }

    wifi_init();

    // Weather on core 0, UI on core 1.
    xTaskCreatePinnedToCore(weather_task, "weather_task", 12288, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(display_task, "display_task", 8192, NULL, 5, NULL, 1);
}
