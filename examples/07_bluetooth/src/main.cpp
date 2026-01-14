/**
 * @file main.cpp
 * @brief Example 07: Bluetooth BLE Scanner for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - BLE initialization via ESP-HOSTED (C6 co-processor)
 * - BLE device scanning
 * - Displaying discovered devices on the LCD
 * - Periodic scan refresh
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * Bluetooth: Via ESP32-C6 co-processor using ESP-HOSTED
 *
 * NOTE: BLE functionality depends on ESP-HOSTED BLE support.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

// ESP-HOSTED for BLE via C6 co-processor
#include "esp_hosted.h"
#include "esp_hosted_bluedroid.h"

// Bluetooth includes (via ESP-HOSTED remote)
// Note: esp_bt.h is NOT included - controller is on C6 chip
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_device.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char *TAG = "bluetooth";

// Maximum discovered devices to track
#define MAX_DEVICES 20

// Device info structure
typedef struct {
    uint8_t bda[6];           // Bluetooth device address
    char name[32];            // Device name (if available)
    int8_t rssi;              // Signal strength
    bool has_name;            // Whether name was discovered
} ble_device_t;

// Discovered devices list
static ble_device_t devices[MAX_DEVICES];
static int device_count = 0;
static SemaphoreHandle_t device_mutex = NULL;

// LVGL UI elements
static lv_obj_t *status_label = NULL;
static lv_obj_t *count_label = NULL;
static lv_obj_t *device_list = NULL;
static lv_obj_t *scan_btn = NULL;

// Scan state
static bool is_scanning = false;

/**
 * @brief Convert BDA to string
 */
static void bda_to_str(const uint8_t *bda, char *str, size_t str_len) {
    snprintf(str, str_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

/**
 * @brief Check if device is already in list
 */
static int find_device(const uint8_t *bda) {
    for (int i = 0; i < device_count; i++) {
        if (memcmp(devices[i].bda, bda, 6) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Add or update device in list
 */
static void add_or_update_device(const uint8_t *bda, const char *name, int8_t rssi) {
    if (device_mutex == NULL) return;

    xSemaphoreTake(device_mutex, portMAX_DELAY);

    int idx = find_device(bda);

    if (idx >= 0) {
        // Update existing device
        devices[idx].rssi = rssi;
        if (name && strlen(name) > 0 && !devices[idx].has_name) {
            strncpy(devices[idx].name, name, sizeof(devices[idx].name) - 1);
            devices[idx].has_name = true;
        }
    } else if (device_count < MAX_DEVICES) {
        // Add new device
        memcpy(devices[device_count].bda, bda, 6);
        devices[device_count].rssi = rssi;
        if (name && strlen(name) > 0) {
            strncpy(devices[device_count].name, name, sizeof(devices[device_count].name) - 1);
            devices[device_count].has_name = true;
        } else {
            devices[device_count].name[0] = '\0';
            devices[device_count].has_name = false;
        }
        device_count++;
    }

    xSemaphoreGive(device_mutex);
}

/**
 * @brief GAP callback for BLE events
 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Scan parameters set, starting scan...");
                esp_ble_gap_start_scanning(10);  // Scan for 10 seconds
            } else {
                ESP_LOGE(TAG, "Failed to set scan parameters: %d", param->scan_param_cmpl.status);
            }
            break;

        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "BLE scan started");
                is_scanning = true;

                bsp_display_lock(0);
                if (status_label) {
                    lv_label_set_text(status_label, "Status: Scanning...");
                    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
                }
                bsp_display_unlock();
            } else {
                ESP_LOGE(TAG, "Scan start failed: %d", param->scan_start_cmpl.status);
            }
            break;

        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            esp_ble_gap_cb_param_t *scan_result = param;

            switch (scan_result->scan_rst.search_evt) {
                case ESP_GAP_SEARCH_INQ_RES_EVT: {
                    // Found a device
                    char bda_str[18];
                    bda_to_str(scan_result->scan_rst.bda, bda_str, sizeof(bda_str));

                    // Try to get device name from advertising data
                    char name[32] = {0};
                    uint8_t *adv_name = NULL;
                    uint8_t adv_name_len = 0;
                    adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                        ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
                    if (adv_name == NULL) {
                        adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                            ESP_BLE_AD_TYPE_NAME_SHORT, &adv_name_len);
                    }
                    if (adv_name && adv_name_len > 0) {
                        int copy_len = (adv_name_len < sizeof(name) - 1) ? adv_name_len : sizeof(name) - 1;
                        memcpy(name, adv_name, copy_len);
                        name[copy_len] = '\0';
                    }

                    add_or_update_device(scan_result->scan_rst.bda, name, scan_result->scan_rst.rssi);

                    if (name[0]) {
                        ESP_LOGI(TAG, "Device: %s [%s] RSSI: %d", name, bda_str, scan_result->scan_rst.rssi);
                    } else {
                        ESP_LOGD(TAG, "Device: %s RSSI: %d", bda_str, scan_result->scan_rst.rssi);
                    }
                    break;
                }

                case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                    ESP_LOGI(TAG, "Scan complete, found %d devices", device_count);
                    is_scanning = false;

                    bsp_display_lock(0);
                    if (status_label) {
                        lv_label_set_text(status_label, "Status: Scan complete");
                        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
                    }
                    if (count_label) {
                        lv_label_set_text_fmt(count_label, "Found: %d devices", device_count);
                    }
                    bsp_display_unlock();
                    break;

                default:
                    break;
            }
            break;
        }

        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            if (param->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Scan stopped");
                is_scanning = false;
            }
            break;

        default:
            break;
    }
}

/**
 * @brief Initialize BLE via ESP-HOSTED
 * Note: BT controller is on the C6 co-processor, managed by ESP-HOSTED
 */
static esp_err_t ble_init(void) {
    ESP_LOGI(TAG, "Initializing BLE via ESP-HOSTED...");

    // Initialize Bluedroid (controller is on C6, handled by ESP-HOSTED)
    esp_err_t ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register GAP callback
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "BLE initialized successfully");
    return ESP_OK;
}

/**
 * @brief Start BLE scan
 */
static void start_ble_scan(void) {
    ESP_LOGI(TAG, "Starting BLE scan...");

    // Clear previous results
    xSemaphoreTake(device_mutex, portMAX_DELAY);
    device_count = 0;
    memset(devices, 0, sizeof(devices));
    xSemaphoreGive(device_mutex);

    // Set scan parameters
    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,  // 50ms
        .scan_window = 0x30,    // 30ms
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    };

    esp_ble_gap_set_scan_params(&scan_params);
}

/**
 * @brief Update the device list UI
 */
static void update_device_list(void) {
    if (device_list == NULL || device_mutex == NULL) return;

    bsp_display_lock(0);

    // Clear existing items
    lv_obj_clean(device_list);

    xSemaphoreTake(device_mutex, portMAX_DELAY);

    if (device_count == 0) {
        lv_obj_t *label = lv_label_create(device_list);
        lv_label_set_text(label, "No devices found");
        lv_obj_set_style_text_color(label, lv_color_hex(0x888888), 0);
    } else {
        // Add each device to the list
        for (int i = 0; i < device_count; i++) {
            lv_obj_t *item = lv_obj_create(device_list);
            lv_obj_set_size(item, LV_PCT(95), 55);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x1a1a2e), 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_pad_all(item, 5, 0);

            // Device name or "Unknown"
            lv_obj_t *name_label = lv_label_create(item);
            if (devices[i].has_name && devices[i].name[0]) {
                lv_label_set_text(name_label, devices[i].name);
            } else {
                lv_label_set_text(name_label, "(Unknown Device)");
            }
            lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
            lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 5, 2);

            // MAC address and RSSI
            char bda_str[18];
            bda_to_str(devices[i].bda, bda_str, sizeof(bda_str));

            lv_obj_t *info_label = lv_label_create(item);
            lv_label_set_text_fmt(info_label, "%s  |  RSSI: %d dBm", bda_str, devices[i].rssi);
            lv_obj_set_style_text_color(info_label, lv_color_hex(0x88CCFF), 0);
            lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
            lv_obj_align(info_label, LV_ALIGN_BOTTOM_LEFT, 5, -2);
        }

        // Update count
        if (count_label) {
            lv_label_set_text_fmt(count_label, "Found: %d devices", device_count);
        }
    }

    xSemaphoreGive(device_mutex);

    bsp_display_unlock();
}

/**
 * @brief Scan button click callback
 */
static void scan_btn_click_cb(lv_event_t *e) {
    if (is_scanning) {
        ESP_LOGI(TAG, "Scan already in progress");
        return;
    }

    ESP_LOGI(TAG, "Scan button clicked");

    // Disable button during scan
    lv_obj_add_state(scan_btn, LV_STATE_DISABLED);

    // Start scan
    start_ble_scan();

    // Re-enable button after delay (scan will complete)
    // Note: In production, we'd re-enable in the scan complete callback
    vTaskDelay(pdMS_TO_TICKS(100));
    lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
}

/**
 * @brief Refresh button click callback
 */
static void refresh_btn_click_cb(lv_event_t *e) {
    update_device_list();
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
    lv_label_set_text(title, "BLE Scanner");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Status label
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Status: Ready");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 10, 50);

    // Count label
    count_label = lv_label_create(scr);
    lv_label_set_text(count_label, "Found: 0 devices");
    lv_obj_set_style_text_color(count_label, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(count_label, LV_ALIGN_TOP_RIGHT, -10, 50);

    // Button container
    lv_obj_t *btn_container = lv_obj_create(scr);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, 350, 55);
    lv_obj_align(btn_container, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Scan button
    scan_btn = lv_btn_create(btn_container);
    lv_obj_set_size(scan_btn, 140, 50);
    lv_obj_add_event_cb(scan_btn, scan_btn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);

    // Refresh button
    lv_obj_t *refresh_btn = lv_btn_create(btn_container);
    lv_obj_set_size(refresh_btn, 140, 50);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    // Device list container
    device_list = lv_obj_create(scr);
    lv_obj_set_size(device_list, LV_PCT(95), 550);
    lv_obj_align(device_list, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(device_list, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(device_list, 0, 0);
    lv_obj_set_flex_flow(device_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(device_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(device_list, 5, 0);

    // Initial message
    lv_obj_t *hint = lv_label_create(device_list);
    lv_label_set_text(hint, "Press Scan to find BLE devices");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C BLE Scanner Example");
    ESP_LOGI(TAG, "  ESP32-P4 + ESP-HOSTED + LVGL 9");
    ESP_LOGI(TAG, "========================================");

    // Create mutex for device list
    device_mutex = xSemaphoreCreateMutex();
    if (device_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");
        return;
    }

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

    // Initialize ESP-HOSTED transport to C6 co-processor
    ESP_LOGI(TAG, "Initializing ESP-HOSTED...");
    ret = esp_hosted_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-HOSTED init failed: %s", esp_err_to_name(ret));
        bsp_display_lock(0);
        lv_label_set_text(status_label, "ESP-HOSTED init failed!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        bsp_display_unlock();
        return;
    }
    ESP_LOGI(TAG, "ESP-HOSTED initialized");

    // Wait for transport to stabilize
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize BLE
    ret = ble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE initialization failed!");
        bsp_display_lock(0);
        lv_label_set_text(status_label, "BLE init failed!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        bsp_display_unlock();
    } else {
        ESP_LOGI(TAG, "BLE ready");
        bsp_display_lock(0);
        lv_label_set_text(status_label, "Status: Ready to scan");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
        bsp_display_unlock();

        // Optionally start initial scan
        vTaskDelay(pdMS_TO_TICKS(1000));
        start_ble_scan();
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  BLE Scanner ready!");
    ESP_LOGI(TAG, "========================================");

    // Main loop - periodically update display
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Update the device list periodically
        if (!is_scanning) {
            update_device_list();
        }

        ESP_LOGI(TAG, "Free heap: %lu bytes, Devices: %d",
                 (unsigned long)esp_get_free_heap_size(), device_count);
    }
}
