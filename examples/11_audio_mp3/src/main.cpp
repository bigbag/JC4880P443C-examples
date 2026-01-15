/**
 * @file main.cpp
 * @brief Example 11: Audio MP3 Player for JC4880P443C (ESP32-P4)
 *
 * This example demonstrates:
 * - MP3 playback from SD card using audio_player component
 * - Volume control via audio codec
 * - LVGL UI with play/pause, next/prev, volume controls
 * - Track listing and selection
 *
 * Board: Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)
 *
 * Requirements:
 * - SD card with MP3 files in /sdcard/music/ directory
 * - Audio codec hardware (ES8311 or similar)
 *
 * SD Card structure:
 *   /sdcard/
 *     music/
 *       track1.mp3
 *       track2.mp3
 *       ...
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

// BSP includes
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

// LVGL
#include "lvgl.h"

static const char* TAG = "audio_mp3";

// Music directory on SD card
#define MUSIC_DIR           "/sdcard/music"
#define MAX_TRACKS          50
#define MAX_FILENAME_LEN    64

// SD card handles
static sdmmc_card_t* sd_card = NULL;
static sd_pwr_ctrl_handle_t sd_pwr_ctrl_handle = NULL;

// Audio state
static file_iterator_instance_t* file_iterator = NULL;
static int total_tracks = 0;
static int current_track = 0;
static bool is_playing = false;
static int current_volume = 50;

// Playback completion semaphore
static SemaphoreHandle_t playback_semaphore = NULL;

// LVGL UI elements
static lv_obj_t* track_label = NULL;
static lv_obj_t* status_label = NULL;
static lv_obj_t* play_btn = NULL;
static lv_obj_t* prev_btn = NULL;
static lv_obj_t* next_btn = NULL;
static lv_obj_t* volume_slider = NULL;
static lv_obj_t* volume_label = NULL;
static lv_obj_t* track_list = NULL;
static lv_obj_t* track_count_label = NULL;

/**
 * @brief Mount SD card with LDO power control
 */
static esp_err_t mount_sd_card(void) {
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
        sd_pwr_ctrl_del_on_chip_ldo(sd_pwr_ctrl_handle);
        sd_pwr_ctrl_handle = NULL;
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Audio player callback - called when playback finishes
 */
static void audio_player_callback(audio_player_cb_ctx_t* ctx) {
    ESP_LOGI(TAG, "Audio callback event: %d", ctx->audio_event);

    if (ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_IDLE ||
        ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN) {
        is_playing = false;
        if (playback_semaphore != NULL) {
            xSemaphoreGive(playback_semaphore);
        }
    }
}

/**
 * @brief Play the current track
 */
static void play_current_track(void) {
    if (file_iterator == NULL || total_tracks == 0) {
        ESP_LOGW(TAG, "No tracks available");
        return;
    }

    ESP_LOGI(TAG, "Playing track %d of %d", current_track + 1, total_tracks);
    esp_err_t ret = bsp_extra_player_play_index(file_iterator, current_track);
    if (ret == ESP_OK) {
        is_playing = true;
    } else {
        ESP_LOGE(TAG, "Failed to play track: %s", esp_err_to_name(ret));
        is_playing = false;
    }
}

/**
 * @brief Update UI with current state
 */
static void update_ui(void) {
    if (track_label == NULL) return;

    bsp_display_lock(0);

    // Update track label
    if (file_iterator != NULL && total_tracks > 0) {
        const char* filename = file_iterator_get_name_from_index(file_iterator, current_track);
        if (filename != NULL) {
            lv_label_set_text(track_label, filename);
        } else {
            lv_label_set_text(track_label, "Unknown Track");
        }
    } else {
        lv_label_set_text(track_label, "No tracks found");
    }

    // Update status
    if (is_playing) {
        lv_label_set_text(status_label, "Playing");
        lv_label_set_text(lv_obj_get_child(play_btn, 0), "Pause");
    } else {
        lv_label_set_text(status_label, "Stopped");
        lv_label_set_text(lv_obj_get_child(play_btn, 0), "Play");
    }

    // Update track count
    lv_label_set_text_fmt(track_count_label, "Track %d / %d", current_track + 1, total_tracks);

    bsp_display_unlock();
}

/**
 * @brief Play/Pause button callback
 */
static void play_btn_click_cb(lv_event_t* e) {
    if (is_playing) {
        // Pause (stop current playback)
        bsp_extra_codec_dev_stop();
        is_playing = false;
        ESP_LOGI(TAG, "Paused");
    } else {
        // Play
        play_current_track();
        ESP_LOGI(TAG, "Playing");
    }
    update_ui();
}

/**
 * @brief Previous track button callback
 */
static void prev_btn_click_cb(lv_event_t* e) {
    if (total_tracks == 0) return;

    if (current_track > 0) {
        current_track--;
    } else {
        current_track = total_tracks - 1;  // Wrap to last
    }

    if (is_playing) {
        play_current_track();
    }
    update_ui();
    ESP_LOGI(TAG, "Previous track: %d", current_track);
}

/**
 * @brief Next track button callback
 */
static void next_btn_click_cb(lv_event_t* e) {
    if (total_tracks == 0) return;

    if (current_track < total_tracks - 1) {
        current_track++;
    } else {
        current_track = 0;  // Wrap to first
    }

    if (is_playing) {
        play_current_track();
    }
    update_ui();
    ESP_LOGI(TAG, "Next track: %d", current_track);
}

/**
 * @brief Volume slider callback
 */
static void volume_slider_changed_cb(lv_event_t* e) {
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    current_volume = lv_slider_get_value(slider);

    int actual_volume = 0;
    bsp_extra_codec_volume_set(current_volume, &actual_volume);

    bsp_display_lock(0);
    lv_label_set_text_fmt(volume_label, "Vol: %d%%", current_volume);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Volume: %d%%", current_volume);
}

/**
 * @brief Track list item click callback
 */
static void track_list_click_cb(lv_event_t* e) {
    lv_obj_t* list = (lv_obj_t*)lv_event_get_target(e);
    uint32_t idx = lv_obj_get_index(list);

    if (idx < (uint32_t)total_tracks) {
        current_track = idx;
        play_current_track();
        update_ui();
        ESP_LOGI(TAG, "Selected track: %d", current_track);
    }
}

/**
 * @brief Populate track list from SD card
 */
static void populate_track_list(void) {
    if (track_list == NULL) return;

    bsp_display_lock(0);
    lv_obj_clean(track_list);

    if (file_iterator == NULL || total_tracks == 0) {
        lv_obj_t* item = lv_list_add_text(track_list, "No MP3 files found");
        lv_obj_set_style_text_color(item, lv_color_hex(0x888888), 0);
        bsp_display_unlock();
        return;
    }

    for (int i = 0; i < total_tracks && i < MAX_TRACKS; i++) {
        const char* filename = file_iterator_get_name_from_index(file_iterator, i);
        if (filename != NULL) {
            lv_obj_t* btn = lv_list_add_btn(track_list, LV_SYMBOL_AUDIO, filename);
            lv_obj_add_event_cb(btn, track_list_click_cb, LV_EVENT_CLICKED, NULL);
        }
    }

    bsp_display_unlock();
}

/**
 * @brief Create the MP3 player UI
 */
static void create_ui(void) {
    lv_obj_t* scr = lv_scr_act();

    // Set background color
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "MP3 Player");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Track count label
    track_count_label = lv_label_create(scr);
    lv_label_set_text(track_count_label, "Track 0 / 0");
    lv_obj_set_style_text_color(track_count_label, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(track_count_label, LV_ALIGN_TOP_MID, 0, 40);

    // Current track label
    track_label = lv_label_create(scr);
    lv_label_set_text(track_label, "Loading...");
    lv_obj_set_style_text_color(track_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(track_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(track_label, 400);
    lv_label_set_long_mode(track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(track_label, LV_ALIGN_TOP_MID, 0, 70);

    // Status label
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Stopped");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x44FF44), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 95);

    // Control buttons row
    // Previous button
    prev_btn = lv_btn_create(scr);
    lv_obj_set_size(prev_btn, 80, 50);
    lv_obj_align(prev_btn, LV_ALIGN_TOP_LEFT, 40, 130);
    lv_obj_add_event_cb(prev_btn, prev_btn_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(prev_btn, lv_color_hex(0x555555), 0);

    lv_obj_t* prev_label = lv_label_create(prev_btn);
    lv_label_set_text(prev_label, LV_SYMBOL_PREV);
    lv_obj_center(prev_label);

    // Play/Pause button
    play_btn = lv_btn_create(scr);
    lv_obj_set_size(play_btn, 120, 50);
    lv_obj_align(play_btn, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_add_event_cb(play_btn, play_btn_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x4CAF50), 0);

    lv_obj_t* play_label = lv_label_create(play_btn);
    lv_label_set_text(play_label, "Play");
    lv_obj_center(play_label);

    // Next button
    next_btn = lv_btn_create(scr);
    lv_obj_set_size(next_btn, 80, 50);
    lv_obj_align(next_btn, LV_ALIGN_TOP_RIGHT, -40, 130);
    lv_obj_add_event_cb(next_btn, next_btn_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x555555), 0);

    lv_obj_t* next_label = lv_label_create(next_btn);
    lv_label_set_text(next_label, LV_SYMBOL_NEXT);
    lv_obj_center(next_label);

    // Volume control
    volume_label = lv_label_create(scr);
    lv_label_set_text_fmt(volume_label, "Vol: %d%%", current_volume);
    lv_obj_set_style_text_color(volume_label, lv_color_hex(0xFFAA00), 0);
    lv_obj_align(volume_label, LV_ALIGN_TOP_LEFT, 20, 200);

    volume_slider = lv_slider_create(scr);
    lv_obj_set_size(volume_slider, 300, 20);
    lv_obj_align(volume_slider, LV_ALIGN_TOP_MID, 40, 200);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, current_volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(volume_slider, volume_slider_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(0xFFAA00), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(0xFFCC00), LV_PART_KNOB);

    // Track list
    lv_obj_t* list_title = lv_label_create(scr);
    lv_label_set_text(list_title, "Tracks:");
    lv_obj_set_style_text_color(list_title, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(list_title, LV_ALIGN_TOP_LEFT, 20, 240);

    track_list = lv_list_create(scr);
    lv_obj_set_size(track_list, LV_PCT(95), 400);
    lv_obj_align(track_list, LV_ALIGN_TOP_MID, 0, 265);
    lv_obj_set_style_bg_color(track_list, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(track_list, 0, 0);

    // Instructions
    lv_obj_t* instructions = lv_label_create(scr);
    lv_label_set_text(instructions, "Place MP3 files in /sdcard/music/");
    lv_obj_set_style_text_color(instructions, lv_color_hex(0x555555), 0);
    lv_obj_align(instructions, LV_ALIGN_BOTTOM_MID, 0, -20);
}

/**
 * @brief Auto-play next track task
 */
static void auto_play_task(void* arg) {
    while (1) {
        // Wait for playback to complete
        if (xSemaphoreTake(playback_semaphore, portMAX_DELAY) == pdTRUE) {
            // Auto-advance to next track
            if (total_tracks > 0) {
                vTaskDelay(pdMS_TO_TICKS(500));  // Brief pause between tracks

                current_track++;
                if (current_track >= total_tracks) {
                    current_track = 0;  // Loop back to start
                }

                play_current_track();
                update_ui();
            }
        }
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  JC4880P443C Audio MP3 Player");
    ESP_LOGI(TAG, "  ESP32-P4 + Audio Codec + LVGL 9");
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Create playback semaphore
    playback_semaphore = xSemaphoreCreateBinary();

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

    // Mount SD card
    ESP_LOGI(TAG, "Mounting SD card...");
    ret = mount_sd_card();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed - insert SD card with MP3 files");
        bsp_display_lock(0);
        lv_label_set_text(status_label, "Insert SD card");
        lv_label_set_text(track_label, "No SD card found");
        bsp_display_unlock();
    } else {
        ESP_LOGI(TAG, "SD card mounted: %s (%.1f GB)",
                 sd_card->cid.name,
                 (float)((uint64_t)sd_card->csd.capacity * sd_card->csd.sector_size) / (1024 * 1024 * 1024));

        // Initialize audio codec
        ESP_LOGI(TAG, "Initializing audio codec...");
        ret = bsp_extra_codec_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize codec");
        } else {
            ESP_LOGI(TAG, "Audio codec initialized");

            // Set initial volume
            bsp_extra_codec_volume_set(current_volume, NULL);

            // Initialize audio player
            ESP_LOGI(TAG, "Initializing audio player...");
            ret = bsp_extra_player_init();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Audio player initialized");

                // Register callback
                bsp_extra_player_register_callback(audio_player_callback, NULL);

                // Initialize file iterator for music directory
                ESP_LOGI(TAG, "Scanning music directory: %s", MUSIC_DIR);
                ret = bsp_extra_file_instance_init(MUSIC_DIR, &file_iterator);
                if (ret == ESP_OK && file_iterator != NULL) {
                    total_tracks = file_iterator_get_count(file_iterator);
                    ESP_LOGI(TAG, "Found %d tracks", total_tracks);
                } else {
                    ESP_LOGW(TAG, "No music files found in %s", MUSIC_DIR);
                    total_tracks = 0;
                }
            }
        }
    }

    // Populate track list
    populate_track_list();
    update_ui();

    // Start auto-play task
    xTaskCreate(auto_play_task, "auto_play", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MP3 Player ready!");
    ESP_LOGI(TAG, "  Tracks found: %d", total_tracks);
    ESP_LOGI(TAG, "========================================");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
