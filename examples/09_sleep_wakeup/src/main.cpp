/**
 * @file main.cpp
 * @brief Example 09: Sleep & Wakeup for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - Light sleep mode with timer wakeup
 * - Deep sleep mode with timer wakeup
 * - GPIO wakeup (touch interrupt pin)
 * - Displaying wakeup cause
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * Display: 480x800 MIPI-DSI with ST7701 controller
 *
 * Note: Touch wakeup uses the GT911 interrupt GPIO
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char *TAG = "sleep_wakeup";

// Touch interrupt GPIO (GT911 on JC4880P443C)
// Check your board schematic - commonly GPIO4 or similar
#define TOUCH_INT_GPIO  GPIO_NUM_4

// Sleep duration for timer wakeup (in seconds)
#define LIGHT_SLEEP_DURATION_SEC    5
#define DEEP_SLEEP_DURATION_SEC     10

// UI elements
static lv_obj_t *wakeup_label = NULL;
static lv_obj_t *info_label = NULL;
static lv_obj_t *status_label = NULL;

/**
 * @brief Get wakeup cause as string
 */
static const char* get_wakeup_cause_str(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_UNDEFINED:    return "Undefined (power on)";
        case ESP_SLEEP_WAKEUP_ALL:          return "All wakeup sources";
        case ESP_SLEEP_WAKEUP_EXT0:         return "External signal (RTC_IO)";
        case ESP_SLEEP_WAKEUP_EXT1:         return "External signal (RTC_CNTL)";
        case ESP_SLEEP_WAKEUP_TIMER:        return "Timer";
        case ESP_SLEEP_WAKEUP_TOUCHPAD:     return "Touchpad";
        case ESP_SLEEP_WAKEUP_ULP:          return "ULP program";
        case ESP_SLEEP_WAKEUP_GPIO:         return "GPIO";
        case ESP_SLEEP_WAKEUP_UART:         return "UART";
        case ESP_SLEEP_WAKEUP_WIFI:         return "WiFi";
        case ESP_SLEEP_WAKEUP_COCPU:        return "Co-CPU";
        case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG: return "Co-CPU trap trigger";
        case ESP_SLEEP_WAKEUP_BT:           return "Bluetooth";
        default:                            return "Unknown";
    }
}

/**
 * @brief Enter light sleep with timer wakeup
 */
static void enter_light_sleep_timer(void) {
    ESP_LOGI(TAG, "Entering light sleep for %d seconds...", LIGHT_SLEEP_DURATION_SEC);

    bsp_display_lock(0);
    lv_label_set_text(status_label, "Entering light sleep...");
    bsp_display_unlock();

    // Turn off backlight before sleep
    bsp_display_brightness_set(0);

    vTaskDelay(pdMS_TO_TICKS(200));

    // Configure timer wakeup
    esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_DURATION_SEC * 1000000ULL);

    // Enter light sleep
    esp_err_t ret = esp_light_sleep_start();

    // Woken up - restore backlight
    bsp_display_brightness_set(100);

    if (ret == ESP_OK) {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        ESP_LOGI(TAG, "Woke up from light sleep, cause: %s", get_wakeup_cause_str(cause));

        bsp_display_lock(0);
        lv_label_set_text_fmt(status_label, "Woke up! Cause: %s", get_wakeup_cause_str(cause));
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "Light sleep failed: %s", esp_err_to_name(ret));
        bsp_display_lock(0);
        lv_label_set_text_fmt(status_label, "Sleep failed: %s", esp_err_to_name(ret));
        bsp_display_unlock();
    }
}

/**
 * @brief Enter light sleep with GPIO wakeup (touch)
 */
static void enter_light_sleep_gpio(void) {
    ESP_LOGI(TAG, "Entering light sleep, wake on touch...");

    bsp_display_lock(0);
    lv_label_set_text(status_label, "Sleeping... Touch to wake!");
    bsp_display_unlock();

    // Turn off backlight
    bsp_display_brightness_set(0);

    vTaskDelay(pdMS_TO_TICKS(200));

    // Configure GPIO wakeup on touch interrupt
    gpio_wakeup_enable(TOUCH_INT_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Also enable timer as fallback (30 seconds)
    esp_sleep_enable_timer_wakeup(30 * 1000000ULL);

    // Enter light sleep
    esp_err_t ret = esp_light_sleep_start();

    // Woken up
    bsp_display_brightness_set(100);
    gpio_wakeup_disable(TOUCH_INT_GPIO);

    if (ret == ESP_OK) {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        ESP_LOGI(TAG, "Woke up! Cause: %s", get_wakeup_cause_str(cause));

        bsp_display_lock(0);
        lv_label_set_text_fmt(status_label, "Woke up! Cause: %s", get_wakeup_cause_str(cause));
        bsp_display_unlock();
    }
}

/**
 * @brief Enter deep sleep with timer wakeup
 * Note: Deep sleep will reset the chip
 */
static void enter_deep_sleep_timer(void) {
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds...", DEEP_SLEEP_DURATION_SEC);
    ESP_LOGI(TAG, "The chip will reset after waking up!");

    bsp_display_lock(0);
    lv_label_set_text(status_label, "Entering deep sleep...\nChip will reset on wakeup!");
    bsp_display_unlock();

    // Turn off backlight
    bsp_display_brightness_set(0);

    vTaskDelay(pdMS_TO_TICKS(500));

    // Configure timer wakeup
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_SEC * 1000000ULL);

    // Enter deep sleep (no return - chip resets)
    ESP_LOGI(TAG, "Going to deep sleep now...");
    esp_deep_sleep_start();
}

/**
 * @brief Light sleep timer button callback
 */
static void light_sleep_timer_btn_cb(lv_event_t *e) {
    enter_light_sleep_timer();
}

/**
 * @brief Light sleep GPIO button callback
 */
static void light_sleep_gpio_btn_cb(lv_event_t *e) {
    enter_light_sleep_gpio();
}

/**
 * @brief Deep sleep button callback
 */
static void deep_sleep_btn_cb(lv_event_t *e) {
    enter_deep_sleep_timer();
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
    lv_label_set_text(title, "Sleep & Wakeup Demo");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Wakeup cause section
    lv_obj_t *wakeup_title = lv_label_create(scr);
    lv_label_set_text(wakeup_title, "Last Wakeup Cause:");
    lv_obj_set_style_text_color(wakeup_title, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(wakeup_title, LV_ALIGN_TOP_LEFT, 20, 60);

    wakeup_label = lv_label_create(scr);
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    lv_label_set_text(wakeup_label, get_wakeup_cause_str(cause));
    lv_obj_set_style_text_color(wakeup_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(wakeup_label, &lv_font_montserrat_16, 0);
    lv_obj_align(wakeup_label, LV_ALIGN_TOP_LEFT, 20, 85);

    // Info section
    info_label = lv_label_create(scr);
    lv_label_set_text_fmt(info_label,
        "Light Sleep: CPU pauses, RAM preserved\n"
        "Deep Sleep: Reset on wakeup, lowest power\n\n"
        "Touch INT GPIO: %d", (int)TOUCH_INT_GPIO);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
    lv_obj_align(info_label, LV_ALIGN_TOP_LEFT, 20, 130);

    // Status label
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Press a button to test sleep modes");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 30);

    // Light sleep timer button (green)
    lv_obj_t *light_timer_btn = lv_btn_create(scr);
    lv_obj_set_size(light_timer_btn, 200, 55);
    lv_obj_align(light_timer_btn, LV_ALIGN_CENTER, 0, 100);
    lv_obj_set_style_bg_color(light_timer_btn, lv_color_hex(0x336633), 0);
    lv_obj_add_event_cb(light_timer_btn, light_sleep_timer_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *light_timer_label = lv_label_create(light_timer_btn);
    lv_label_set_text_fmt(light_timer_label, "Light Sleep %ds", LIGHT_SLEEP_DURATION_SEC);
    lv_obj_center(light_timer_label);

    // Light sleep GPIO button (blue)
    lv_obj_t *light_gpio_btn = lv_btn_create(scr);
    lv_obj_set_size(light_gpio_btn, 200, 55);
    lv_obj_align(light_gpio_btn, LV_ALIGN_CENTER, 0, 165);
    lv_obj_set_style_bg_color(light_gpio_btn, lv_color_hex(0x333366), 0);
    lv_obj_add_event_cb(light_gpio_btn, light_sleep_gpio_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *light_gpio_label = lv_label_create(light_gpio_btn);
    lv_label_set_text(light_gpio_label, "Light Sleep (Touch)");
    lv_obj_center(light_gpio_label);

    // Deep sleep button (red/orange)
    lv_obj_t *deep_btn = lv_btn_create(scr);
    lv_obj_set_size(deep_btn, 200, 55);
    lv_obj_align(deep_btn, LV_ALIGN_CENTER, 0, 230);
    lv_obj_set_style_bg_color(deep_btn, lv_color_hex(0x993333), 0);
    lv_obj_add_event_cb(deep_btn, deep_sleep_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *deep_label = lv_label_create(deep_btn);
    lv_label_set_text_fmt(deep_label, "Deep Sleep %ds", DEEP_SLEEP_DURATION_SEC);
    lv_obj_center(deep_label);

    // Warning label for deep sleep
    lv_obj_t *warning = lv_label_create(scr);
    lv_label_set_text(warning, "Warning: Deep sleep causes chip reset!");
    lv_obj_set_style_text_color(warning, lv_color_hex(0xFF6666), 0);
    lv_obj_set_style_text_font(warning, &lv_font_montserrat_14, 0);
    lv_obj_align(warning, LV_ALIGN_BOTTOM_MID, 0, -40);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C Sleep & Wakeup Example");
    ESP_LOGI(TAG, "  ESP32-P4 + LVGL 9");
    ESP_LOGI(TAG, "========================================");

    // Log wakeup cause
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wakeup cause: %s", get_wakeup_cause_str(cause));

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Configure touch interrupt GPIO for wakeup
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << TOUCH_INT_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

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
    ESP_LOGI(TAG, "  Sleep & Wakeup demo ready!");
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
