idf_component_register(SRCS "main.c" "image_display.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd espressif__esp_lcd_ili9341 fatfs spiffs driver esp_jpeg esp_timer) 