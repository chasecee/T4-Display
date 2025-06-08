Project context
LilyGo T4 (ESP32-WROVER-B, ILI9341 320 × 240)
JPEG frames decoded to RGB565 and shown full-screen.
Upscaling work
Started with 2× nearest-neighbour, added/removed ordered-dither, removed bilinear path.
Final design: UPSCALE_MODE
0 = none
1 = nearest-neighbour, runtime-selected 2× or 3×.
Code changes
image_display.h now only lists modes 0 & 1.
image_display.c keeps fast nn_scale_2x_rgb565, adds nn_scale_3x_rgb565.
Decode path auto-detects if source is ½-res (160 × 120) → 2× scale or ⅓-res (106 × 80) → 3× scale, centres result.
All bilinear/dither code deleted; simpler, smaller binary.
Converter guidance
For 2× path: convert frames to 160 × 120.
For 3× path: convert frames to 106 × 80 (command shown).
sdkconfig audit
Flash size should match hardware (likely 16 MB).
PSRAM at 80 MHz suggested; duplicate config lines pruned.
Outcome
Plain nearest-neighbour scaling (2× or 3×) with correct colours, no halos.
Memory-safe allocations, zero leaks on looped playback.

DISPLAY & SCALING
Simplified to nearest-neighbour only.
2× scaler for 160 × 120
3× scaler for 106 × 80 (318 × 240 with 1-px margins)
Bilinear/dither code removed.
Runtime auto-detects scale factor and centres the image.
ENCODER (KY-040 on 5-pin JST)
Wires:
3 V3 → +V GND → GND GPIO22(SCL) → CLK GPIO21(SDA) → DT
Added encoder.c/h using PCNT unit-0 (two-channel quadrature).
Internal pull-ups enabled.
Glitch filter disabled while debugging.
Logs ENC: delta= on every detent.
main.c:
Global g_frame_delay_ms (30-100 ms).
encoder_init() at startup.
Delay adjusted ±5 ms per detent; log prints new value.
image_display.c:
Inside the per-frame loop we also poll the encoder so delay changes take effect immediately during playback.
BUILD
encoder.c added to CMakeLists.txt; esp_driver_pcnt dependency declared.
Format-string warning fixed (frame_delay_ms now uint32_t).
Legacy PCNT warning noted—harmless.
DEBUG
Added periodic RAW: A=x B=y log to verify signal levels.
Confirmed no transitions → hardware wiring issue; jumper-to-ground test suggested.
PERFORMANCE
Encoder polling costs negligible CPU; FPS unaffected.
Manifest scan/loading dominates startup (~9 s). Options discussed to speed this (less throttling, single-pass realloc, or streaming).
