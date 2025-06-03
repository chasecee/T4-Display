#pragma once

#include "esp_err.h"
#include <stddef.h> // For size_t

// Structure to hold information about a preloaded JPEG frame
typedef struct {
    uint8_t* data; // Pointer to JPEG data in PSRAM
    size_t size;   // Size of the JPEG data
} preloaded_jpeg_frame_t;

// Initialize SPIFFS filesystem
esp_err_t init_spiffs(void);

// Load and display a raw RGB565 image
esp_err_t load_and_display_raw_image(const char* filename);

// Decode and display a JPEG image from a data buffer
esp_err_t decode_and_display_jpeg(const uint8_t* jpeg_data, size_t jpeg_data_size, uint8_t* external_out_buffer, size_t external_out_buffer_size, uint8_t* external_work_buffer, size_t external_work_buffer_size);

// Play a sequence of JPEGs listed in a manifest file
esp_err_t play_jpeg_sequence_from_manifest(const char* manifest_path, uint32_t frame_delay_ms); 