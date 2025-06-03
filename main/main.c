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
    ESP_LOGI(TAG, "🚀 Starting T4 Display Demo");
    
    // Initialize SPIFFS
    init_spiffs();
    
    // Initialize LCD
    ESP_LOGI(TAG, "📺 Initializing LCD");
    
    // Initialize SPI bus
    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_NUM_SCLK,
        .mosi_io_num = LCD_PIN_NUM_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * LCD_BIT_PER_PIXEL / 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    
    // LCD panel IO configuration
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_NUM_CS,
        .dc_gpio_num = LCD_PIN_NUM_DC,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));
    
    // LCD panel configuration
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_NUM_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
    
    // Initialize LCD panel
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, false);
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, false, true);
    esp_lcd_panel_disp_on_off(panel_handle, true);
    
    // Turn on the LCD backlight
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_PIN_NUM_BCKL
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(LCD_PIN_NUM_BCKL, 1);
    
    ESP_LOGI(TAG, "✅ LCD initialized successfully");
    
    // Try to display a JPEG image
    const char* jpeg_file = "/spiffs/test.jpg";
    ESP_LOGI(TAG, "🖼️  Attempting to display JPEG: %s", jpeg_file);
    
    esp_err_t ret = decode_and_display_jpeg(jpeg_file);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️  Failed to display JPEG, falling back to raw image");
        // Fall back to raw image if JPEG fails
        load_and_display_raw_image("/spiffs/test.rgb565");
    }
    
    // Keep the image displayed
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}