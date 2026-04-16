/*
 * File: weather.h
 * Author: Abhinav Dhakal
 * Date: 2026-04-14
 * Notes: Shared types and task entry points.
 */
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

typedef struct {
    float temperature;
    float humidity;
    float uv_index;
    char description[64];
} weather_data_t;

extern QueueHandle_t weather_queue;
extern SemaphoreHandle_t spi_mutex;
extern EventGroupHandle_t wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0

// Task entry points
void weather_task(void *pvParameters);
void display_task(void *pvParameters);
