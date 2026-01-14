/**
 * @file main.cpp
 * @brief Example 08: Reset Device for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - Different reset methods (software reset, restart)
 * - Reading reset reason on boot
 * - Countdown before reset
 * - Display reset information
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * Display: 480x800 MIPI-DSI with ST7701 controller
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "nvs_flash.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char *TAG = "reset_device";

// UI elements
static lv_obj_t *reason_label = NULL;
static lv_obj_t *info_label = NULL;
static lv_obj_t *countdown_label = NULL;
static lv_obj_t *reset_btn = NULL;
static lv_obj_t *delayed_btn = NULL;

// Countdown state
static bool countdown_active = false;
static int countdown_value = 5;
static lv_timer_t *countdown_timer = NULL;

/**
 * @brief Get reset reason as string
 */
static const char* get_reset_reason_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:    return "Unknown";
        case ESP_RST_POWERON:    return "Power-on";
        case ESP_RST_EXT:        return "External pin";
        case ESP_RST_SW:         return "Software reset (esp_restart)";
        case ESP_RST_PANIC:      return "Exception/panic";
        case ESP_RST_INT_WDT:    return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:   return "Task watchdog";
        case ESP_RST_WDT:        return "Other watchdog";
        case ESP_RST_DEEPSLEEP:  return "Deep sleep wakeup";
        case ESP_RST_BROWNOUT:   return "Brownout";
        case ESP_RST_SDIO:       return "SDIO";
        default:                 return "Unknown";
    }
}

/**
 * @brief Countdown timer callback
 */
static void countdown_timer_cb(lv_timer_t *timer) {
    if (!countdown_active) {
        lv_timer_del(timer);
        countdown_timer = NULL;
        return;
    }

    countdown_value--;

    bsp_display_lock(0);
    if (countdown_label) {
        if (countdown_value > 0) {
            lv_label_set_text_fmt(countdown_label, "Resetting in %d...", countdown_value);
        } else {
            lv_label_set_text(countdown_label, "Resetting NOW!");
        }
    }
    bsp_display_unlock();

    if (countdown_value <= 0) {
        ESP_LOGI(TAG, "Countdown complete - resetting!");
        countdown_active = false;
        lv_timer_del(timer);
        countdown_timer = NULL;

        // Give time for display to update
        vTaskDelay(pdMS_TO_TICKS(200));

        // Perform software reset
        esp_restart();
    }
}

/**
 * @brief Immediate reset button callback
 */
static void reset_btn_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Immediate reset requested");

    bsp_display_lock(0);
    lv_label_set_text(countdown_label, "Resetting NOW!");
    bsp_display_unlock();

    // Small delay to show message
    vTaskDelay(pdMS_TO_TICKS(200));

    // Perform software reset
    esp_restart();
}

/**
 * @brief Delayed reset button callback
 */
static void delayed_reset_btn_cb(lv_event_t *e) {
    if (countdown_active) {
        // Cancel countdown
        ESP_LOGI(TAG, "Countdown cancelled");
        countdown_active = false;
        countdown_value = 5;

        bsp_display_lock(0);
        if (countdown_label) {
            lv_label_set_text(countdown_label, "Countdown cancelled");
        }
        lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        if (label) {
            lv_label_set_text(label, "Reset in 5s");
        }
        bsp_display_unlock();

    } else {
        // Start countdown
        ESP_LOGI(TAG, "Starting 5 second countdown");
        countdown_active = true;
        countdown_value = 5;

        bsp_display_lock(0);
        if (countdown_label) {
            lv_label_set_text_fmt(countdown_label, "Resetting in %d...", countdown_value);
        }
        lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        if (label) {
            lv_label_set_text(label, "Cancel");
        }

        // Create timer for countdown
        if (countdown_timer == NULL) {
            countdown_timer = lv_timer_create(countdown_timer_cb, 1000, NULL);
        }
        bsp_display_unlock();
    }
}

/**
 * @brief Create the UI
 */
static void create_ui(void) {
    lv_obj_t *scr = lv_scr_act();

    // Set dark background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), LV_PART_MAIN);

    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Reset Device Demo");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Reset reason section
    lv_obj_t *reason_title = lv_label_create(scr);
    lv_label_set_text(reason_title, "Last Reset Reason:");
    lv_obj_set_style_text_color(reason_title, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(reason_title, LV_ALIGN_TOP_LEFT, 20, 70);

    reason_label = lv_label_create(scr);
    esp_reset_reason_t reason = esp_reset_reason();
    lv_label_set_text(reason_label, get_reset_reason_str(reason));
    lv_obj_set_style_text_color(reason_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(reason_label, &lv_font_montserrat_16, 0);
    lv_obj_align(reason_label, LV_ALIGN_TOP_LEFT, 20, 95);

    // Chip info section
    lv_obj_t *chip_title = lv_label_create(scr);
    lv_label_set_text(chip_title, "Chip Information:");
    lv_obj_set_style_text_color(chip_title, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(chip_title, LV_ALIGN_TOP_LEFT, 20, 140);

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    info_label = lv_label_create(scr);
    lv_label_set_text_fmt(info_label,
        "Model: ESP32-P4\n"
        "Cores: %d\n"
        "Revision: %d\n"
        "Free Heap: %lu KB",
        chip_info.cores,
        chip_info.revision,
        (unsigned long)(esp_get_free_heap_size() / 1024));
    lv_obj_set_style_text_color(info_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
    lv_obj_align(info_label, LV_ALIGN_TOP_LEFT, 20, 165);

    // Countdown label
    countdown_label = lv_label_create(scr);
    lv_label_set_text(countdown_label, "Press a button to reset");
    lv_obj_set_style_text_color(countdown_label, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(countdown_label, &lv_font_montserrat_18, 0);
    lv_obj_align(countdown_label, LV_ALIGN_CENTER, 0, 50);

    // Button container
    lv_obj_t *btn_container = lv_obj_create(scr);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, 400, 70);
    lv_obj_align(btn_container, LV_ALIGN_CENTER, 0, 130);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Immediate reset button (red)
    reset_btn = lv_btn_create(btn_container);
    lv_obj_set_size(reset_btn, 160, 60);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0xCC3333), 0);
    lv_obj_add_event_cb(reset_btn, reset_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset Now");
    lv_obj_center(reset_label);

    // Delayed reset button (orange)
    delayed_btn = lv_btn_create(btn_container);
    lv_obj_set_size(delayed_btn, 160, 60);
    lv_obj_set_style_bg_color(delayed_btn, lv_color_hex(0xCC8833), 0);
    lv_obj_add_event_cb(delayed_btn, delayed_reset_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *delayed_label = lv_label_create(delayed_btn);
    lv_label_set_text(delayed_label, "Reset in 5s");
    lv_obj_center(delayed_label);

    // Instructions
    lv_obj_t *instructions = lv_label_create(scr);
    lv_label_set_text(instructions,
        "Reset Now: Immediate software reset\n"
        "Reset in 5s: Countdown with cancel option");
    lv_obj_set_style_text_color(instructions, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(instructions, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(instructions, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instructions, LV_ALIGN_BOTTOM_MID, 0, -50);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C Reset Device Example");
    ESP_LOGI(TAG, "  ESP32-P4 + LVGL 9");
    ESP_LOGI(TAG, "========================================");

    // Log reset reason
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %s", get_reset_reason_str(reason));

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize display using BSP
    ESP_LOGI(TAG, "Initializing display...");

    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 50,
        .double_buffer = false,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,
        }
    };

    lv_display_t *disp = bsp_display_start_with_config(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display!");
        return;
    }
    ESP_LOGI(TAG, "Display initialized");

    // Turn on backlight
    bsp_display_backlight_on();
    bsp_display_brightness_set(100);

    // Create UI
    bsp_display_lock(0);
    create_ui();
    bsp_display_unlock();
    ESP_LOGI(TAG, "UI created");

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Reset Device demo ready!");
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
