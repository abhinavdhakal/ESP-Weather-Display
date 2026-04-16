idf_component_register(
    SRCS "main.c" "wifi.c" "weather_api.c" "lvgl_port.c" "ui.c"
    INCLUDE_DIRS "."
    REQUIRES json esp_http_client esp-tls
    PRIV_REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_driver_spi esp_driver_gpio
)
