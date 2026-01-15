/**
 * @file main.cpp
 * @brief Example 10: Battery ADC for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - Battery voltage monitoring via ADC2 Channel 4
 * - ADC calibration using curve fitting
 * - Battery percentage calculation (0-100%)
 * - LVGL UI with voltage display and progress bar
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * ADC: ADC2 Channel 4 with 12dB attenuation
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char* TAG = "battery_adc";

// ADC configuration
#define ADC_CHANNEL         ADC_CHANNEL_4
#define ADC_ATTEN           ADC_ATTEN_DB_12
#define ADC_SAMPLES         500

// Battery voltage thresholds (in mV)
// These values should be calibrated for your specific battery
#define BATTERY_V_MAX       2500  // Voltage at 100% charge
#define BATTERY_V_MIN       2250  // Voltage at 0% charge

// ADC handles
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool adc_calibrated = false;

// LVGL UI elements
static lv_obj_t* voltage_label = NULL;
static lv_obj_t* percent_label = NULL;
static lv_obj_t* bar = NULL;
static lv_obj_t* battery_icon = NULL;

// Battery state
static int current_voltage_mv = 0;
static int current_percent = 0;

/**
 * @brief Initialize ADC calibration
 */
static bool init_adc_calibration(void) {
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "Calibration scheme: Curve Fitting");
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_2,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "Calibration scheme: Line Fitting");
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_2,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle);
#endif

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration successful");
        return true;
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "ADC calibration failed: %s", esp_err_to_name(ret));
    }
    return false;
}

/**
 * @brief Initialize ADC
 */
static esp_err_t init_adc(void) {
    // ADC unit configuration
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_2,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

    // ADC channel configuration
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_config));

    // Initialize calibration
    adc_calibrated = init_adc_calibration();

    ESP_LOGI(TAG, "ADC initialized on Unit 2, Channel %d", ADC_CHANNEL);
    return ESP_OK;
}

/**
 * @brief Read battery voltage with averaging
 */
static int read_battery_voltage(void) {
    int raw_sum = 0;
    int raw_value = 0;

    // Read multiple samples and average
    for (int i = 0; i < ADC_SAMPLES; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw_value));
        raw_sum += raw_value;
    }
    int raw_avg = raw_sum / ADC_SAMPLES;

    // Convert to voltage using calibration
    int voltage_mv = 0;
    if (adc_calibrated) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, raw_avg, &voltage_mv));
    } else {
        // Fallback: rough conversion without calibration
        // For 12dB attenuation, max voltage is ~3.3V at 4095
        voltage_mv = (raw_avg * 3300) / 4095;
    }

    return voltage_mv;
}

/**
 * @brief Calculate battery percentage from voltage
 */
static int calculate_battery_percent(int voltage_mv) {
    if (voltage_mv >= BATTERY_V_MAX) {
        return 100;
    }
    if (voltage_mv <= BATTERY_V_MIN) {
        return 0;
    }

    int percent = (voltage_mv - BATTERY_V_MIN) * 100 / (BATTERY_V_MAX - BATTERY_V_MIN);
    return percent;
}

/**
 * @brief Get battery icon fill color based on percentage
 */
static lv_color_t get_battery_color(int percent) {
    if (percent <= 20) {
        return lv_color_hex(0xFF4444);  // Red for low battery
    } else if (percent <= 50) {
        return lv_color_hex(0xFFAA00);  // Orange for medium
    }
    return lv_color_hex(0x44FF44);  // Green for high
}

/**
 * @brief Update the UI with current battery values
 */
static void update_ui(void) {
    if (voltage_label == NULL) return;

    bsp_display_lock(0);

    // Update voltage label
    lv_label_set_text_fmt(voltage_label, "%d mV", current_voltage_mv);

    // Update percentage label
    lv_label_set_text_fmt(percent_label, "%d%%", current_percent);

    // Update progress bar
    lv_bar_set_value(bar, current_percent, LV_ANIM_ON);

    // Update bar color based on level
    lv_color_t bar_color = get_battery_color(current_percent);
    lv_obj_set_style_bg_color(bar, bar_color, LV_PART_INDICATOR);

    // Update battery icon fill
    lv_obj_set_style_bg_color(battery_icon, bar_color, LV_PART_MAIN);

    bsp_display_unlock();
}

/**
 * @brief Battery monitoring task
 */
static void battery_monitor_task(void* arg) {
    while (1) {
        // Read voltage
        current_voltage_mv = read_battery_voltage();

        // Calculate percentage
        current_percent = calculate_battery_percent(current_voltage_mv);

        // Log values
        ESP_LOGI(TAG, "Battery: %d mV, %d%%", current_voltage_mv, current_percent);

        // Update UI
        update_ui();

        // Delay 1 second between readings
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Create the battery monitoring UI
 */
static void create_ui(void) {
    lv_obj_t* scr = lv_scr_act();

    // Set background color
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Battery Monitor");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Subtitle
    lv_obj_t* subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "ADC2 Channel 4");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 60);

    // Create battery icon container
    lv_obj_t* battery_container = lv_obj_create(scr);
    lv_obj_set_size(battery_container, 120, 60);
    lv_obj_align(battery_container, LV_ALIGN_CENTER, 0, -80);
    lv_obj_set_style_bg_color(battery_container, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(battery_container, 3, 0);
    lv_obj_set_style_border_color(battery_container, lv_color_white(), 0);
    lv_obj_set_style_radius(battery_container, 8, 0);
    lv_obj_set_style_pad_all(battery_container, 5, 0);

    // Battery positive terminal
    lv_obj_t* battery_tip = lv_obj_create(scr);
    lv_obj_set_size(battery_tip, 8, 24);
    lv_obj_align_to(battery_tip, battery_container, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(battery_tip, lv_color_white(), 0);
    lv_obj_set_style_border_width(battery_tip, 0, 0);
    lv_obj_set_style_radius(battery_tip, 2, 0);

    // Battery fill (icon that changes size based on percentage)
    battery_icon = lv_obj_create(battery_container);
    lv_obj_set_size(battery_icon, 100, 40);
    lv_obj_align(battery_icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(battery_icon, lv_color_hex(0x44FF44), 0);
    lv_obj_set_style_border_width(battery_icon, 0, 0);
    lv_obj_set_style_radius(battery_icon, 4, 0);

    // Voltage label
    lv_obj_t* voltage_title = lv_label_create(scr);
    lv_label_set_text(voltage_title, "Voltage:");
    lv_obj_set_style_text_color(voltage_title, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(voltage_title, LV_ALIGN_CENTER, -60, 20);

    voltage_label = lv_label_create(scr);
    lv_label_set_text(voltage_label, "---- mV");
    lv_obj_set_style_text_color(voltage_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(voltage_label, &lv_font_montserrat_18, 0);
    lv_obj_align(voltage_label, LV_ALIGN_CENTER, 40, 20);

    // Percentage label
    lv_obj_t* percent_title = lv_label_create(scr);
    lv_label_set_text(percent_title, "Charge:");
    lv_obj_set_style_text_color(percent_title, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(percent_title, LV_ALIGN_CENTER, -60, 60);

    percent_label = lv_label_create(scr);
    lv_label_set_text(percent_label, "--%");
    lv_obj_set_style_text_color(percent_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(percent_label, &lv_font_montserrat_18, 0);
    lv_obj_align(percent_label, LV_ALIGN_CENTER, 40, 60);

    // Progress bar
    bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 300, 30);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 130);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x44FF44), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);

    // Info text
    lv_obj_t* info = lv_label_create(scr);
    lv_label_set_text(info, "Reading battery voltage every 1 second");
    lv_obj_set_style_text_color(info, lv_color_hex(0x666666), 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -80);

    // Threshold info
    lv_obj_t* threshold_info = lv_label_create(scr);
    lv_label_set_text_fmt(threshold_info, "Range: %d mV (0%%) - %d mV (100%%)",
                          BATTERY_V_MIN, BATTERY_V_MAX);
    lv_obj_set_style_text_color(threshold_info, lv_color_hex(0x555555), 0);
    lv_obj_align(threshold_info, LV_ALIGN_BOTTOM_MID, 0, -50);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C Battery ADC Example");
    ESP_LOGI(TAG, "  ESP32-P4 ADC2 + LVGL 9");
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize ADC
    ESP_LOGI(TAG, "Initializing ADC...");
    init_adc();

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

    lv_display_t* disp = bsp_display_start_with_config(&disp_cfg);
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

    // Start battery monitoring task
    xTaskCreate(battery_monitor_task, "battery_monitor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Battery monitoring started!");
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
