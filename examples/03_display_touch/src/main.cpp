/**
 * @file main.cpp
 * @brief Example 03: Display Touch for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - Touch coordinate display in real-time
 * - Interactive drawing canvas
 * - Touch state visualization
 * - Clear button to reset canvas
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * Display: 480x800 MIPI-DSI with ST7701 controller
 * Touch: GT911 capacitive touch controller
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

static const char *TAG = "display_touch";

// Canvas dimensions (smaller than screen for UI elements)
#define CANVAS_WIDTH  460
#define CANVAS_HEIGHT 550

// UI elements
static lv_obj_t *coord_label = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *canvas = NULL;
static lv_layer_t canvas_layer;
static uint8_t *canvas_buf = NULL;

// Drawing state
static bool is_drawing = false;
static lv_point_t last_point = {0, 0};

// Drawing color
static lv_color_t draw_color = {0};
static int color_index = 0;
static const uint32_t colors[] = {
    0xFF0000,  // Red
    0x00FF00,  // Green
    0x0000FF,  // Blue
    0xFFFF00,  // Yellow
    0xFF00FF,  // Magenta
    0x00FFFF,  // Cyan
    0xFFFFFF,  // White
};
#define NUM_COLORS (sizeof(colors) / sizeof(colors[0]))

/**
 * @brief Draw a circle on the canvas at the given position
 */
static void draw_on_canvas(int32_t x, int32_t y) {
    if (canvas == NULL) return;

    // Adjust coordinates relative to canvas
    lv_obj_t *canvas_parent = lv_obj_get_parent(canvas);
    int32_t canvas_x = lv_obj_get_x(canvas);
    int32_t canvas_y = lv_obj_get_y(canvas);

    // Get screen coordinates
    lv_obj_t *scr = lv_scr_act();
    int32_t scr_x = lv_obj_get_x2(scr);

    // Calculate relative position
    int32_t rel_x = x - canvas_x;
    int32_t rel_y = y - canvas_y;

    // Check bounds
    if (rel_x < 0 || rel_x >= CANVAS_WIDTH || rel_y < 0 || rel_y >= CANVAS_HEIGHT) {
        return;
    }

    // Initialize the canvas layer for drawing
    lv_canvas_init_layer(canvas, &canvas_layer);

    // Draw a filled circle at touch point
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = draw_color;
    arc_dsc.width = 8;
    arc_dsc.center.x = rel_x;
    arc_dsc.center.y = rel_y;
    arc_dsc.radius = 4;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;

    lv_draw_arc(&canvas_layer, &arc_dsc);

    // If drawing continuously, draw line from last point
    if (is_drawing && (last_point.x != 0 || last_point.y != 0)) {
        int32_t last_rel_x = last_point.x - canvas_x;
        int32_t last_rel_y = last_point.y - canvas_y;

        if (last_rel_x >= 0 && last_rel_x < CANVAS_WIDTH &&
            last_rel_y >= 0 && last_rel_y < CANVAS_HEIGHT) {
            lv_draw_line_dsc_t line_dsc;
            lv_draw_line_dsc_init(&line_dsc);
            line_dsc.color = draw_color;
            line_dsc.width = 8;
            line_dsc.round_start = true;
            line_dsc.round_end = true;
            line_dsc.p1.x = last_rel_x;
            line_dsc.p1.y = last_rel_y;
            line_dsc.p2.x = rel_x;
            line_dsc.p2.y = rel_y;

            lv_draw_line(&canvas_layer, &line_dsc);
        }
    }

    // Finish drawing
    lv_canvas_finish_layer(canvas, &canvas_layer);

    last_point.x = x;
    last_point.y = y;
}

/**
 * @brief Clear the canvas
 */
static void clear_canvas(void) {
    if (canvas == NULL || canvas_buf == NULL) return;

    lv_canvas_fill_bg(canvas, lv_color_hex(0x1a1a2e), LV_OPA_COVER);
    last_point.x = 0;
    last_point.y = 0;
    ESP_LOGI(TAG, "Canvas cleared");
}

/**
 * @brief Touch event handler for canvas
 */
static void canvas_touch_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();

    if (indev == NULL) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        is_drawing = true;
        last_point.x = 0;
        last_point.y = 0;

        // Update state label
        if (state_label) {
            lv_label_set_text(state_label, "State: PRESSED");
            lv_obj_set_style_text_color(state_label, lv_color_hex(0x00FF00), 0);
        }

        draw_on_canvas(point.x, point.y);

    } else if (code == LV_EVENT_PRESSING) {
        // Update coordinates
        if (coord_label) {
            lv_label_set_text_fmt(coord_label, "X: %d  Y: %d", (int)point.x, (int)point.y);
        }

        draw_on_canvas(point.x, point.y);

    } else if (code == LV_EVENT_RELEASED) {
        is_drawing = false;
        last_point.x = 0;
        last_point.y = 0;

        // Update state label
        if (state_label) {
            lv_label_set_text(state_label, "State: RELEASED");
            lv_obj_set_style_text_color(state_label, lv_color_hex(0x888888), 0);
        }
    }
}

/**
 * @brief Clear button callback
 */
static void clear_btn_cb(lv_event_t *e) {
    bsp_display_lock(0);
    clear_canvas();
    bsp_display_unlock();
}

/**
 * @brief Color button callback - cycle through colors
 */
static void color_btn_cb(lv_event_t *e) {
    color_index = (color_index + 1) % NUM_COLORS;
    draw_color = lv_color_hex(colors[color_index]);

    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_set_style_bg_color(btn, draw_color, 0);

    ESP_LOGI(TAG, "Color changed to index %d", color_index);
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
    lv_label_set_text(title, "Touch Demo");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Coordinate label
    coord_label = lv_label_create(scr);
    lv_label_set_text(coord_label, "X: ---  Y: ---");
    lv_obj_set_style_text_color(coord_label, lv_color_hex(0x88CCFF), 0);
    lv_obj_set_style_text_font(coord_label, &lv_font_montserrat_16, 0);
    lv_obj_align(coord_label, LV_ALIGN_TOP_LEFT, 10, 40);

    // State label
    state_label = lv_label_create(scr);
    lv_label_set_text(state_label, "State: IDLE");
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_16, 0);
    lv_obj_align(state_label, LV_ALIGN_TOP_RIGHT, -10, 40);

    // Create canvas for drawing
    // Allocate buffer in PSRAM
    size_t buf_size = LV_CANVAS_BUF_SIZE(CANVAS_WIDTH, CANVAS_HEIGHT, 16, LV_DRAW_BUF_STRIDE_ALIGN);
    canvas_buf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (canvas_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer!");
        return;
    }
    ESP_LOGI(TAG, "Canvas buffer allocated: %d bytes", (int)buf_size);

    canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(canvas, LV_ALIGN_TOP_MID, 0, 70);

    // Set canvas background
    lv_canvas_fill_bg(canvas, lv_color_hex(0x1a1a2e), LV_OPA_COVER);

    // Add border to canvas
    lv_obj_set_style_border_width(canvas, 2, 0);
    lv_obj_set_style_border_color(canvas, lv_color_hex(0x444466), 0);

    // Make canvas clickable and add touch event
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas, canvas_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(canvas, canvas_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(canvas, canvas_touch_cb, LV_EVENT_RELEASED, NULL);

    // Initialize drawing color
    draw_color = lv_color_hex(colors[0]);

    // Button container at bottom
    lv_obj_t *btn_container = lv_obj_create(scr);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, 460, 60);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Clear button
    lv_obj_t *clear_btn = lv_btn_create(btn_container);
    lv_obj_set_size(clear_btn, 150, 50);
    lv_obj_add_event_cb(clear_btn, clear_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clear_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_center(clear_label);

    // Color button
    lv_obj_t *color_btn = lv_btn_create(btn_container);
    lv_obj_set_size(color_btn, 150, 50);
    lv_obj_set_style_bg_color(color_btn, draw_color, 0);
    lv_obj_add_event_cb(color_btn, color_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *color_label = lv_label_create(color_btn);
    lv_label_set_text(color_label, "Color");
    lv_obj_center(color_label);

    // Instructions label
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Draw with your finger on the canvas");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666688), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -85);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C Touch Demo Example");
    ESP_LOGI(TAG, "  ESP32-P4 + LVGL 9 + GT911 Touch");
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

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Touch Demo ready!");
    ESP_LOGI(TAG, "  Draw on canvas, tap Color to change");
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
