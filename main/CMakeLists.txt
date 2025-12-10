# Main component CMakeLists.txt

idf_component_register(
    SRCS 
        "main.c"
        "motor_control.c"
        "encoder.c"
        "fuzzy_control.c"
        "wifi_manager.c"
        "websocket_server.c"
        "dht_sensor.c"
    INCLUDE_DIRS "."
    REQUIRES 
        driver
        esp_timer
        nvs_flash
        spi_flash
        esp_wifi
        esp_netif
        esp_http_server
        json
        esp_wifi_remote
        esp_hosted
)