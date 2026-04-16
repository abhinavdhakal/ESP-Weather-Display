/*
 * File: weather_api.c
 * Author: Abhinav Dhakal
 * Date: 2026-04-14
 * Notes: Open-Meteo fetch + parse, posts updates to the queue.
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "config.h"
#include "weather.h"

static const char *TAG = "weather_api";

typedef struct {
    char *buffer;
    size_t buffer_size;
    size_t bytes_written;
} http_response_t;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && response != NULL && evt->data != NULL && evt->data_len > 0) {
        size_t space_left = (response->buffer_size > response->bytes_written) ? (response->buffer_size - response->bytes_written - 1) : 0;
        if (space_left > 0) {
            size_t copy_len = (evt->data_len < space_left) ? evt->data_len : space_left;
            memcpy(response->buffer + response->bytes_written, evt->data, copy_len);
            response->bytes_written += copy_len;
            response->buffer[response->bytes_written] = '\0';
        }
    }
    return ESP_OK;
}

void weather_task(void *pvParameters)
{
    static char buffer[4096];

    if (wifi_event_group != NULL) {
        xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    }

    while (1) {
        ESP_LOGI(TAG, "Fetching weather from Open-Meteo...");
        // Reuse a fixed buffer to keep heap usage stable.
        http_response_t response = {
            .buffer = buffer,
            .buffer_size = sizeof(buffer),
            .bytes_written = 0,
        };
        buffer[0] = '\0';
        // Build Open-Meteo URL (no API key required).
        char path[512];
        snprintf(path, sizeof(path),
               "/v1/forecast?latitude=%s&longitude=%s&hourly=temperature_2m,relative_humidity_2m,uv_index,weather_code&forecast_hours=1&timezone=auto",
             LOCATION_LAT_STR, LOCATION_LON_STR);

        esp_http_client_config_t config = {
            .host = "api.open-meteo.com",
            .path = path,
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .event_handler = _http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .user_data = &response,
            .timeout_ms = 10000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (esp_http_client_perform(client) == ESP_OK) {
            ESP_LOGI(TAG, "HTTP request completed, bytes=%u", (unsigned)response.bytes_written);
            if (response.bytes_written > 0) {
                cJSON *root = cJSON_Parse(buffer);
                if (root) {
                    weather_data_t data = {0};

                    // Pull the first hour sample and send it to the UI task.
                    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
                    int weathercode = -1;
                    if (hourly) {
                        cJSON *temps = cJSON_GetObjectItem(hourly, "temperature_2m");
                        cJSON *humidity = cJSON_GetObjectItem(hourly, "relative_humidity_2m");
                        cJSON *uv = cJSON_GetObjectItem(hourly, "uv_index");
                        cJSON *codes = cJSON_GetObjectItem(hourly, "weather_code");

                        if (temps && cJSON_IsArray(temps) && cJSON_GetArraySize(temps) > 0) {
                            cJSON *t0 = cJSON_GetArrayItem(temps, 0);
                            if (t0) data.temperature = (float)t0->valuedouble;
                        }
                        if (humidity && cJSON_IsArray(humidity) && cJSON_GetArraySize(humidity) > 0) {
                            cJSON *h0 = cJSON_GetArrayItem(humidity, 0);
                            if (h0) data.humidity = (float)h0->valuedouble;
                        }
                        if (uv && cJSON_IsArray(uv) && cJSON_GetArraySize(uv) > 0) {
                            cJSON *u0 = cJSON_GetArrayItem(uv, 0);
                            if (u0) data.uv_index = (float)u0->valuedouble;
                        }
                        if (codes && cJSON_IsArray(codes) && cJSON_GetArraySize(codes) > 0) {
                            cJSON *c0 = cJSON_GetArrayItem(codes, 0);
                            if (c0) weathercode = c0->valueint;
                        }
                    }

                    // Map weathercode to simple description
                    const char *wc_desc = "Unknown";
                    switch (weathercode) {
                        case 0: wc_desc = "Clear"; break;
                        case 1: case 2: case 3: wc_desc = "Partly cloudy"; break;
                        case 45: case 48: wc_desc = "Fog"; break;
                        case 51: case 53: case 55: wc_desc = "Drizzle"; break;
                        case 61: case 63: case 65: wc_desc = "Rain"; break;
                        case 80: case 81: case 82: wc_desc = "Rain Showers"; break;
                        case 95: case 96: case 99: wc_desc = "Thunderstorm"; break;
                        default: break;
                    }
                    strncpy(data.description, wc_desc, sizeof(data.description)-1);

                    cJSON_Delete(root);

                    // Push to queue
                    if (weather_queue) {
                        xQueueSend(weather_queue, &data, portMAX_DELAY);
                        ESP_LOGI(TAG, "Weather updated: %.1f C, %.1f%% RH, UV %.1f, %s",
                                 data.temperature, data.humidity, data.uv_index, data.description);
                    }
                } else {
                    ESP_LOGW(TAG, "JSON parse failed");
                }
            } else {
                ESP_LOGW(TAG, "No response body read from HTTP client");
            }
        } else {
            ESP_LOGW(TAG, "HTTP request failed");
        }

        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
