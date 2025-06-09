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
#include <dirent.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <assert.h>
#include "encoder.h"

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

extern volatile uint32_t g_frame_delay_ms;

#include <assert.h>

/*-----------------------------------------------------------------------
 * Optional 2× up-scalers controlled by UPSCALE_MODE (see image_display.h)
 *   0 – none
 *   1 – nearest-neighbour (already very fast)
 *   2 – bilinear (slightly smoother, ~2-3 ms extra at 160×120)
 *---------------------------------------------------------------------*/

#if UPSCALE_MODE == 1
// Optional ordered-dither variant: instead of making all 4 pixels in the 2×2
// block identical, sample the neighbouring source pixels in a Bayer-like
// pattern (TL = centre, TR = right, BL = below, BR = diagonal). This gives a
// subtle anti-alias look without the cost of bilinear maths and removes the
// hard block edges that regular nearest-neighbour exhibits. Enable by
// defining USE_DITHERED_NN at compile time (defaults on).
#ifndef USE_DITHERED_NN
#define USE_DITHERED_NN 0 // set to 1 if you prefer the ordered-dither variant
#endif

#if USE_DITHERED_NN
static void nn_scale_dither_2x_rgb565(const uint16_t *src, uint16_t *dst, int src_w, int src_h)
{
    assert(src_w * 2 == LOGICAL_DISPLAY_WIDTH && src_h * 2 == LOGICAL_DISPLAY_HEIGHT);

    for (int y = 0; y < src_h; y++) {
        const uint16_t *row0 = src + y * src_w;
        const uint16_t *row1 = (y + 1 < src_h) ? (row0 + src_w) : row0; // clamp last line

        uint16_t *d_row0 = dst + (y * 2) * LOGICAL_DISPLAY_WIDTH;
        uint16_t *d_row1 = d_row0 + LOGICAL_DISPLAY_WIDTH;

        for (int x = 0; x < src_w; x++) {
            uint16_t p00 = row0[x];
            uint16_t p01 = (x + 1 < src_w) ? row0[x + 1] : p00;
            uint16_t p10 = row1[x];
            uint16_t p11 = (x + 1 < src_w) ? row1[x + 1] : p10;

            int dx = x * 2;

            d_row0[dx]     = p00; // top-left: original pixel
            d_row0[dx + 1] = p01; // top-right: sample right neighbour
            d_row1[dx]     = p10; // bottom-left: sample below neighbour
            d_row1[dx + 1] = p11; // bottom-right: diagonal neighbour
        }
    }
}
#endif // USE_DITHERED_NN

static void nn_scale_2x_rgb565(const uint16_t *src, uint16_t *dst, int src_w, int src_h)
{
    assert(src_w * 2 == LOGICAL_DISPLAY_WIDTH && src_h * 2 == LOGICAL_DISPLAY_HEIGHT);
    for (int y = 0; y < src_h; y++) {
        const uint16_t *s_row = src + y * src_w;
        uint16_t *d_row0 = dst + (y * 2) * LOGICAL_DISPLAY_WIDTH;
        uint16_t *d_row1 = d_row0 + LOGICAL_DISPLAY_WIDTH;
        for (int x = 0; x < src_w; x++) {
            uint16_t pix = s_row[x];
            int d_idx = x * 2;
            d_row0[d_idx] = pix;
            d_row0[d_idx + 1] = pix;
            d_row1[d_idx] = pix;
            d_row1[d_idx + 1] = pix;
        }
    }
}

// 3× nearest-neighbour (for 106×80 → 318×240). We still use the logical
// display stride (320) so the 1-pixel margins on left/right stay black.
static void nn_scale_3x_rgb565(const uint16_t *src, uint16_t *dst, int src_w, int src_h)
{
    int dst_w = src_w * 3;
    for (int y = 0; y < src_h; y++) {
        const uint16_t *s_row = src + y * src_w;
        uint16_t *d_row0 = dst + (y * 3) * dst_w;
        uint16_t *d_row1 = d_row0 + dst_w;
        uint16_t *d_row2 = d_row1 + dst_w;

        for (int x = 0; x < src_w; x++) {
            uint16_t pix = s_row[x];
            int dx = x * 3;

            d_row0[dx] = d_row0[dx + 1] = d_row0[dx + 2] = pix;
            d_row1[dx] = d_row1[dx + 1] = d_row1[dx + 2] = pix;
            d_row2[dx] = d_row2[dx + 1] = d_row2[dx + 2] = pix;
        }
    }
}
#endif // UPSCALE_MODE switch

// Function to decode and display JPEG image from a data buffer
esp_err_t decode_and_display_jpeg(const uint8_t* jpeg_data, size_t jpeg_data_size, 
                                  uint8_t* external_out_buffer, size_t external_out_buffer_size, 
                                  uint8_t* external_work_buffer, size_t external_work_buffer_size) {
    if (jpeg_data == NULL || jpeg_data_size == 0) {
        ESP_LOGE(TAG, "❌ Invalid JPEG data pointer or size");
        return ESP_ERR_INVALID_ARG;
    }

    esp_jpeg_image_cfg_t jpeg_cfg = {
        .indata = (uint8_t*)jpeg_data,  // Cast away const to match API
        .indata_size = jpeg_data_size,
        .outbuf = NULL, 
        .outbuf_size = 0,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,  // No scaling for maximum speed
        .flags = { 
            .swap_color_bytes = 1         // BGR format for ILI9341
        },
        .advanced = { .working_buffer = NULL, .working_buffer_size = 0 },
    };

    // Always use provided work buffer for consistent performance
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

    uint8_t* outbuf_to_use = NULL;        // Buffer into which JPEG is decoded (could be small or full-size)
    bool outbuf_allocated_internally = false;
    int upscale_factor = 1; // 1 means no upscale
    if (jpeg_info.width * 2 == LOGICAL_DISPLAY_WIDTH && jpeg_info.height * 2 == LOGICAL_DISPLAY_HEIGHT) {
        upscale_factor = 2;
    } else if (jpeg_info.width * 3 <= LOGICAL_DISPLAY_WIDTH && jpeg_info.height * 3 <= LOGICAL_DISPLAY_HEIGHT) {
        upscale_factor = 3;
    }
    bool need_upscale = (upscale_factor > 1);

    size_t actual_outbuf_size_needed = (size_t)jpeg_info.width * jpeg_info.height * 2; // Size for decoded image

    if (need_upscale) {
        // Allocate a *small* temporary buffer for decode
        outbuf_to_use = heap_caps_malloc(actual_outbuf_size_needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!outbuf_to_use) {
            ESP_LOGE(TAG, "❌ Failed to allocate temp decode buffer");
            return ESP_ERR_NO_MEM;
        }
        outbuf_allocated_internally = true;
        jpeg_cfg.outbuf = outbuf_to_use;
        jpeg_cfg.outbuf_size = actual_outbuf_size_needed;
    } else {
        // Decode straight into external_out_buffer (or allocate fallback)
        if (external_out_buffer != NULL) {
            if (actual_outbuf_size_needed > external_out_buffer_size) {
                ESP_LOGE(TAG, "❌ External buffer too small. Need: %lu, Have: %lu", 
                         (unsigned long)actual_outbuf_size_needed, (unsigned long)external_out_buffer_size);
                return ESP_ERR_NO_MEM;
            }
            outbuf_to_use = external_out_buffer;
            jpeg_cfg.outbuf = outbuf_to_use;
            jpeg_cfg.outbuf_size = actual_outbuf_size_needed;
        } else {
            // Fallback full-size allocate
            outbuf_to_use = heap_caps_malloc(actual_outbuf_size_needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!outbuf_to_use) {
                ESP_LOGE(TAG, "❌ Failed to allocate output buffer");
                return ESP_ERR_NO_MEM;
            }
            outbuf_allocated_internally = true;
            jpeg_cfg.outbuf = outbuf_to_use;
            jpeg_cfg.outbuf_size = actual_outbuf_size_needed;
        }
    }

    // PERFORMANCE: Optimized decode with larger work buffers
    ret = esp_jpeg_decode(&jpeg_cfg, &jpeg_info);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ JPEG decode failed");
        if (outbuf_allocated_internally) {
            free(outbuf_to_use);
        }
        return ESP_FAIL;
    }

    // Apply optional up-scale
#if UPSCALE_MODE == 1
    if (need_upscale) {
        if (upscale_factor == 2) {
            nn_scale_2x_rgb565((uint16_t*)outbuf_to_use, (uint16_t*)external_out_buffer,
                               jpeg_info.width, jpeg_info.height);
        } else if (upscale_factor == 3) {
            nn_scale_3x_rgb565((uint16_t*)outbuf_to_use, (uint16_t*)external_out_buffer,
                               jpeg_info.width, jpeg_info.height);
        }
        // After scaling we can free temp buffer
        if (outbuf_allocated_internally && outbuf_to_use) {
            free(outbuf_to_use);
            outbuf_to_use = NULL;
            outbuf_allocated_internally = false;
        }
        // Now pretend the image is full screen for the draw call
        jpeg_info.width = jpeg_info.width * upscale_factor;
        jpeg_info.height = jpeg_info.height * upscale_factor;
        outbuf_to_use = external_out_buffer;
    }
#endif

    // PERFORMANCE: Direct bitmap transfer with display sync to prevent tearing
    // Center the image if it is smaller than the logical display size (after optional upscale)
    int x_offset = 0;
    int y_offset = 0;
    if (jpeg_info.width < LOGICAL_DISPLAY_WIDTH) {
        x_offset = (LOGICAL_DISPLAY_WIDTH - jpeg_info.width) / 2;
    }
    if (jpeg_info.height < LOGICAL_DISPLAY_HEIGHT) {
        y_offset = (LOGICAL_DISPLAY_HEIGHT - jpeg_info.height) / 2;
    }

    ret = esp_lcd_panel_draw_bitmap(panel_handle,
                                    x_offset,
                                    y_offset,
                                    x_offset + jpeg_info.width,
                                    y_offset + jpeg_info.height,
                                    outbuf_to_use);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to display image");
    }

    if (outbuf_allocated_internally && outbuf_to_use) {
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
        ESP_LOGI(TAG, "📊 SPIFFS: total: %lu, used: %lu, free: %lu (%d%% used)", 
                 (unsigned long)total, (unsigned long)used, (unsigned long)(total - used), (int)((used * 100) / total));
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
    ESP_LOGI(TAG, "📐 Image contains %lu pixels", (unsigned long)total_pixels);
    
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
#define JPEG_WORK_BUFFER_SIZE_ALLOC 65472  // Required for JD_FASTDECODE=2 (table-based fast decode)

// Performance optimization: Use internal RAM for critical buffers when possible
#define USE_INTERNAL_RAM_FOR_WORK_BUFFER 0  // Disabled because 65KB won't fit in internal RAM

esp_err_t play_jpeg_sequence_from_manifest(const char* manifest_path, uint32_t frame_delay_ms) {
    ESP_LOGI(TAG, "🎬 Playing JPEG sequence from manifest: %s (OPTIMIZED PSRAM preloading)", manifest_path);
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
            // Yield every 10 lines for system stability (watchdog disabled)
            if (++line_count % 10 == 0) {
                vTaskDelay(1);
            }

            line_buffer[strcspn(line_buffer, "\r\n")] = 0;
            if (strlen(line_buffer) == 0) continue;

            // Extract filename (first token) and optional size (second token)
            char filename_only[MAX_FILENAME_LEN];
            unsigned long file_size_hint = 0;
            int tokens = sscanf(line_buffer, "%255s %lu", filename_only, &file_size_hint);
            if (tokens < 1) {
                continue; // malformed line
            }

            if (strlen(filename_only) > MAX_FILENAME_LEN - 1) {
                ESP_LOGW(TAG, "⚠️ Filename too long, skipping: %s", filename_only);
                continue;
            }

            int written = snprintf(image_path, sizeof(image_path), "/spiffs/output/%s", filename_only);
            if (written < 0 || written >= sizeof(image_path)) {
                ESP_LOGW(TAG, "⚠️ Path truncation, skipping: %s", filename_only);
                continue;
            }

            size_t sz = 0;
            if (file_size_hint > 0) {
                sz = file_size_hint;
            } else {
                unsigned long sz_temp = 0;
                if (sscanf(line_buffer, "%*s %lu", &sz_temp) == 1 && sz_temp > 0) {
                    sz = sz_temp;
                } else {
                    // No size column; stat the file to determine size
                    struct stat st;
                    if (stat(image_path, &st) == 0 && st.st_size > 0) {
                        sz = st.st_size;
                    }
                }
            }

            if (sz > 0) {
                total_jpeg_data_size += sz;
                num_frames++;
            }
        }
        fclose(f);

        if (num_frames == 0) {
            ESP_LOGE(TAG, "❌ No valid frames found in manifest");
            return ESP_ERR_NOT_FOUND;
        }

        // Phase 2: Allocate buffers if not already allocated
        ESP_LOGI(TAG, "🧠 Allocating buffers for %d frames (%lu bytes)...", num_frames, (unsigned long)total_jpeg_data_size);
        
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

        // PERFORMANCE BOOST: Use internal RAM for work buffer if possible for faster access
#if USE_INTERNAL_RAM_FOR_WORK_BUFFER
        g_common_work_buf = heap_caps_malloc(JPEG_WORK_BUFFER_SIZE_ALLOC, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!g_common_work_buf) {
            // Fallback to regular malloc if internal RAM is full
            g_common_work_buf = malloc(JPEG_WORK_BUFFER_SIZE_ALLOC);
        }
#else
        g_common_work_buf = malloc(JPEG_WORK_BUFFER_SIZE_ALLOC);
#endif
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

        ESP_LOGI(TAG, "🚀 Work buffer allocated in %s RAM for optimal performance", 
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > JPEG_WORK_BUFFER_SIZE_ALLOC ? "INTERNAL" : "EXTERNAL");



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
            // Yield every 5 lines for system stability (watchdog disabled)
            if (++line_count % 5 == 0) {
                vTaskDelay(1);
            }

            line_buffer[strcspn(line_buffer, "\r\n")] = 0;
            if (strlen(line_buffer) == 0) continue;

            // Parse filename and optional size again
            char filename_only2[MAX_FILENAME_LEN];
            unsigned long size_hint2 = 0;
            int tokens = sscanf(line_buffer, "%255s %lu", filename_only2, &size_hint2);
            if (tokens < 1) {continue;}

            if (strlen(filename_only2) > MAX_FILENAME_LEN - 1){ESP_LOGW(TAG, "⚠️ Filename too long, skipping: %s", filename_only2); continue;}

            int written = snprintf(image_path, sizeof(image_path), "/spiffs/output/%s", filename_only2);
            if (written < 0 || written >= sizeof(image_path)) {
                ESP_LOGW(TAG, "⚠️ Path truncation, skipping: %s", filename_only2);
                continue;
            }
            
            FILE* img_f = fopen(image_path, "rb");
            if (!img_f) {
                ESP_LOGW(TAG, "⚠️ Cannot open file: %s", image_path);
                continue;
            }

            // Just use the actual file size, ignore manifest hint
            struct stat st;
            if (stat(image_path, &st) != 0) {
                fclose(img_f);
                continue;
            }
            size_t file_size = st.st_size;

            size_t bytes_read = fread(current_psram_pos, 1, file_size, img_f);
            fclose(img_f);

            if (bytes_read == file_size) {
                g_preloaded_frames[loaded_frames].data = current_psram_pos;
                g_preloaded_frames[loaded_frames].size = bytes_read;
                current_psram_pos += bytes_read;
                loaded_frames++;
            } else {
                ESP_LOGW(TAG, "⚠️ File read failed: %s - expected %lu bytes, read %lu bytes", 
                         filename_only2, (unsigned long)file_size, (unsigned long)bytes_read);
            }
                
            // Log progress every 20 frames
            if (loaded_frames % 20 == 0) {
                ESP_LOGI(TAG, "📥 Loaded %d/%d frames...", loaded_frames, num_frames);
                vTaskDelay(1);
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

    // Clear the screen to black now that frames are loaded (so loading screen stays visible during loading)
    {
        uint16_t *black_line = calloc(LOGICAL_DISPLAY_WIDTH, sizeof(uint16_t));
        if (black_line) {
            for (int y = 0; y < LOGICAL_DISPLAY_HEIGHT; y++) {
                esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LOGICAL_DISPLAY_WIDTH, y + 1, black_line);
            }
            free(black_line);
        }
    }

    // Phase 4: Play sequence from PSRAM with OPTIMIZED SPEED (anti-tearing)
    ESP_LOGI(TAG, "▶️ Playing %d frames with display sync...", g_num_loaded_frames);
    uint32_t frame_start_time;
    uint32_t decode_time, total_time;
    
    for (int i = 0; i < g_num_loaded_frames; i++) {
        frame_start_time = esp_timer_get_time() / 1000; // Convert to ms
        
        uint32_t decode_start = esp_timer_get_time() / 1000;
        esp_err_t ret = decode_and_display_jpeg(
            g_preloaded_frames[i].data,
            g_preloaded_frames[i].size,
            g_common_out_buf,
            LOGICAL_DISPLAY_WIDTH * LOGICAL_DISPLAY_HEIGHT * 2,
            g_common_work_buf,
            JPEG_WORK_BUFFER_SIZE_ALLOC
        );
        decode_time = (esp_timer_get_time() / 1000) - decode_start;

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "⚠️ Frame %d display failed: %s", i, esp_err_to_name(ret));
            if (overall_ret == ESP_OK) overall_ret = ret;
        }

        total_time = (esp_timer_get_time() / 1000) - frame_start_time;
        
        // Performance logging every 50 frames
        if (i % 50 == 0) {
            ESP_LOGI(TAG, "🏎️ Frame %d: decode=%lums, total=%lums, target=%lums", i, decode_time, total_time, g_frame_delay_ms);
        }

        // allow real-time adjustment via rotary encoder
        int step = encoder_get_delta();
        if (step) {
            int32_t new_delay = (int32_t)g_frame_delay_ms + step * 10;
            if (new_delay < 70)  new_delay = 70;   // Above decode time so delays actually matter
            if (new_delay > 150) new_delay = 150;  // Much slower for visible difference
            g_frame_delay_ms = (uint32_t)new_delay;
            ESP_LOGI("ENC","delay=%" PRIu32 " ms", g_frame_delay_ms);
        }

        uint32_t min_frame_time = g_frame_delay_ms;
        if (total_time < min_frame_time) {
            vTaskDelay(pdMS_TO_TICKS(min_frame_time - total_time));
        } else {
            // Frame took longer than target, add small sync delay to prevent tearing
            vTaskDelay(pdMS_TO_TICKS(2));
        }
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
#if USE_INTERNAL_RAM_FOR_WORK_BUFFER
        heap_caps_free(g_common_work_buf);
#else
        free(g_common_work_buf);
#endif
        g_common_work_buf = NULL;
    }
    g_frames_loaded = false;
    g_num_loaded_frames = 0;
    return overall_ret;
} 