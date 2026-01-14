/**
 * @file main.cpp
 * @brief Example 06: SD Card for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - microSD card mounting
 * - File read/write operations
 * - Directory listing
 * - Display results on LCD
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 * SD Card: Slot 0 (SDMMC interface)
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// LVGL
#include "lvgl.h"

static const char *TAG = "sdcard";

// SD card handles (managed locally to fix LDO leak in BSP)
static sdmmc_card_t* sd_card = NULL;
static sd_pwr_ctrl_handle_t sd_pwr_ctrl_handle = NULL;

// LVGL UI elements
static lv_obj_t *status_label = NULL;
static lv_obj_t *file_list = NULL;
static lv_obj_t *mount_btn = NULL;
static lv_obj_t *write_btn = NULL;

// SD card state
static bool sd_mounted = false;

/**
 * @brief Mount SD card with proper LDO power control
 * This replaces bsp_sdcard_mount() to fix LDO leak on unmount/remount
 */
static esp_err_t sd_mount(void) {
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 64 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    // Create LDO power control
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &sd_pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LDO power control: %s", esp_err_to_name(ret));
        return ret;
    }
    host.pwr_ctrl_handle = sd_pwr_ctrl_handle;

    const sdmmc_slot_config_t slot_config = {
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = 0,
    };

    ret = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &sd_card);
    if (ret != ESP_OK) {
        // Clean up LDO on mount failure
        sd_pwr_ctrl_del_on_chip_ldo(sd_pwr_ctrl_handle);
        sd_pwr_ctrl_handle = NULL;
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Unmount SD card and release LDO power control
 * This replaces bsp_sdcard_unmount() to properly release the LDO
 */
static esp_err_t sd_unmount(void) {
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, sd_card);
    sd_card = NULL;

    // Release LDO power control (fixes the remount bug)
    if (sd_pwr_ctrl_handle != NULL) {
        sd_pwr_ctrl_del_on_chip_ldo(sd_pwr_ctrl_handle);
        sd_pwr_ctrl_handle = NULL;
    }

    return ret;
}

/**
 * @brief Update the file list UI
 */
static void update_file_list(void) {
    if (file_list == NULL) return;

    bsp_display_lock(0);

    // Clear existing items
    lv_obj_clean(file_list);

    if (!sd_mounted) {
        lv_obj_t *label = lv_label_create(file_list);
        lv_label_set_text(label, "SD card not mounted");
        lv_obj_set_style_text_color(label, lv_color_hex(0x888888), 0);
        bsp_display_unlock();
        return;
    }

    // Open directory
    DIR *dir = opendir(BSP_SD_MOUNT_POINT);
    if (dir == NULL) {
        lv_obj_t *label = lv_label_create(file_list);
        lv_label_set_text(label, "Failed to open directory");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFF4444), 0);
        bsp_display_unlock();
        return;
    }

    // Read directory entries
    struct dirent *entry;
    int file_count = 0;

    while ((entry = readdir(dir)) != NULL && file_count < 15) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Get file info
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%s", BSP_SD_MOUNT_POINT, entry->d_name);

        struct stat st;
        stat(filepath, &st);

        // Create item
        lv_obj_t *item = lv_obj_create(file_list);
        lv_obj_set_size(item, LV_PCT(95), 45);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 5, 0);

        // File name
        lv_obj_t *name_label = lv_label_create(item);
        if (S_ISDIR(st.st_mode)) {
            lv_label_set_text_fmt(name_label, "[DIR] %s", entry->d_name);
        } else {
            lv_label_set_text(name_label, entry->d_name);
        }
        lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
        lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 5, 2);

        // File size
        if (!S_ISDIR(st.st_mode)) {
            lv_obj_t *size_label = lv_label_create(item);
            if (st.st_size < 1024) {
                lv_label_set_text_fmt(size_label, "%ld bytes", (long)st.st_size);
            } else if (st.st_size < 1024 * 1024) {
                lv_label_set_text_fmt(size_label, "%.1f KB", (float)st.st_size / 1024);
            } else {
                lv_label_set_text_fmt(size_label, "%.1f MB", (float)st.st_size / (1024 * 1024));
            }
            lv_obj_set_style_text_color(size_label, lv_color_hex(0x88CCFF), 0);
            lv_obj_set_style_text_font(size_label, &lv_font_montserrat_14, 0);
            lv_obj_align(size_label, LV_ALIGN_BOTTOM_LEFT, 5, -2);
        }

        file_count++;
    }

    closedir(dir);

    if (file_count == 0) {
        lv_obj_t *label = lv_label_create(file_list);
        lv_label_set_text(label, "SD card is empty");
        lv_obj_set_style_text_color(label, lv_color_hex(0x888888), 0);
    }

    bsp_display_unlock();
}

/**
 * @brief Mount button callback
 */
static void mount_btn_click_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Mount button clicked");

    bsp_display_lock(0);

    if (sd_mounted) {
        // Unmount
        esp_err_t ret = sd_unmount();
        if (ret == ESP_OK) {
            sd_mounted = false;
            lv_label_set_text(status_label, "SD card unmounted");
            lv_label_set_text(lv_obj_get_child(mount_btn, 0), "Mount");
            ESP_LOGI(TAG, "SD card unmounted");
        } else {
            lv_label_set_text(status_label, "Unmount failed!");
            ESP_LOGE(TAG, "Failed to unmount: %s", esp_err_to_name(ret));
        }
    } else {
        // Mount
        lv_label_set_text(status_label, "Mounting...");
        bsp_display_unlock();

        esp_err_t ret = sd_mount();

        bsp_display_lock(0);
        if (ret == ESP_OK) {
            sd_mounted = true;
            lv_label_set_text_fmt(status_label, "Mounted: %s (%.1f MB)",
                                   sd_card->cid.name,
                                   (float)((uint64_t)sd_card->csd.capacity * sd_card->csd.sector_size) / (1024 * 1024));
            lv_label_set_text(lv_obj_get_child(mount_btn, 0), "Unmount");
            ESP_LOGI(TAG, "SD card mounted");
        } else {
            lv_label_set_text(status_label, "Mount failed! Insert SD card");
            ESP_LOGE(TAG, "Failed to mount: %s", esp_err_to_name(ret));
        }
    }

    bsp_display_unlock();

    // Update file list
    update_file_list();
}

/**
 * @brief Write test file button callback
 */
static void write_btn_click_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Write button clicked");

    if (!sd_mounted) {
        bsp_display_lock(0);
        lv_label_set_text(status_label, "Mount SD card first!");
        bsp_display_unlock();
        return;
    }

    // Create test file
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/test_%lu.txt", BSP_SD_MOUNT_POINT, (unsigned long)(esp_timer_get_time() / 1000000));

    FILE *f = fopen(filepath, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create file");
        bsp_display_lock(0);
        lv_label_set_text(status_label, "Failed to create file!");
        bsp_display_unlock();
        return;
    }

    // Write test content
    fprintf(f, "JC4880P443C SD Card Test\n");
    fprintf(f, "========================\n");
    fprintf(f, "ESP32-P4 Development Board\n");
    fprintf(f, "Guition JC-ESP32P4-M3-C6 Module\n");
    fprintf(f, "\n");
    fprintf(f, "Timestamp: %llu ms\n", (unsigned long long)(esp_timer_get_time() / 1000));
    fprintf(f, "Free heap: %lu bytes\n", (unsigned long)esp_get_free_heap_size());

    fclose(f);

    ESP_LOGI(TAG, "Test file created: %s", filepath);

    bsp_display_lock(0);
    lv_label_set_text(status_label, "Test file created!");
    bsp_display_unlock();

    // Update file list
    update_file_list();
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
    lv_label_set_text(title, "SD Card Demo");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Status label
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Press Mount to access SD card");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 50);

    // Mount button
    mount_btn = lv_btn_create(scr);
    lv_obj_set_size(mount_btn, 140, 50);
    lv_obj_align(mount_btn, LV_ALIGN_TOP_LEFT, 30, 85);
    lv_obj_add_event_cb(mount_btn, mount_btn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *mount_label = lv_label_create(mount_btn);
    lv_label_set_text(mount_label, "Mount");
    lv_obj_center(mount_label);

    // Write test file button
    write_btn = lv_btn_create(scr);
    lv_obj_set_size(write_btn, 140, 50);
    lv_obj_align(write_btn, LV_ALIGN_TOP_RIGHT, -30, 85);
    lv_obj_add_event_cb(write_btn, write_btn_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(write_btn, lv_color_hex(0x2d8659), 0);

    lv_obj_t *write_label = lv_label_create(write_btn);
    lv_label_set_text(write_label, "Write Test");
    lv_obj_center(write_label);

    // File list container
    file_list = lv_obj_create(scr);
    lv_obj_set_size(file_list, LV_PCT(95), 520);
    lv_obj_align(file_list, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(file_list, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(file_list, 0, 0);
    lv_obj_set_flex_flow(file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(file_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(file_list, 5, 0);

    // Initial message
    lv_obj_t *hint = lv_label_create(file_list);
    lv_label_set_text(hint, "SD card files will appear here");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C SD Card Example");
    ESP_LOGI(TAG, "  ESP32-P4 SDMMC + LVGL 9");
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
    ESP_LOGI(TAG, "  SD Card demo ready!");
    ESP_LOGI(TAG, "  Insert SD card and press Mount");
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
