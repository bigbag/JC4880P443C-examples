/**
 * @file main.cpp
 * @brief Example 12: RS485 Serial for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - UART in RS485 half-duplex mode
 * - Echo mode (receive and echo back data)
 * - Send mode (transmit test messages)
 * - LVGL UI for data display and control
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 *
 * Hardware connections for RS485:
 * - TXD (GPIO9) -> DI (Driver Input) on MAX485
 * - RXD (GPIO8) -> RO (Receiver Output) on MAX485
 * - RTS (GPIO10) -> DE + RE (Driver Enable / Receiver Enable)
 * - VCC -> 3.3V
 * - GND -> GND
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char* TAG = "rs485_serial";

// RS485 UART configuration
#define RS485_UART_PORT     UART_NUM_1
#define RS485_TXD_PIN       GPIO_NUM_9
#define RS485_RXD_PIN       GPIO_NUM_8
#define RS485_RTS_PIN       GPIO_NUM_10
#define RS485_BAUD_RATE     115200
#define RS485_BUF_SIZE      256

// Read timeout in RTOS ticks
#define RS485_READ_TIMEOUT  (100 / portTICK_PERIOD_MS)

// Timeout for RS485 TOUT feature (3.5 character times)
#define RS485_RX_TOUT       3

// LVGL UI elements
static lv_obj_t* rx_textarea = NULL;
static lv_obj_t* tx_textarea = NULL;
static lv_obj_t* status_label = NULL;
static lv_obj_t* mode_btn = NULL;
static lv_obj_t* send_btn = NULL;
static lv_obj_t* clear_btn = NULL;

// Mode: true = Echo mode, false = Send mode
static bool echo_mode = true;

// Statistics
static int rx_count = 0;
static int tx_count = 0;

// Mutex for UI updates
static SemaphoreHandle_t ui_mutex = NULL;

// Task handle
static TaskHandle_t rs485_task_handle = NULL;

/**
 * @brief Initialize RS485 UART
 */
static esp_err_t init_rs485_uart(void) {
    uart_config_t uart_config = {
        .baud_rate = RS485_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install UART driver
    ESP_ERROR_CHECK(uart_driver_install(RS485_UART_PORT, RS485_BUF_SIZE * 2, 0, 0, NULL, 0));

    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(RS485_UART_PORT, &uart_config));

    // Set UART pins
    ESP_ERROR_CHECK(uart_set_pin(RS485_UART_PORT, RS485_TXD_PIN, RS485_RXD_PIN,
                                  RS485_RTS_PIN, UART_PIN_NO_CHANGE));

    // Set RS485 half-duplex mode
    ESP_ERROR_CHECK(uart_set_mode(RS485_UART_PORT, UART_MODE_RS485_HALF_DUPLEX));

    // Set read timeout
    ESP_ERROR_CHECK(uart_set_rx_timeout(RS485_UART_PORT, RS485_RX_TOUT));

    ESP_LOGI(TAG, "RS485 UART initialized: TXD=%d, RXD=%d, RTS=%d, Baud=%d",
             RS485_TXD_PIN, RS485_RXD_PIN, RS485_RTS_PIN, RS485_BAUD_RATE);

    return ESP_OK;
}

/**
 * @brief Send data over RS485
 */
static int rs485_send(const char* data, size_t len) {
    int sent = uart_write_bytes(RS485_UART_PORT, data, len);
    if (sent > 0) {
        tx_count += sent;
        ESP_LOGI(TAG, "TX: %d bytes", sent);
    }
    return sent;
}

/**
 * @brief Format data as hex string
 */
static void format_hex_string(const uint8_t* data, size_t len, char* out, size_t out_size) {
    size_t pos = 0;
    for (size_t i = 0; i < len && pos < out_size - 4; i++) {
        pos += snprintf(out + pos, out_size - pos, "%02X ", data[i]);
    }
    if (pos > 0) {
        out[pos - 1] = '\0';  // Remove trailing space
    } else {
        out[0] = '\0';
    }
}

/**
 * @brief Update UI with received/sent data
 */
static void update_ui_data(const char* rx_data, const char* tx_data) {
    if (xSemaphoreTake(ui_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    bsp_display_lock(0);

    if (rx_data != NULL && rx_textarea != NULL) {
        lv_textarea_add_text(rx_textarea, rx_data);
        lv_textarea_add_text(rx_textarea, "\n");
    }

    if (tx_data != NULL && tx_textarea != NULL) {
        lv_textarea_add_text(tx_textarea, tx_data);
        lv_textarea_add_text(tx_textarea, "\n");
    }

    // Update status
    if (status_label != NULL) {
        lv_label_set_text_fmt(status_label, "RX: %d bytes | TX: %d bytes", rx_count, tx_count);
    }

    bsp_display_unlock();
    xSemaphoreGive(ui_mutex);
}

/**
 * @brief RS485 communication task
 */
static void rs485_task(void* arg) {
    uint8_t* rx_buffer = (uint8_t*)malloc(RS485_BUF_SIZE);
    char* hex_str = (char*)malloc(RS485_BUF_SIZE * 3 + 1);
    int msg_counter = 0;

    if (rx_buffer == NULL || hex_str == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        vTaskDelete(NULL);
        return;
    }

    // Send initial message
    const char* init_msg = "RS485 Ready\r\n";
    rs485_send(init_msg, strlen(init_msg));

    while (1) {
        if (echo_mode) {
            // Echo mode: Read and echo back
            int len = uart_read_bytes(RS485_UART_PORT, rx_buffer, RS485_BUF_SIZE - 1,
                                       RS485_READ_TIMEOUT);

            if (len > 0) {
                rx_buffer[len] = '\0';
                rx_count += len;

                ESP_LOGI(TAG, "RX: %d bytes", len);

                // Format as hex for display
                format_hex_string(rx_buffer, len, hex_str, RS485_BUF_SIZE * 3);

                // Update UI with received data
                char display_str[128];
                snprintf(display_str, sizeof(display_str), "[%d] %s", len, hex_str);
                update_ui_data(display_str, NULL);

                // Echo back with prefix
                char echo_msg[RS485_BUF_SIZE + 32];
                snprintf(echo_msg, sizeof(echo_msg), "Echo: %s\r\n", (char*)rx_buffer);
                rs485_send(echo_msg, strlen(echo_msg));

                update_ui_data(NULL, "Echo sent");
            }
        } else {
            // Send mode: Send periodic test messages
            char test_msg[64];
            snprintf(test_msg, sizeof(test_msg), "Test message #%d\r\n", ++msg_counter);
            rs485_send(test_msg, strlen(test_msg));

            char display_str[80];
            snprintf(display_str, sizeof(display_str), "[%zu] %s",
                     strlen(test_msg) - 2, test_msg);  // -2 for \r\n
            update_ui_data(NULL, display_str);

            vTaskDelay(pdMS_TO_TICKS(2000));  // Send every 2 seconds
        }

        vTaskDelay(pdMS_TO_TICKS(10));  // Small delay to prevent busy loop
    }

    free(rx_buffer);
    free(hex_str);
    vTaskDelete(NULL);
}

/**
 * @brief Mode button callback - toggle between Echo and Send mode
 */
static void mode_btn_click_cb(lv_event_t* e) {
    echo_mode = !echo_mode;

    bsp_display_lock(0);
    if (echo_mode) {
        lv_label_set_text(lv_obj_get_child(mode_btn, 0), "Mode: Echo");
        lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x2196F3), 0);
    } else {
        lv_label_set_text(lv_obj_get_child(mode_btn, 0), "Mode: Send");
        lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0xFF9800), 0);
    }
    bsp_display_unlock();

    ESP_LOGI(TAG, "Mode changed to: %s", echo_mode ? "Echo" : "Send");
}

/**
 * @brief Send button callback - send a test message
 */
static void send_btn_click_cb(lv_event_t* e) {
    static int manual_count = 0;
    char msg[64];
    snprintf(msg, sizeof(msg), "Manual send #%d\r\n", ++manual_count);
    rs485_send(msg, strlen(msg));

    char display_str[80];
    snprintf(display_str, sizeof(display_str), "[Manual] %s", msg);
    update_ui_data(NULL, display_str);

    ESP_LOGI(TAG, "Manual message sent");
}

/**
 * @brief Clear button callback - clear text areas
 */
static void clear_btn_click_cb(lv_event_t* e) {
    bsp_display_lock(0);
    lv_textarea_set_text(rx_textarea, "");
    lv_textarea_set_text(tx_textarea, "");
    bsp_display_unlock();

    rx_count = 0;
    tx_count = 0;

    update_ui_data(NULL, NULL);  // Update status counts

    ESP_LOGI(TAG, "Cleared");
}

/**
 * @brief Create the RS485 UI
 */
static void create_ui(void) {
    lv_obj_t* scr = lv_scr_act();

    // Set background color
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "RS485 Serial");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Status label
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "RX: 0 bytes | TX: 0 bytes");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 35);

    // Connection info
    lv_obj_t* conn_info = lv_label_create(scr);
    lv_label_set_text_fmt(conn_info, "UART%d: TXD=%d RXD=%d RTS=%d @ %d baud",
                          RS485_UART_PORT, RS485_TXD_PIN, RS485_RXD_PIN,
                          RS485_RTS_PIN, RS485_BAUD_RATE);
    lv_obj_set_style_text_color(conn_info, lv_color_hex(0x666666), 0);
    lv_obj_align(conn_info, LV_ALIGN_TOP_MID, 0, 55);

    // Button row
    // Mode button
    mode_btn = lv_btn_create(scr);
    lv_obj_set_size(mode_btn, 120, 40);
    lv_obj_align(mode_btn, LV_ALIGN_TOP_LEFT, 20, 80);
    lv_obj_add_event_cb(mode_btn, mode_btn_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x2196F3), 0);

    lv_obj_t* mode_label = lv_label_create(mode_btn);
    lv_label_set_text(mode_label, "Mode: Echo");
    lv_obj_center(mode_label);

    // Send button
    send_btn = lv_btn_create(scr);
    lv_obj_set_size(send_btn, 100, 40);
    lv_obj_align(send_btn, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_add_event_cb(send_btn, send_btn_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(0x4CAF50), 0);

    lv_obj_t* send_label = lv_label_create(send_btn);
    lv_label_set_text(send_label, "Send");
    lv_obj_center(send_label);

    // Clear button
    clear_btn = lv_btn_create(scr);
    lv_obj_set_size(clear_btn, 100, 40);
    lv_obj_align(clear_btn, LV_ALIGN_TOP_RIGHT, -20, 80);
    lv_obj_add_event_cb(clear_btn, clear_btn_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0xF44336), 0);

    lv_obj_t* clear_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_center(clear_label);

    // RX section
    lv_obj_t* rx_title = lv_label_create(scr);
    lv_label_set_text(rx_title, "Received (RX):");
    lv_obj_set_style_text_color(rx_title, lv_color_hex(0x44FF44), 0);
    lv_obj_align(rx_title, LV_ALIGN_TOP_LEFT, 20, 135);

    rx_textarea = lv_textarea_create(scr);
    lv_obj_set_size(rx_textarea, LV_PCT(90), 200);
    lv_obj_align(rx_textarea, LV_ALIGN_TOP_MID, 0, 160);
    lv_textarea_set_text(rx_textarea, "");
    lv_textarea_set_placeholder_text(rx_textarea, "Received data will appear here...");
    lv_obj_set_style_bg_color(rx_textarea, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_text_color(rx_textarea, lv_color_hex(0x44FF44), 0);
    lv_obj_set_style_border_color(rx_textarea, lv_color_hex(0x44FF44), 0);

    // TX section
    lv_obj_t* tx_title = lv_label_create(scr);
    lv_label_set_text(tx_title, "Sent (TX):");
    lv_obj_set_style_text_color(tx_title, lv_color_hex(0xFF9944), 0);
    lv_obj_align(tx_title, LV_ALIGN_TOP_LEFT, 20, 375);

    tx_textarea = lv_textarea_create(scr);
    lv_obj_set_size(tx_textarea, LV_PCT(90), 200);
    lv_obj_align(tx_textarea, LV_ALIGN_TOP_MID, 0, 400);
    lv_textarea_set_text(tx_textarea, "");
    lv_textarea_set_placeholder_text(tx_textarea, "Sent data will appear here...");
    lv_obj_set_style_bg_color(tx_textarea, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_text_color(tx_textarea, lv_color_hex(0xFF9944), 0);
    lv_obj_set_style_border_color(tx_textarea, lv_color_hex(0xFF9944), 0);

    // Instructions
    lv_obj_t* instructions = lv_label_create(scr);
    lv_label_set_text(instructions, "Connect MAX485: TXD->DI, RXD->RO, RTS->DE+RE");
    lv_obj_set_style_text_color(instructions, lv_color_hex(0x555555), 0);
    lv_obj_align(instructions, LV_ALIGN_BOTTOM_MID, 0, -20);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C RS485 Serial Example");
    ESP_LOGI(TAG, "  ESP32-P4 UART RS485 + LVGL 9");
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Create UI mutex
    ui_mutex = xSemaphoreCreateMutex();
    if (ui_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Initialize RS485 UART
    ESP_LOGI(TAG, "Initializing RS485 UART...");
    init_rs485_uart();

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

    // Start RS485 task
    xTaskCreate(rs485_task, "rs485_task", 4096, NULL, 5, &rs485_task_handle);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  RS485 communication ready!");
    ESP_LOGI(TAG, "  Mode: Echo (toggle with button)");
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
