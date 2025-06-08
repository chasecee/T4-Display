#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "image_display.h"
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include "esp_heap_caps.h"

static const char *TAG = "T4_DISPLAY";

// LCD pins
#define LCD_PIN_NUM_BCKL    4
#define LCD_PIN_NUM_CS      27
#define LCD_PIN_NUM_DC      32
#define LCD_PIN_NUM_RST     5
#define LCD_PIN_NUM_SCLK    18
#define LCD_PIN_NUM_MOSI    23

// LCD parameters
#define LCD_H_RES           320
#define LCD_V_RES           240
#define LCD_BIT_PER_PIXEL   16

// LCD panel handle
esp_lcd_panel_handle_t panel_handle = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Starting T4 Display Sequence Player");
    
    // Initialize SPIFFS
    esp_err_t spiffs_ret = init_spiffs(); // Ensure this is declared in image_display.h and defined in image_display.c
    if (spiffs_ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS Initialization failed. Halting.");
        return; // Stop if SPIFFS fails, as we need it for the manifest and images
    }
    
    // Initialize LCD
    ESP_LOGI(TAG, "📺 Initializing LCD");
    
    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_NUM_SCLK,
        .mosi_io_num = LCD_PIN_NUM_MOSI,
        .miso_io_num = -1, // MISO not used by typical LCDs
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * LCD_BIT_PER_PIXEL / 8, // Buffer for 80 lines
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_NUM_CS,
        .dc_gpio_num = LCD_PIN_NUM_DC,
        .pclk_hz = 40 * 1000 * 1000, // 40MHz - stable speed for smooth display
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));
    
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_NUM_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR, // BGR for common ILI9341
        .bits_per_pixel = LCD_BIT_PER_PIXEL, // 16 for RGB565
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
    
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, false);
    esp_lcd_panel_swap_xy(panel_handle, true);     // Swap X and Y axis (for landscape)
    esp_lcd_panel_mirror(panel_handle, true, true); // Mirror Y an X axis, now X is true for horizontal flip
    esp_lcd_panel_disp_on_off(panel_handle, true);
    
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_PIN_NUM_BCKL
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(LCD_PIN_NUM_BCKL, 1); // Backlight on
    
    ESP_LOGI(TAG, "✅ LCD initialized successfully");

    // --- Display test.jpg first ---
    ESP_LOGI(TAG, "🖼️  Displaying test.jpg");
    
    // Allocate buffers in PSRAM for JPEG decoding
    size_t out_buf_size = LCD_H_RES * LCD_V_RES * 2; // 16-bit per pixel
    size_t work_buf_size = 65472; // Required work buffer size for JD_FASTDECODE=2 (table-based fast decode)
    
    uint8_t* out_buf = heap_caps_malloc(out_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t* work_buf = heap_caps_malloc(work_buf_size, MALLOC_CAP_8BIT); // Will fallback to PSRAM if needed
    
    if (!out_buf || !work_buf) {
        ESP_LOGE(TAG, "❌ Failed to allocate buffers for JPEG decoding");
        goto cleanup_test_jpg;
    }
    
    // Read the JPEG file into memory
    FILE* f = fopen("/spiffs/test.jpg", "rb");
    if (!f) {
        ESP_LOGE(TAG, "❌ Failed to open test.jpg");
        goto cleanup_test_jpg;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Allocate buffer for JPEG file
    uint8_t* jpeg_data = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg_data) {
        ESP_LOGE(TAG, "❌ Failed to allocate JPEG buffer");
        fclose(f);
        goto cleanup_test_jpg;
    }
    
    // Read JPEG file
    size_t bytes_read = fread(jpeg_data, 1, file_size, f);
    fclose(f);
    
    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "❌ Failed to read complete JPEG file");
        heap_caps_free(jpeg_data);
        goto cleanup_test_jpg;
    }
    
    // Now decode and display with proper arguments
    esp_err_t test_img_ret = decode_and_display_jpeg(
        jpeg_data,      // JPEG data buffer
        file_size,      // JPEG data size
        out_buf,        // Output buffer
        out_buf_size,   // Output buffer size
        work_buf,       // Work buffer
        work_buf_size   // Work buffer size
    );
    
    // Free JPEG data buffer
    heap_caps_free(jpeg_data);
    
    if (test_img_ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ test.jpg displayed successfully. Will stay as loading screen while frames load...");
    } else {
        ESP_LOGE(TAG, "❌ Failed to display test.jpg. Error: %s", esp_err_to_name(test_img_ret));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

cleanup_test_jpg:
    if (out_buf) heap_caps_free(out_buf);
    if (work_buf) heap_caps_free(work_buf);

    // --- Play sequence from manifest (test.jpg stays visible during loading) --- 
    const char* manifest_file = "/spiffs/output/manifest.txt";
    uint32_t frame_delay_ms = 80; // Increased speed: 80ms = ~12.5 FPS (was 120ms = ~8.3 FPS)

    ESP_LOGI(TAG, "🎬 Attempting to play sequence from: %s at %" PRIu32 " ms per frame", manifest_file, frame_delay_ms);
    
    // Loop to continuously play the sequence
    while (1) {
        esp_err_t play_ret = play_jpeg_sequence_from_manifest(manifest_file, frame_delay_ms);
        if (play_ret == ESP_OK) {
            ESP_LOGI(TAG, "🎉 Sequence finished. Replaying...");
        } else {
            ESP_LOGE(TAG, "⚠️ Error playing sequence. Will retry after a delay.");
            vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds before retrying if an error occurred
        }
        // Add a small delay before replaying even on success, if desired
        // vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}