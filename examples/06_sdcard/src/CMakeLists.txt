idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_timer
        driver
        esp_lcd
        vfs
        sdmmc
        espressif__esp32_p4_function_ev_board
)
