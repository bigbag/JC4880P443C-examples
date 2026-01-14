/**
 * @file main.cpp
 * @brief Example 04: WiFi Scan for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - WiFi scanning via ESP-HOSTED (C6 co-processor)
 * - Displaying scanned networks on the LCD
 * - Periodic network scan refresh
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * WiFi: Via ESP32-C6 co-processor using ESP-HOSTED
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

// ESP-HOSTED for WiFi via C6 co-processor
#include "esp_hosted.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char *TAG = "wifi_scan";

// Event group for WiFi events
static EventGroupHandle_t wifi_event_group;
#define WIFI_SCAN_DONE_BIT BIT0

// LVGL UI elements
static lv_obj_t *status_label = NULL;
static lv_obj_t *network_list = NULL;
static lv_obj_t *scan_btn = NULL;

// Scan results storage
#define MAX_SCAN_RESULTS 20
static wifi_ap_record_t ap_records[MAX_SCAN_RESULTS];
static uint16_t ap_count = 0;

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "WiFi scan completed");
                xEventGroupSetBits(wifi_event_group, WIFI_SCAN_DONE_BIT);
                break;
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi station started");
                break;
            default:
                break;
        }
    }
}

/**
 * @brief Initialize WiFi in station mode
 */
static esp_err_t wifi_init_sta(void) {
    ESP_LOGI(TAG, "Initializing WiFi...");

    wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default WiFi station
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    // Set WiFi mode to station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialized in station mode");
    return ESP_OK;
}

/**
 * @brief Perform WiFi scan
 */
static esp_err_t wifi_scan(void) {
    ESP_LOGI(TAG, "Starting WiFi scan...");

    // Update UI status
    if (status_label) {
        bsp_display_lock(0);
        lv_label_set_text(status_label, "Scanning...");
        bsp_display_unlock();
    }

    // Configure scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300,
            },
        },
    };

    // Clear previous results
    ap_count = 0;
    memset(ap_records, 0, sizeof(ap_records));

    // Start BLOCKING scan (required for ESP-HOSTED)
    // The blocking parameter must be true for ESP-HOSTED WiFi Remote
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Get number of APs found (must call this first with ESP-HOSTED)
    uint16_t num_aps = 0;
    ret = esp_wifi_scan_get_ap_num(&num_aps);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Scan found %d APs", num_aps);

    // Limit to our buffer size
    ap_count = (num_aps > MAX_SCAN_RESULTS) ? MAX_SCAN_RESULTS : num_aps;

    // Get scan records
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
        return ret;
    }

    // Debug: print first few SSIDs
    for (int i = 0; i < ap_count && i < 5; i++) {
        ESP_LOGI(TAG, "  [%d] SSID: %s, RSSI: %d, CH: %d",
                 i, (char*)ap_records[i].ssid, ap_records[i].rssi, ap_records[i].primary);
    }

    ESP_LOGI(TAG, "Found %d networks", ap_count);
    return ESP_OK;
}

/**
 * @brief Update the network list UI
 */
static void update_network_list(void) {
    if (network_list == NULL) return;

    bsp_display_lock(0);

    // Clear existing items
    lv_obj_clean(network_list);

    if (ap_count == 0) {
        lv_obj_t *label = lv_label_create(network_list);
        lv_label_set_text(label, "No networks found");
        lv_obj_set_style_text_color(label, lv_color_hex(0x888888), 0);
    } else {
        // Add each network to the list
        for (int i = 0; i < ap_count; i++) {
            lv_obj_t *item = lv_obj_create(network_list);
            lv_obj_set_size(item, LV_PCT(95), 50);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x1a1a2e), 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_pad_all(item, 5, 0);

            // SSID label
            lv_obj_t *ssid_label = lv_label_create(item);
            lv_label_set_text_fmt(ssid_label, "%s", (char*)ap_records[i].ssid);
            lv_obj_set_style_text_color(ssid_label, lv_color_white(), 0);
            lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 5, 2);

            // RSSI and security info
            const char *security = "";
            switch (ap_records[i].authmode) {
                case WIFI_AUTH_OPEN:         security = "Open"; break;
                case WIFI_AUTH_WEP:          security = "WEP"; break;
                case WIFI_AUTH_WPA_PSK:      security = "WPA"; break;
                case WIFI_AUTH_WPA2_PSK:     security = "WPA2"; break;
                case WIFI_AUTH_WPA_WPA2_PSK: security = "WPA/WPA2"; break;
                case WIFI_AUTH_WPA3_PSK:     security = "WPA3"; break;
                default:                     security = "?"; break;
            }

            lv_obj_t *info_label = lv_label_create(item);
            lv_label_set_text_fmt(info_label, "RSSI: %d dBm | %s | CH %d",
                                  ap_records[i].rssi,
                                  security,
                                  ap_records[i].primary);
            lv_obj_set_style_text_color(info_label, lv_color_hex(0x88CCFF), 0);
            lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
            lv_obj_align(info_label, LV_ALIGN_BOTTOM_LEFT, 5, -2);
        }

        // Update status
        lv_label_set_text_fmt(status_label, "Found %d networks", ap_count);
    }

    bsp_display_unlock();
}

/**
 * @brief Scan button click callback
 */
static void scan_btn_click_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Scan button clicked");

    // Disable button during scan
    lv_obj_add_state(scan_btn, LV_STATE_DISABLED);

    // Perform scan
    if (wifi_scan() == ESP_OK) {
        update_network_list();
    } else {
        bsp_display_lock(0);
        lv_label_set_text(status_label, "Scan failed!");
        bsp_display_unlock();
    }

    // Re-enable button
    lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
}

/**
 * @brief Create the UI
 */
static void create_ui(void) {
    lv_obj_t *scr = lv_scr_act();

    // Set background color
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), LV_PART_MAIN);

    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "WiFi Network Scanner");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Status label
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Press Scan to find networks");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 50);

    // Scan button
    scan_btn = lv_btn_create(scr);
    lv_obj_set_size(scan_btn, 150, 50);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_add_event_cb(scan_btn, scan_btn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(scan_btn);
    lv_label_set_text(btn_label, "Scan");
    lv_obj_center(btn_label);

    // Network list container
    network_list = lv_obj_create(scr);
    lv_obj_set_size(network_list, LV_PCT(95), 550);
    lv_obj_align(network_list, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(network_list, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(network_list, 0, 0);
    lv_obj_set_flex_flow(network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(network_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(network_list, 5, 0);

    // Initial message
    lv_obj_t *hint = lv_label_create(network_list);
    lv_label_set_text(hint, "Networks will appear here");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C WiFi Scanner Example");
    ESP_LOGI(TAG, "  ESP32-P4 + ESP-HOSTED + LVGL 9");
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
    // (must be called before any WiFi APIs)
    ESP_LOGI(TAG, "Initializing ESP-HOSTED...");
    ret = esp_hosted_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-HOSTED init failed: %s", esp_err_to_name(ret));
        bsp_display_lock(0);
        lv_label_set_text(status_label, "ESP-HOSTED init failed!");
        bsp_display_unlock();
        return;
    }
    ESP_LOGI(TAG, "ESP-HOSTED initialized");

    // Wait for transport to become active
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize WiFi
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed!");
        bsp_display_lock(0);
        lv_label_set_text(status_label, "WiFi init failed!");
        bsp_display_unlock();
    } else {
        ESP_LOGI(TAG, "WiFi ready");
        bsp_display_lock(0);
        lv_label_set_text(status_label, "WiFi ready - Press Scan");
        bsp_display_unlock();

        // Do initial scan
        vTaskDelay(pdMS_TO_TICKS(1000));  // Give WiFi time to stabilize
        if (wifi_scan() == ESP_OK) {
            update_network_list();
        }
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  WiFi Scanner ready!");
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
