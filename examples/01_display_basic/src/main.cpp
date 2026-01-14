/**
 * @file main.cpp
 * @brief Example 01: Basic Display for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - MIPI-DSI display initialization with ST7701 controller
 * - LVGL 9 basic UI (label + button)
 * - Touch input (GT911)
 * - Backlight control
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * Display: 480x800 MIPI-DSI, ST7701 controller
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char *TAG = "display_basic";

// Button click counter
static int click_count = 0;
static lv_obj_t *count_label = NULL;

/**
 * @brief Button click callback
 */
static void btn_click_cb(lv_event_t *e) {
    click_count++;
    if (count_label) {
        lv_label_set_text_fmt(count_label, "Clicked: %d times", click_count);
    }
    ESP_LOGI(TAG, "Button clicked! Count: %d", click_count);
}

/**
 * @brief Create demo UI
 */
static void create_demo_ui(void) {
    // Get default display
    lv_obj_t *scr = lv_scr_act();

    // Set background color
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003366), LV_PART_MAIN);

    // Create title label
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "JC4880P443C Display Demo");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    // Create info label
    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text(info, "ESP32-P4 + MIPI-DSI + LVGL 9");
    lv_obj_set_style_text_color(info, lv_color_hex(0x88CCFF), LV_PART_MAIN);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 100);

    // Create button
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED, NULL);

    // Button label
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click Me!");
    lv_obj_center(btn_label);

    // Create click counter label
    count_label = lv_label_create(scr);
    lv_label_set_text(count_label, "Clicked: 0 times");
    lv_obj_set_style_text_color(count_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(count_label, LV_ALIGN_CENTER, 0, 80);

    // Create resolution label
    lv_obj_t *res_label = lv_label_create(scr);
    lv_label_set_text_fmt(res_label, "Resolution: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_style_text_color(res_label, lv_color_hex(0x88CCFF), LV_PART_MAIN);
    lv_obj_align(res_label, LV_ALIGN_BOTTOM_MID, 0, -50);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C Basic Display Example");
    ESP_LOGI(TAG, "  ESP32-P4 MIPI-DSI + LVGL 9");
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize display using BSP
    ESP_LOGI(TAG, "Initializing display...");

    // Configure display with SPIRAM buffer
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 50,  // 50 lines buffer
        .double_buffer = false,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,  // Use PSRAM for buffer
            .sw_rotate = true,
        }
    };

    lv_display_t *disp = bsp_display_start_with_config(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display!");
        return;
    }
    ESP_LOGI(TAG, "Display initialized successfully");

    // Turn on backlight
    bsp_display_backlight_on();
    bsp_display_brightness_set(100);
    ESP_LOGI(TAG, "Backlight ON");

    // Create demo UI (with display lock)
    bsp_display_lock(0);
    create_demo_ui();
    bsp_display_unlock();
    ESP_LOGI(TAG, "Demo UI created");

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Display ready! Touch the button.");
    ESP_LOGI(TAG, "========================================");

    // Main loop - just print heap info periodically
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
