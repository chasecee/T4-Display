# T4-Display Project

ESP-IDF project for LilyGO T4 V1.3 with ILI9341 2.4" display.

## Hardware

- **LilyGO T4 V1.3** board with ESP32
- **2.4" ILI9341 LCD Display** (240x320)
- SPI interface communication

## Current Status

✅ **WORKING**: ILI9341 display driver with correct colors  
✅ **WORKING**: Image display system with RGB565 support  
✅ **WORKING**: SPIFFS filesystem for image storage  
🎨 **READY**: Lightweight graphics without LVGL bloat  
📸 **READY**: Image conversion pipeline for JPEG → RGB565

## Recent Updates

- **JPEG Display with PSRAM**: Successfully implemented JPEG image display using PSRAM allocation for large image buffers. This ensures smooth handling of high-resolution images without running out of memory.

## Project Structure

```
T4-Display/
├── CMakeLists.txt          # Root CMake configuration
├── sdkconfig.defaults      # ESP-IDF default configuration
├── main/
│   ├── main.c              # Main application with display tests
│   ├── image_display.c     # Image loading and display functions
│   └── CMakeLists.txt      # Main component configuration
├── data/
│   └── images/             # Put your image files here
└── managed_components/
    └── espressif__esp_lcd_ili9341/  # ILI9341 driver
```

## 🖼️ Image Display Features

### Supported Formats

- **RGB565 raw binary** (native format)
- **JPEG** (via conversion to RGB565)

### Display Capabilities

- Full screen images (240x320)
- Automatic centering for smaller images
- Memory-efficient streaming display
- Color test patterns

## 🚀 Quick Start

### 1. Build and Flash

```bash
idf.py build
idf.py flash monitor
```

### 2. Display Your JPEG Images

#### Option A: Convert with FFmpeg (Recommended)

```bash
# Convert JPEG to RGB565 raw format
ffmpeg -i your_image.jpeg -vf scale=240:320 -f rawvideo -pix_fmt rgb565le image.rgb565

# Copy to project
cp image.rgb565 data/images/
```

#### Option B: Online Converters

1. Use online JPEG → RGB565 converter
2. Resize to 240x320 or smaller
3. Save as `.rgb565` binary file
4. Copy to `data/images/`

#### Option C: Python Script

```python
from PIL import Image
import struct

# Load and resize image
img = Image.open('your_image.jpeg')
img = img.resize((240, 320))
img = img.convert('RGB')

# Convert to RGB565
with open('image.rgb565', 'wb') as f:
    for y in range(img.height):
        for x in range(img.width):
            r, g, b = img.getpixel((x, y))
            # Convert to RGB565
            r5 = (r * 31) // 255
            g6 = (g * 63) // 255
            b5 = (b * 31) // 255
            rgb565 = (r5 << 11) | (g6 << 5) | b5
            f.write(struct.pack('<H', rgb565))
```

### 3. Rebuild and Flash

```bash
idf.py build flash
```

## 📱 Display Features

### What You'll See

1. **Rainbow test pattern** - Verifies display is working
2. **Color tests** - Red, Green, Blue, White, Black screens
3. **Graphics demo** - Rectangles, gradients, simple icons
4. **Your image** - If RGB565 file is found

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

## 🎨 Graphics Capabilities

- **Memory efficient**: Streams data to display
- **8MB RAM friendly**: No large framebuffers
- **Fast SPI**: 40MHz for smooth updates
- **Color accurate**: Proper RGB565 color mapping
- **No LVGL**: Lightweight, direct display control

## 🔧 Technical Details

| Function  | GPIO | Description           |
| --------- | ---- | --------------------- |
| SPI MISO  | 12   | SPI data input        |
| SPI MOSI  | 23   | SPI data output       |
| SPI CLK   | 18   | SPI clock             |
| LCD CS    | 27   | Chip select           |
| LCD DC    | 32   | Data/Command          |
| LCD RST   | 5    | Reset                 |
| Backlight | 4    | LCD backlight control |

### Display Specifications

- **Resolution**: 240x320 pixels
- **Color depth**: 16-bit (RGB565)
- **Driver**: ILI9341
- **Interface**: SPI
- **Clock speed**: 20MHz

## Build Instructions

1. **Install ESP-IDF** (tested with v5.0+)

2. **Set Target**:

   ```bash
   idf.py set-target esp32
   ```

3. **Add your JPG images**:

   ```bash
   # Copy your JPG files to data/images/
   cp ~/Pictures/*.jpg data/images/
   ```

4. **Build and Flash**:
   ```bash
   idf.py build flash monitor
   ```

## Features

### Original Features

- ✅ Direct ILI9341 driver implementation
- ✅ Basic drawing functions (fill screen, rectangles)
- ✅ RGB565 color support with correct GBR compensation
- ✅ SPI DMA transfers for performance
- ✅ Backlight control

### New GFX Library Features

- 🎨 **JPEG Image Loading**: Load and display JPG files from SPIFFS
- 🎯 **High Performance**: Optimized bitmap operations
- 📝 **True Type Fonts**: Beautiful scalable text rendering
- 🎪 **Advanced Graphics**: Polygons, gradients, transformations
- 💾 **Memory Efficient**: Smart buffering for 8MB systems

## Expected Output

After flashing with images:

1. **Original Demo**: Colors and rectangles (main.c)
2. **GFX Demo**: JPEG loading and display (gfx_demo.cpp)
3. **Performance**: Smooth graphics with DMA transfers

## Troubleshooting

### JPEG Loading Issues

- ✅ **Files Location**: Put JPG files in `data/images/` folder
- ✅ **File Size**: Keep images reasonable size for 8MB system
- ✅ **Format**: Use standard JPEG files (RGB, not CMYK)
- 💡 **TIP**: 240x320 or smaller images work best

### Build Issues

- Make sure GFX component is in `components/gfx/`
- Verify SPIFFS partition is created: check `partitions.csv`
- Ensure C++ files have `.cpp` extension for GFX code

### Memory Issues

- 📊 **SPIFFS Size**: 960KB available for images (see partitions.csv)
- 🧠 **RAM Usage**: GFX library uses much less RAM than LVGL
- 💡 **TIP**: Monitor memory usage with `esp_get_free_heap_size()`

## Technical Notes

- **Driver**: Using ESP Component Registry ILI9341 driver v2.0.0
- **Graphics**: GFX Library with JPEG support
- **Storage**: SPIFFS filesystem for images (960KB)
- **Performance**: SPI DMA + GFX optimizations
- **Memory**: Designed for 8MB systems like T4 V1.3

## Next Steps

1. 🎯 **Add your JPG images** to `data/images/`
2. 🚀 **Integrate GFX driver** with existing ILI9341 setup
3. 🎨 **Create awesome graphics** with JPEG + drawing primitives
4. 📱 **Build your app** with lightweight graphics instead of LVGL

## Quick Start with GFX + JPEG

```bash
# 1. Put your images in the right place
cp my_image.jpg data/images/

# 2. Build and flash everything (including SPIFFS)
idf.py build flash monitor

# 3. Your JPEGs will be available at /spiffs/ on the device!
```

No more LVGL bloat - just fast, efficient graphics! 🔥
