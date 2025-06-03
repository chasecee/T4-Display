#pragma once

#include "esp_err.h"

// Initialize SPIFFS filesystem
esp_err_t init_spiffs(void);

// Load and display a raw RGB565 image
esp_err_t load_and_display_raw_image(const char* filename);

// Decode and display a JPEG image
esp_err_t decode_and_display_jpeg(const char* filename); 