idf_component_register(SRCS "main.c" "image_display.c" "encoder.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd espressif__esp_lcd_ili9341 spiffs driver esp_driver_pcnt esp_jpeg esp_timer) 