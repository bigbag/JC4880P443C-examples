/**
 * @file main.cpp
 * @brief Example 02: Display Images for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - Drawing shapes and colors with LVGL
 * - Screen rotation options
 * - Color gradients and fills
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * Display: 480x800 MIPI-DSI with ST7701 controller
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char *TAG = "display_images";

// Demo state
static int current_demo = 0;
static lv_obj_t *demo_container = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *info_label = NULL;

/**
 * @brief Clear the demo container
 */
static void clear_demo(void) {
    if (demo_container) {
        lv_obj_clean(demo_container);
    }
}

/**
 * @brief Demo 1: Color bars
 */
static void demo_color_bars(void) {
    clear_demo();
    lv_label_set_text(title_label, "Color Bars");
    lv_label_set_text(info_label, "RGB color test pattern");

    const lv_color_t colors[] = {
        lv_color_hex(0xFF0000),  // Red
        lv_color_hex(0x00FF00),  // Green
        lv_color_hex(0x0000FF),  // Blue
        lv_color_hex(0xFFFF00),  // Yellow
        lv_color_hex(0xFF00FF),  // Magenta
        lv_color_hex(0x00FFFF),  // Cyan
        lv_color_hex(0xFFFFFF),  // White
        lv_color_hex(0x000000),  // Black
    };

    int bar_width = 480 / 8;
    for (int i = 0; i < 8; i++) {
        lv_obj_t *bar = lv_obj_create(demo_container);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, bar_width, 500);
        lv_obj_set_pos(bar, i * bar_width, 0);
        lv_obj_set_style_bg_color(bar, colors[i], 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
}

/**
 * @brief Demo 2: Gradient
 */
static void demo_gradient(void) {
    clear_demo();
    lv_label_set_text(title_label, "Gradient");
    lv_label_set_text(info_label, "Smooth color transition");

    lv_obj_t *grad = lv_obj_create(demo_container);
    lv_obj_remove_style_all(grad);
    lv_obj_set_size(grad, 460, 480);
    lv_obj_center(grad);
    lv_obj_set_style_bg_color(grad, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_grad_color(grad, lv_color_hex(0x0000FF), 0);
    lv_obj_set_style_bg_grad_dir(grad, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(grad, LV_OPA_COVER, 0);
}

/**
 * @brief Demo 3: Shapes
 */
static void demo_shapes(void) {
    clear_demo();
    lv_label_set_text(title_label, "Shapes");
    lv_label_set_text(info_label, "Various LVGL shapes");

    // Circle
    lv_obj_t *circle = lv_obj_create(demo_container);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, 150, 150);
    lv_obj_set_pos(circle, 50, 50);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xFF5733), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);

    // Rounded rectangle
    lv_obj_t *rect = lv_obj_create(demo_container);
    lv_obj_remove_style_all(rect);
    lv_obj_set_size(rect, 180, 100);
    lv_obj_set_pos(rect, 250, 50);
    lv_obj_set_style_radius(rect, 20, 0);
    lv_obj_set_style_bg_color(rect, lv_color_hex(0x33FF57), 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);

    // Square with border
    lv_obj_t *square = lv_obj_create(demo_container);
    lv_obj_remove_style_all(square);
    lv_obj_set_size(square, 120, 120);
    lv_obj_set_pos(square, 50, 250);
    lv_obj_set_style_bg_color(square, lv_color_hex(0x3357FF), 0);
    lv_obj_set_style_bg_opa(square, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(square, 5, 0);
    lv_obj_set_style_border_color(square, lv_color_white(), 0);

    // Ring (circle with hole)
    lv_obj_t *ring = lv_obj_create(demo_container);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 140, 140);
    lv_obj_set_pos(ring, 230, 220);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 20, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0xFF33FF), 0);
}

/**
 * @brief Demo 4: Animation
 */
static void demo_animation(void) {
    clear_demo();
    lv_label_set_text(title_label, "Animation");
    lv_label_set_text(info_label, "Animated spinner");

    // Spinner
    lv_obj_t *spinner = lv_spinner_create(demo_container);
    lv_obj_set_size(spinner, 200, 200);
    lv_obj_center(spinner);
    lv_spinner_set_anim_params(spinner, 1000, 200);
}

/**
 * @brief Demo 5: Text styles
 */
static void demo_text(void) {
    clear_demo();
    lv_label_set_text(title_label, "Text Styles");
    lv_label_set_text(info_label, "Font rendering demo");

    lv_obj_t *label1 = lv_label_create(demo_container);
    lv_label_set_text(label1, "JC4880P443C");
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label1, lv_color_white(), 0);
    lv_obj_set_pos(label1, 50, 50);

    lv_obj_t *label2 = lv_label_create(demo_container);
    lv_label_set_text(label2, "ESP32-P4 + ESP32-C6");
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label2, lv_color_hex(0x88CCFF), 0);
    lv_obj_set_pos(label2, 50, 100);

    lv_obj_t *label3 = lv_label_create(demo_container);
    lv_label_set_text(label3, "480x800 MIPI-DSI Display");
    lv_obj_set_style_text_font(label3, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label3, lv_color_hex(0xFFCC88), 0);
    lv_obj_set_pos(label3, 50, 150);

    lv_obj_t *label4 = lv_label_create(demo_container);
    lv_label_set_text(label4, "LVGL 9 Graphics Library");
    lv_obj_set_style_text_font(label4, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label4, lv_color_hex(0x88FF88), 0);
    lv_obj_set_pos(label4, 50, 200);

    // Large number
    lv_obj_t *big_num = lv_label_create(demo_container);
    lv_label_set_text(big_num, "2026");
    lv_obj_set_style_text_font(big_num, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(big_num, lv_color_hex(0xFF8888), 0);
    lv_obj_set_pos(big_num, 50, 300);
}

typedef void (*demo_func_t)(void);
static const demo_func_t demos[] = {
    demo_color_bars,
    demo_gradient,
    demo_shapes,
    demo_animation,
    demo_text,
};
#define NUM_DEMOS (sizeof(demos) / sizeof(demos[0]))

/**
 * @brief Button callback to switch demos
 */
static void next_demo_cb(lv_event_t *e) {
    current_demo = (current_demo + 1) % NUM_DEMOS;
    bsp_display_lock(0);
    demos[current_demo]();
    bsp_display_unlock();
    ESP_LOGI(TAG, "Switched to demo %d", current_demo);
}

/**
 * @brief Create the UI
 */
static void create_ui(void) {
    lv_obj_t *scr = lv_scr_act();

    // Set dark background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), LV_PART_MAIN);

    // Title
    title_label = lv_label_create(scr);
    lv_label_set_text(title_label, "Display Demo");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 10);

    // Info label
    info_label = lv_label_create(scr);
    lv_label_set_text(info_label, "Tap Next to change demo");
    lv_obj_set_style_text_color(info_label, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(info_label, LV_ALIGN_TOP_MID, 0, 35);

    // Next button
    lv_obj_t *next_btn = lv_btn_create(scr);
    lv_obj_set_size(next_btn, 120, 45);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(next_btn, next_demo_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(next_btn);
    lv_label_set_text(btn_label, "Next");
    lv_obj_center(btn_label);

    // Demo container
    demo_container = lv_obj_create(scr);
    lv_obj_remove_style_all(demo_container);
    lv_obj_set_size(demo_container, 480, 500);
    lv_obj_align(demo_container, LV_ALIGN_CENTER, 0, 20);

    // Show first demo
    demos[0]();
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C Display Images Example");
    ESP_LOGI(TAG, "  ESP32-P4 + LVGL 9");
    ESP_LOGI(TAG, "========================================");

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
    ESP_LOGI(TAG, "  Display Images demo ready!");
    ESP_LOGI(TAG, "  Touch 'Next' to switch demos");
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
