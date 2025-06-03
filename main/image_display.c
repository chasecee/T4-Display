#include "image_display.h" // For preloaded_jpeg_frame_t
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "jpeg_decoder.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"

static const char *TAG = "T4_IMAGE_DISPLAY";

// LOGICAL dimensions after panel transformations (e.g., from main.c's perspective)
#define LOGICAL_DISPLAY_WIDTH  320 
#define LOGICAL_DISPLAY_HEIGHT 240

// T4 V1.3 Pin definitions
#define PIN_NUM_MISO      12
#define PIN_NUM_MOSI      23
#define PIN_NUM_CLK       18
#define PIN_NUM_CS        27
#define PIN_NUM_DC        32
#define PIN_NUM_RST       5
#define PIN_NUM_BCKL      4

// Display resolution for ILI9341
#define LCD_H_RES         240
#define LCD_V_RES         320

// Use the panel_handle that's already created in main.c
extern esp_lcd_panel_handle_t panel_handle;

// JPEG decoder configuration
// #define JPEG_DECODE_BUFFER_SIZE (LCD_H_RES * 16)  // Buffer for 16 lines at a time - Unused
// #define JPEG_DECODE_TIMEOUT_MS 1000 // Unused

// Add these at the top with other globals
static preloaded_jpeg_frame_t* g_preloaded_frames = NULL;
static uint8_t* g_all_jpeg_data_psram = NULL;
static uint8_t* g_common_out_buf = NULL;
static uint8_t* g_common_work_buf = NULL;
static int g_num_loaded_frames = 0;
static bool g_frames_loaded = false;

// Function to decode and display JPEG image from a data buffer
esp_err_t decode_and_display_jpeg(const uint8_t* jpeg_data, size_t jpeg_data_size, 
                                  uint8_t* external_out_buffer, size_t external_out_buffer_size, 
                                  uint8_t* external_work_buffer, size_t external_work_buffer_size) {
    if (jpeg_data == NULL || jpeg_data_size == 0) {
        ESP_LOGE(TAG, "❌ Invalid JPEG data pointer or size");
        return ESP_ERR_INVALID_ARG;
    }

    esp_jpeg_image_cfg_t jpeg_cfg = {
        .indata = jpeg_data,
        .indata_size = jpeg_data_size,
        .outbuf = NULL, 
        .outbuf_size = 0,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = { .swap_color_bytes = 1 },
        .advanced = { .working_buffer = NULL, .working_buffer_size = 0 },
    };

    if (external_work_buffer != NULL) {
        jpeg_cfg.advanced.working_buffer = external_work_buffer;
        jpeg_cfg.advanced.working_buffer_size = external_work_buffer_size;
    }

    esp_jpeg_image_output_t jpeg_info;
    esp_err_t ret = esp_jpeg_get_image_info(&jpeg_cfg, &jpeg_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to get JPEG info");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t* outbuf_to_use = NULL;
    bool outbuf_allocated_internally = false;
    size_t actual_outbuf_size_needed = (size_t)jpeg_info.width * jpeg_info.height * 2;

    if (external_out_buffer != NULL) {
        if (actual_outbuf_size_needed > external_out_buffer_size) {
            ESP_LOGE(TAG, "❌ External buffer too small. Need: %zu, Have: %zu", 
                     actual_outbuf_size_needed, external_out_buffer_size);
            return ESP_ERR_NO_MEM;
        }
        outbuf_to_use = external_out_buffer;
        jpeg_cfg.outbuf = outbuf_to_use;
        jpeg_cfg.outbuf_size = actual_outbuf_size_needed;
    } else {
        outbuf_to_use = heap_caps_malloc(actual_outbuf_size_needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!outbuf_to_use) {
            ESP_LOGE(TAG, "❌ Failed to allocate output buffer");
            return ESP_ERR_NO_MEM;
        }
        outbuf_allocated_internally = true;
        jpeg_cfg.outbuf = outbuf_to_use;
        jpeg_cfg.outbuf_size = actual_outbuf_size_needed;
    }

    ret = esp_jpeg_decode(&jpeg_cfg, &jpeg_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ JPEG decode failed");
        if (outbuf_allocated_internally) {
            free(outbuf_to_use);
        }
        return ESP_FAIL;
    }

    ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, jpeg_info.width, jpeg_info.height, outbuf_to_use);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to display image");
    }

    if (outbuf_allocated_internally) {
        free(outbuf_to_use);
    }
    return ret;
}

// Initialize SPIFFS
esp_err_t init_spiffs(void) {
    ESP_LOGI(TAG, "📁 Initializing SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "📊 SPIFFS: total: %zu, used: %zu", total, used);
    }
    
    return ESP_OK;
}

// Load and display raw RGB565 image
esp_err_t load_and_display_raw_image(const char* filename) {
    ESP_LOGI(TAG, "🖼️  Loading raw RGB565 image: %s", filename);
    
    // Check if file exists
    struct stat st;
    if (stat(filename, &st) != 0) {
        ESP_LOGE(TAG, "❌ File not found: %s", filename);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "📄 File size: %lld bytes", (long long)st.st_size);
    
    // Calculate total pixels from file size (RGB565: 2 bytes per pixel)
    size_t total_pixels = st.st_size / 2;
    ESP_LOGI(TAG, "📐 Image contains %zu pixels", total_pixels);
    
    // Allocate buffer for image data
    uint16_t* image_data = malloc(st.st_size);
    if (image_data == NULL) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for image data");
        return ESP_ERR_NO_MEM;
    }
    
    // Read file
    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "❌ Failed to open file");
        free(image_data);
        return ESP_ERR_NOT_FOUND;
    }
    
    size_t bytes_read = fread(image_data, 1, st.st_size, f);
    fclose(f);
    
    if (bytes_read != st.st_size) {
        ESP_LOGE(TAG, "❌ Failed to read complete file");
        free(image_data);
        return ESP_ERR_INVALID_SIZE;
    }
    
    ESP_LOGI(TAG, "✅ Image loaded successfully, displaying...");
    
    // Display the image with correct BGR endian (no color swapping needed!)
    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, image_data);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🎉 Image displayed successfully with correct colors!");
    } else {
        ESP_LOGE(TAG, "❌ Failed to display image");
    }
    
    // Cleanup
    free(image_data);
    
    return ret;
}

// Create a simple test pattern
void create_test_pattern(void) {
    ESP_LOGI(TAG, "🎨 Creating test pattern...");
    
    uint16_t* pattern = malloc(LCD_H_RES * LCD_V_RES * 2);
    if (pattern == NULL) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for test pattern");
        return;
    }
    
    // Create a colorful test pattern
    for (int y = 0; y < LCD_V_RES; y++) {
        for (int x = 0; x < LCD_H_RES; x++) {
            int index = y * LCD_H_RES + x;
            
            // Create a rainbow pattern
            uint16_t color;
            if (y < LCD_V_RES / 6) {
                color = 0xF800; // Red
            } else if (y < LCD_V_RES * 2 / 6) {
                color = 0xFFE0; // Yellow
            } else if (y < LCD_V_RES * 3 / 6) {
                color = 0x07E0; // Green
            } else if (y < LCD_V_RES * 4 / 6) {
                color = 0x07FF; // Cyan
            } else if (y < LCD_V_RES * 5 / 6) {
                color = 0x001F; // Blue
            } else {
                color = 0xF81F; // Magenta
            }
            
            pattern[index] = color;
        }
    }
    
    // Display the pattern
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, pattern);
    
    free(pattern);
    ESP_LOGI(TAG, "✅ Test pattern displayed!");
}

// Main image display function
void image_display_main(void) {
    ESP_LOGI(TAG, "🚀 Starting T4 Image Display Demo!");
    
    // Initialize SPIFFS (LCD is already initialized by main.c)
    ESP_ERROR_CHECK(init_spiffs());
    
    // Create a test pattern first
    create_test_pattern();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Try to load and display your image
    esp_err_t ret = load_and_display_raw_image("/spiffs/images/image.rgb565");
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🎉 Image displayed successfully with correct colors!");
        ESP_LOGI(TAG, "✅ BGR endian fix worked! No more color swapping needed!");
    } else {
        ESP_LOGI(TAG, "💡 To display your JPEG image:");
        ESP_LOGI(TAG, "📋 Steps:");
        ESP_LOGI(TAG, "   1. Convert your JPEG to RGB565 raw format:");
        ESP_LOGI(TAG, "      ffmpeg -i image.jpeg -f rawvideo -pix_fmt rgb565le image.rgb565");
        ESP_LOGI(TAG, "   2. Copy image.rgb565 to data/images/");
        ESP_LOGI(TAG, "   3. Rebuild and flash");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "🔧 Alternative: Use online converters:");
        ESP_LOGI(TAG, "   - Convert JPEG → RGB565 raw binary");
        ESP_LOGI(TAG, "   - Resize to 240x320 or smaller");
        ESP_LOGI(TAG, "   - Save as .rgb565 file");
    }
    
    ESP_LOGI(TAG, "✅ Image display demo complete!");
}

// Define buffer sizes for manifest processing
#define MAX_FILENAME_LEN 256 
#define MANIFEST_LINE_BUFFER_SIZE (MAX_FILENAME_LEN + 64)
#define MAX_PATH_LEN (MAX_FILENAME_LEN + 16) // Enough space for "/spiffs/" prefix and some extra
#define JPEG_WORK_BUFFER_SIZE_ALLOC 4096 

esp_err_t play_jpeg_sequence_from_manifest(const char* manifest_path, uint32_t frame_delay_ms) {
    ESP_LOGI(TAG, "🎬 Playing JPEG sequence from manifest: %s (PSRAM preloading)", manifest_path);
    esp_err_t overall_ret = ESP_OK;

    // If frames aren't loaded yet, load them
    if (!g_frames_loaded) {
        int num_frames = 0;
        size_t total_jpeg_data_size = 0;

        // Phase 1: Scan manifest for frame count and total size
        ESP_LOGI(TAG, "🔍 Scanning manifest...");
        FILE* f = fopen(manifest_path, "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "❌ Failed to open manifest file: %s", manifest_path);
            return ESP_ERR_NOT_FOUND;
        }

        char line_buffer[MANIFEST_LINE_BUFFER_SIZE];
        char image_path[MAX_PATH_LEN]; 
        int line_count = 0;

        while (fgets(line_buffer, sizeof(line_buffer), f) != NULL) {
            // Yield every 10 lines to prevent watchdog timeout
            if (++line_count % 10 == 0) {
                vTaskDelay(1);
            }

            line_buffer[strcspn(line_buffer, "\r\n")] = 0;
            if (strlen(line_buffer) == 0) continue;

            if (strlen(line_buffer) > MAX_FILENAME_LEN - 1) {
                ESP_LOGW(TAG, "⚠️ Filename too long, skipping: %s", line_buffer);
                continue;
            }

            int written = snprintf(image_path, sizeof(image_path), "/spiffs/output/%s", line_buffer);
            if (written < 0 || written >= sizeof(image_path)) {
                ESP_LOGW(TAG, "⚠️ Path truncation, skipping: %s", line_buffer);
                continue;
            }

            struct stat st;
            if (stat(image_path, &st) == 0 && st.st_size > 0) {
                total_jpeg_data_size += st.st_size;
                num_frames++;
            }
        }
        fclose(f);

        if (num_frames == 0) {
            ESP_LOGE(TAG, "❌ No valid frames found in manifest");
            return ESP_ERR_NOT_FOUND;
        }

        // Phase 2: Allocate buffers if not already allocated
        ESP_LOGI(TAG, "🧠 Allocating buffers for %d frames (%zu bytes)...", num_frames, total_jpeg_data_size);
        
        g_preloaded_frames = (preloaded_jpeg_frame_t*)malloc(num_frames * sizeof(preloaded_jpeg_frame_t));
        if (!g_preloaded_frames) {
            ESP_LOGE(TAG, "❌ Failed to allocate frame info array");
            return ESP_ERR_NO_MEM;
        }

        g_all_jpeg_data_psram = heap_caps_malloc(total_jpeg_data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g_all_jpeg_data_psram) {
            ESP_LOGE(TAG, "❌ Failed to allocate PSRAM for JPEG data");
            free(g_preloaded_frames);
            g_preloaded_frames = NULL;
            return ESP_ERR_NO_MEM;
        }

        size_t common_out_buf_size = LOGICAL_DISPLAY_WIDTH * LOGICAL_DISPLAY_HEIGHT * 2;
        g_common_out_buf = heap_caps_malloc(common_out_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g_common_out_buf) {
            ESP_LOGE(TAG, "❌ Failed to allocate output buffer");
            free(g_preloaded_frames);
            heap_caps_free(g_all_jpeg_data_psram);
            g_preloaded_frames = NULL;
            g_all_jpeg_data_psram = NULL;
            return ESP_ERR_NO_MEM;
        }

        g_common_work_buf = malloc(JPEG_WORK_BUFFER_SIZE_ALLOC);
        if (!g_common_work_buf) {
            ESP_LOGE(TAG, "❌ Failed to allocate work buffer");
            free(g_preloaded_frames);
            heap_caps_free(g_all_jpeg_data_psram);
            heap_caps_free(g_common_out_buf);
            g_preloaded_frames = NULL;
            g_all_jpeg_data_psram = NULL;
            g_common_out_buf = NULL;
            return ESP_ERR_NO_MEM;
        }

        // Phase 3: Load JPEGs into PSRAM
        ESP_LOGI(TAG, "⏳ Loading JPEGs into PSRAM...");
        f = fopen(manifest_path, "r");
        if (!f) {
            ESP_LOGE(TAG, "❌ Failed to reopen manifest");
            goto cleanup;
        }

        uint8_t* current_psram_pos = g_all_jpeg_data_psram;
        int loaded_frames = 0;
        line_count = 0;

        while (fgets(line_buffer, sizeof(line_buffer), f) != NULL && loaded_frames < num_frames) {
            // Yield every 5 frames to prevent watchdog timeout
            if (++line_count % 5 == 0) {
                vTaskDelay(1);
            }

            line_buffer[strcspn(line_buffer, "\r\n")] = 0;
            if (strlen(line_buffer) == 0) continue;

            if (strlen(line_buffer) > MAX_FILENAME_LEN - 1) {
                ESP_LOGW(TAG, "⚠️ Filename too long, skipping: %s", line_buffer);
                continue;
            }

            int written = snprintf(image_path, sizeof(image_path), "/spiffs/output/%s", line_buffer);
            if (written < 0 || written >= sizeof(image_path)) {
                ESP_LOGW(TAG, "⚠️ Path truncation, skipping: %s", line_buffer);
                continue;
            }
            
            FILE* img_f = fopen(image_path, "rb");
            if (!img_f) continue;

            struct stat st;
            if (stat(image_path, &st) != 0 || st.st_size == 0) {
                fclose(img_f);
                continue;
            }

            size_t bytes_read = fread(current_psram_pos, 1, st.st_size, img_f);
            fclose(img_f);

            if (bytes_read == st.st_size) {
                g_preloaded_frames[loaded_frames].data = current_psram_pos;
                g_preloaded_frames[loaded_frames].size = bytes_read;
                current_psram_pos += bytes_read;
                loaded_frames++;
                
                // Log progress every 20 frames
                if (loaded_frames % 20 == 0) {
                    ESP_LOGI(TAG, "📥 Loaded %d/%d frames...", loaded_frames, num_frames);
                    vTaskDelay(1);
                }
            }
        }
        fclose(f);

        if (loaded_frames == 0) {
            ESP_LOGE(TAG, "❌ Failed to load any frames");
            overall_ret = ESP_ERR_NOT_FOUND;
            goto cleanup;
        }

        g_num_loaded_frames = loaded_frames;
        g_frames_loaded = true;
        ESP_LOGI(TAG, "✅ Successfully loaded %d frames into PSRAM", loaded_frames);
    }

    // Phase 4: Play sequence from PSRAM
    ESP_LOGI(TAG, "▶️ Playing %d frames...", g_num_loaded_frames);
    for (int i = 0; i < g_num_loaded_frames; i++) {
        esp_err_t ret = decode_and_display_jpeg(
            g_preloaded_frames[i].data,
            g_preloaded_frames[i].size,
            g_common_out_buf,
            LOGICAL_DISPLAY_WIDTH * LOGICAL_DISPLAY_HEIGHT * 2,
            g_common_work_buf,
            JPEG_WORK_BUFFER_SIZE_ALLOC
        );

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "⚠️ Frame %d display failed: %s", i, esp_err_to_name(ret));
            if (overall_ret == ESP_OK) overall_ret = ret;
        }

        vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
    }

    return overall_ret;

cleanup:
    if (g_preloaded_frames) {
        free(g_preloaded_frames);
        g_preloaded_frames = NULL;
    }
    if (g_all_jpeg_data_psram) {
        heap_caps_free(g_all_jpeg_data_psram);
        g_all_jpeg_data_psram = NULL;
    }
    if (g_common_out_buf) {
        heap_caps_free(g_common_out_buf);
        g_common_out_buf = NULL;
    }
    if (g_common_work_buf) {
        free(g_common_work_buf);
        g_common_work_buf = NULL;
    }
    g_frames_loaded = false;
    g_num_loaded_frames = 0;
    return overall_ret;
} 