/**
 * @file main.cpp
 * @brief Example 05: WiFi HTTP Client for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - WiFi connection via ESP-HOSTED (C6 co-processor)
 * - HTTP GET request to a public API
 * - Displaying response on the LCD
 * - Connection status and response time
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * WiFi: Via ESP32-C6 co-processor using ESP-HOSTED
 *
 * NOTE: Configure WIFI_SSID and WIFI_PASSWORD below!
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
#include "esp_http_client.h"
#include "esp_timer.h"

// ESP-HOSTED for WiFi via C6 co-processor
#include "esp_hosted.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char *TAG = "wifi_http";

// ============================================================================
// WiFi Configuration - CHANGE THESE!
// ============================================================================
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// API URL for testing (returns JSON with IP info)
#define HTTP_URL       "http://httpbin.org/ip"

// ============================================================================

// Event group for WiFi events
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// LVGL UI elements
static lv_obj_t *status_label = NULL;
static lv_obj_t *ip_label = NULL;
static lv_obj_t *response_label = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *fetch_btn = NULL;

// HTTP response buffer
#define HTTP_BUFFER_SIZE 2048
static char http_buffer[HTTP_BUFFER_SIZE];
static int http_buffer_len = 0;

// WiFi retry counter
static int wifi_retry_count = 0;
#define WIFI_MAX_RETRY 5

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started, connecting...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                if (wifi_retry_count < WIFI_MAX_RETRY) {
                    ESP_LOGI(TAG, "WiFi disconnected, retrying (%d/%d)...",
                             wifi_retry_count + 1, WIFI_MAX_RETRY);
                    esp_wifi_connect();
                    wifi_retry_count++;
                } else {
                    ESP_LOGE(TAG, "WiFi connection failed after %d retries", WIFI_MAX_RETRY);
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                }
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

        // Update IP label
        if (ip_label) {
            bsp_display_lock(0);
            lv_label_set_text_fmt(ip_label, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
            bsp_display_unlock();
        }
    }
}

/**
 * @brief Initialize WiFi in station mode and connect
 */
static esp_err_t wifi_init_and_connect(void) {
    ESP_LOGI(TAG, "Initializing WiFi...");

    wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default WiFi station
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Configure WiFi
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init complete, waiting for connection...");

    // Wait for connection or failure
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID: %s", WIFI_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", WIFI_SSID);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

/**
 * @brief HTTP event handler
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Append data to buffer
                int copy_len = evt->data_len;
                if (http_buffer_len + copy_len >= HTTP_BUFFER_SIZE) {
                    copy_len = HTTP_BUFFER_SIZE - http_buffer_len - 1;
                }
                if (copy_len > 0) {
                    memcpy(http_buffer + http_buffer_len, evt->data, copy_len);
                    http_buffer_len += copy_len;
                    http_buffer[http_buffer_len] = '\0';
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP request finished");
            break;

        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP request error");
            break;

        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Perform HTTP GET request
 */
static esp_err_t http_fetch(void) {
    ESP_LOGI(TAG, "Fetching: %s", HTTP_URL);

    // Clear buffer
    http_buffer_len = 0;
    memset(http_buffer, 0, sizeof(http_buffer));

    // Record start time
    int64_t start_time = esp_timer_get_time();

    // Configure HTTP client
    esp_http_client_config_t config = {};
    config.url = HTTP_URL;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    // Perform request
    esp_err_t err = esp_http_client_perform(client);

    // Calculate elapsed time
    int64_t elapsed_ms = (esp_timer_get_time() - start_time) / 1000;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int content_len = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP Status: %d, Content-Length: %d", status, content_len);
        ESP_LOGI(TAG, "Response: %s", http_buffer);

        // Update UI
        bsp_display_lock(0);
        if (status_label) {
            lv_label_set_text_fmt(status_label, "Status: %d OK", status);
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
        }
        if (time_label) {
            lv_label_set_text_fmt(time_label, "Time: %lld ms", (long long)elapsed_ms);
        }
        if (response_label) {
            // Truncate if too long for display
            if (strlen(http_buffer) > 500) {
                http_buffer[500] = '\0';
                strcat(http_buffer, "...");
            }
            lv_label_set_text(response_label, http_buffer);
        }
        bsp_display_unlock();

    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));

        bsp_display_lock(0);
        if (status_label) {
            lv_label_set_text(status_label, "Status: ERROR");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        }
        if (response_label) {
            lv_label_set_text_fmt(response_label, "Error: %s", esp_err_to_name(err));
        }
        bsp_display_unlock();
    }

    esp_http_client_cleanup(client);
    return err;
}

/**
 * @brief Fetch button click callback
 */
static void fetch_btn_click_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Fetch button clicked");

    // Disable button during fetch
    lv_obj_add_state(fetch_btn, LV_STATE_DISABLED);

    // Update status
    bsp_display_lock(0);
    lv_label_set_text(status_label, "Status: Fetching...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
    bsp_display_unlock();

    // Perform HTTP request
    http_fetch();

    // Re-enable button
    lv_obj_clear_state(fetch_btn, LV_STATE_DISABLED);
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
    lv_label_set_text(title, "HTTP Client Demo");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // SSID label
    lv_obj_t *ssid_label = lv_label_create(scr);
    lv_label_set_text_fmt(ssid_label, "SSID: %s", WIFI_SSID);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 10, 50);

    // IP label
    ip_label = lv_label_create(scr);
    lv_label_set_text(ip_label, "IP: Connecting...");
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(ip_label, LV_ALIGN_TOP_LEFT, 10, 75);

    // Status label
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Status: Ready");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 10, 100);

    // Time label
    time_label = lv_label_create(scr);
    lv_label_set_text(time_label, "Time: --- ms");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x888888), 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_RIGHT, -10, 100);

    // URL label
    lv_obj_t *url_label = lv_label_create(scr);
    lv_label_set_text_fmt(url_label, "URL: %s", HTTP_URL);
    lv_obj_set_style_text_color(url_label, lv_color_hex(0xFFCC88), 0);
    lv_obj_set_style_text_font(url_label, &lv_font_montserrat_14, 0);
    lv_obj_align(url_label, LV_ALIGN_TOP_LEFT, 10, 130);

    // Fetch button
    fetch_btn = lv_btn_create(scr);
    lv_obj_set_size(fetch_btn, 150, 50);
    lv_obj_align(fetch_btn, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_add_event_cb(fetch_btn, fetch_btn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(fetch_btn);
    lv_label_set_text(btn_label, "Fetch");
    lv_obj_center(btn_label);

    // Response container
    lv_obj_t *response_container = lv_obj_create(scr);
    lv_obj_set_size(response_container, LV_PCT(95), 520);
    lv_obj_align(response_container, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(response_container, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(response_container, 0, 0);
    lv_obj_set_style_pad_all(response_container, 10, 0);

    // Response title
    lv_obj_t *resp_title = lv_label_create(response_container);
    lv_label_set_text(resp_title, "Response:");
    lv_obj_set_style_text_color(resp_title, lv_color_white(), 0);
    lv_obj_align(resp_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Response label
    response_label = lv_label_create(response_container);
    lv_label_set_text(response_label, "Press Fetch to make HTTP request");
    lv_obj_set_style_text_color(response_label, lv_color_hex(0x88FF88), 0);
    lv_obj_set_style_text_font(response_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(response_label, LV_PCT(95));
    lv_label_set_long_mode(response_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(response_label, LV_ALIGN_TOP_LEFT, 0, 25);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C HTTP Client Example");
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

    // Initialize WiFi and connect
    ret = wifi_init_and_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed!");
        bsp_display_lock(0);
        lv_label_set_text(ip_label, "IP: Connection failed");
        lv_label_set_text(status_label, "WiFi connection failed!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        bsp_display_unlock();
    } else {
        ESP_LOGI(TAG, "WiFi connected successfully");
        bsp_display_lock(0);
        lv_label_set_text(status_label, "Status: Connected");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
        bsp_display_unlock();

        // Do initial fetch
        vTaskDelay(pdMS_TO_TICKS(1000));
        http_fetch();
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  HTTP Client demo ready!");
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
