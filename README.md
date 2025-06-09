# T4-Display Project

ESP-IDF project for LilyGO T4 V1.3 with ILI9341 2.4" display. Fast, efficient, and now with extra pizzazz! 💫

## 🚀 Latest Updates & Features

- **Loading Indicator**: Smooth blue gradient animation while frames load
- **PSRAM Optimization**: Efficient memory usage with PSRAM preloading
- **Smooth Playback**: Optimized frame display with minimal tearing
- **Fast Startup**: Quick initialization and display readiness
- **Memory Efficient**: Smart PSRAM allocation for large sequences

## 🎯 Performance Metrics

- **Startup Time**: ~500ms to first frame
- **Frame Loading**: Background loading with visual indicator
- **Playback FPS**: Up to 30fps for optimized sequences
- **Memory Usage**: Efficient PSRAM utilization (8MB available)

## 🖼️ Image Display Features

### Supported Formats

- **RGB565 raw binary** (native format)
- **JPEG** (via optimized conversion to RGB565)
- **Animation Sequences** (from manifest file)

### Display Capabilities

- Full screen images (240x320)
- Loading animations
- Smooth frame transitions
- Memory-efficient streaming
- PSRAM-optimized buffering

## 💅 SLAY THAT FPS - Performance Tips

1. **Convert Those JPEGs, Bestie!**

   - RGB565 raw format is YOUR FRIEND
   - JPEGs are so last season (they need decoding, ew!)
   - Use the ffmpeg command below and thank me later 💁‍♀️

2. **Size Queen Energy**

   - 240x320 EXACTLY - no compromises!
   - Runtime resizing? I don't know her 🙅‍♀️
   - Keep those dimensions consistent across your sequence

3. **Memory is Everything**

   - PSRAM is your best friend (8MB of fabulousness)
   - Group similar frames together like they're besties
   - Clean up after yourself, nobody likes a messy heap 💅

4. **Timing is Life**
   - SPI @ 40MHz is serving lewks
   - DMA transfers are your runway to success
   - Frame delays? Make them werk! 💃

## 🛠️ Quick Start

### The ONLY Way to Slay (RGB565 Conversion)

```bash
# Convert that basic JPEG to fabulous RGB565
ffmpeg -i your_image.jpeg -vf scale=240:320 -f rawvideo -pix_fmt rgb565le image.rgb565

# Put it where it belongs
cp image.rgb565 data/images/
```

### 2. Display Your Images

#### Option A: Convert JPEG to RGB565 (Best Performance)

```bash
# Convert JPEG to RGB565 raw format
ffmpeg -i your_image.jpeg -vf scale=240:320 -f rawvideo -pix_fmt rgb565le image.rgb565

# Copy to project
cp image.rgb565 data/images/
```

#### Option B: Use JPEG Directly (More Convenient)

1. Copy your JPEGs to `data/output/`
2. Create a manifest file listing your images
3. Images will load with a smooth loading animation!

## 🔧 Hardware Setup

### Pin Configuration (T4 V1.3)

```c
#define PIN_NUM_MISO      12
#define PIN_NUM_MOSI      23
#define PIN_NUM_CLK       18
#define PIN_NUM_CS        27
#define PIN_NUM_DC        32
#define PIN_NUM_RST       5
#define PIN_NUM_BCKL      4
```

### Display Specs

- **Resolution**: 240x320 pixels
- **Color depth**: 16-bit (RGB565)
- **Interface**: SPI @ 40MHz
- **Driver**: ILI9341
- **Memory**: 8MB PSRAM available

## 🎨 Graphics Features

- **Fast Display**: Direct SPI DMA transfers
- **Smooth Loading**: Visual feedback during operations
- **Memory Smart**: Efficient PSRAM usage
- **Clean Exit**: Proper resource cleanup

## 🐛 Troubleshooting

### Common Issues

1. **Slow Frame Rate**

   - Check image dimensions
   - Verify PSRAM is enabled
   - Monitor memory usage
   - Consider RGB565 conversion

2. **Display Glitches**

   - Verify SPI connections
   - Check power supply
   - Validate image format
   - Monitor memory allocation

3. **Memory Issues**
   - Use PSRAM for large sequences
   - Monitor heap fragmentation
   - Clean up unused resources
   - Check allocation patterns

## 📚 API Overview

```c
// Initialize display and filesystem
esp_err_t init_display(void);

// Load and display RGB565 image
esp_err_t load_and_display_raw_image(const char* filename);

// Play sequence of JPEGs with loading animation
esp_err_t play_jpeg_sequence_from_manifest(const char* manifest_path, uint32_t frame_delay_ms);
```

## 🎉 Contributing

Found a bug? Want to add a feature? PRs welcome! Just keep it sassy and clean! 💅

## 🗂️ Component Optimization

Tired of your ESP32 carrying more baggage than necessary? Use our minimal configuration:

### Option 1: Minimal Build (Recommended)

```bash
# Use the minimal config (saves ~40-50% flash space!)
cp sdkconfig.minimal sdkconfig
idf.py build
```

### ⚠️ **IMPORTANT: Protecting Your Minimal Config**

Your `sdkconfig` can get overwritten! Here's how to keep it safe:

```bash
# 1. Always keep sdkconfig.minimal as your backup
cp sdkconfig.minimal sdkconfig.backup

# 2. Before any ESP-IDF operations that might change config:
cp sdkconfig.minimal sdkconfig

# 3. After menuconfig or updates, restore if needed:
cp sdkconfig.minimal sdkconfig

# 4. Check if your config got messed with:
diff sdkconfig.minimal sdkconfig
```

**When sdkconfig gets overwritten:**

- ❌ Running `idf.py menuconfig` and saving
- ❌ ESP-IDF framework updates
- ❌ Running `idf.py reconfigure`
- ❌ Changing target chips
- ❌ Build system "fixes" conflicting options

**Pro tip:** Add `sdkconfig.minimal` to your version control, NOT `sdkconfig`!

### Option 2: Manual Component Audit

```bash
# Check what's actually being compiled
idf.py build | grep "Compiling"
```

**What the minimal config removes:**

- ❌ WiFi stack (saves ~300KB)
- ❌ Bluetooth (saves ~200KB)
- ❌ Ethernet drivers
- ❌ mbedTLS crypto libraries (saves ~150KB)
- ❌ HTTP/MQTT clients
- ❌ Touch sensor drivers
- ❌ I2S audio drivers
- ❌ USB support
- ❌ Unnecessary ADC calibration

**What it keeps:**

- ✅ Display drivers (ILI9341)
- ✅ SPIFFS file system
- ✅ JPEG decoder
- ✅ Rotary encoder (PCNT)
- ✅ SPI/GPIO drivers
- ✅ PSRAM support
- ✅ Performance optimizations

**Result:** Binary size drops from ~640KB to ~300-400KB! 💅

## 📝 License

MIT License - Go wild, make something awesome! ✨
