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
#define JPEG_DECODE_BUFFER_SIZE (LCD_H_RES * 16)  // Buffer for 16 lines at a time
#define JPEG_DECODE_TIMEOUT_MS 1000

// Function to decode and display JPEG image
esp_err_t decode_and_display_jpeg(const char* filename) {
    ESP_LOGI(TAG, "🖼️  Loading JPEG image: %s", filename);
    
    // Check if file exists
    struct stat st;
    if (stat(filename, &st) != 0) {
        ESP_LOGE(TAG, "❌ File not found: %s", filename);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "📄 File size: %lld bytes", (long long)st.st_size);
    // Open the JPEG file
    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "❌ Failed to open file");
        return ESP_ERR_NOT_FOUND;
    }
    // Allocate buffer for JPEG data
    uint8_t* jpeg_data = malloc(st.st_size);
    if (jpeg_data == NULL) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for JPEG data");
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    // Read JPEG file
    size_t bytes_read = fread(jpeg_data, 1, st.st_size, f);
    fclose(f);
    if (bytes_read != st.st_size) {
        ESP_LOGE(TAG, "❌ Failed to read complete file");
        free(jpeg_data);
        return ESP_ERR_INVALID_SIZE;
    }
    // Prepare JPEG decode config
    esp_jpeg_image_cfg_t jpeg_cfg = {
        .indata = jpeg_data,
        .indata_size = st.st_size,
        .outbuf = NULL, // We'll allocate after getting info
        .outbuf_size = 0,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = { .swap_color_bytes = 1 },
        .advanced = { .working_buffer = NULL, .working_buffer_size = 0 },
    };
    esp_jpeg_image_output_t jpeg_info;
    // Get image info
    esp_err_t ret = esp_jpeg_get_image_info(&jpeg_cfg, &jpeg_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to get JPEG info");
        free(jpeg_data);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "📐 JPEG dimensions: %dx%d", jpeg_info.width, jpeg_info.height);
    // Allocate output buffer
    size_t outbuf_size = jpeg_info.width * jpeg_info.height * 2; // RGB565 = 2 bytes per pixel
    uint8_t* outbuf = heap_caps_malloc(outbuf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!outbuf) {
        ESP_LOGE(TAG, "❌ Failed to allocate output buffer in PSRAM");
        free(jpeg_data);
        return ESP_ERR_NO_MEM;
    }
    jpeg_cfg.outbuf = outbuf;
    jpeg_cfg.outbuf_size = outbuf_size;
    // Decode JPEG
    ret = esp_jpeg_decode(&jpeg_cfg, &jpeg_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ JPEG decode failed");
        free(jpeg_data);
        free(outbuf);
        return ESP_FAIL;
    }
    // Display the image
    ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, jpeg_info.width, jpeg_info.height, outbuf);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ JPEG image displayed successfully!");
    } else {
        ESP_LOGE(TAG, "❌ Failed to display image");
    }
    free(jpeg_data);
    free(outbuf);
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