idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_timer
        esp_event
        esp_netif
        esp_wifi
        driver
        esp_lcd
        espressif__esp32_p4_function_ev_board
)
