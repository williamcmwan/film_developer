# Film Developer Controller

An ESP32-based film developer controller with LVGL touch interface for automated film development processes.

## Features

- **LVGL Graphics**: Modern UI framework with smooth rendering and animations
- **Touch Interface**: CST816 capacitive touch controller with 320x240 display
- **Visual Feedback**: Professional button styling with press states
- **Real-time Timer**: Large 48pt font MM:SS format timer displays elapsed time
- **Motor Control**: Automated clockwise and counter-clockwise rotation sequences
- **Responsive Stop**: Stop button responds within 50ms during operation
- **Serial Logging**: Comprehensive logging for debugging and monitoring

## Hardware Requirements

- ESP32 development board
- ST7789 240x320 LCD display
- CST816 touch controller
- Motor driver circuit
- DC motor

## Pin Configuration

### Display (ST7789)
- SCLK: GPIO 39
- MOSI: GPIO 38
- MISO: GPIO 40
- DC: GPIO 42
- CS: GPIO 45
- Backlight: GPIO 1

### Touch Controller (CST816)
- SDA: GPIO 48
- SCL: GPIO 47

### Motor Control
- Motor Pin 1: GPIO 12
- Motor Pin 2: GPIO 13

## Installation

1. Install the Arduino IDE or PlatformIO
2. Install required libraries:
   - **lvgl** (v8.3.0 or later) - Install via Library Manager
   - **Arduino_GFX_Library** - Install via Library Manager
   - **Wire** (built-in)
   - **bsp_cst816** (custom touch library)
3. Configure LVGL:
   - Copy `lv_conf_template.h` from the lvgl library folder to your Arduino libraries folder
   - Rename it to `lv_conf.h`
   - Set `#define LV_CONF_SKIP 0` to `1` at the top
   - Adjust memory settings if needed (default should work)
4. Clone this repository
5. Open `film_developer.ino` in Arduino IDE
6. Select your ESP32 board
7. Upload to your device

## Usage

1. Power on the device
2. The display shows START and STOP buttons at the bottom
3. Press **START** to begin the development sequence:
   - Timer starts counting in MM:SS format
   - Motor runs clockwise for 10 seconds
   - Motor runs counter-clockwise for 10 seconds
4. Press **STOP** at any time to halt the sequence and timer

## Display Layout

```
┌─────────────────────────────┐
│                             │
│                             │
│         MM:SS               │  ← Timer (center)
│                             │
│                             │
├─────────────┬───────────────┤
│   START     │     STOP      │  ← Buttons (bottom)
└─────────────┴───────────────┘
```

## Serial Monitor Output

Connect at 115200 baud to see:
- Touch events with coordinates
- Button press notifications
- Motor state changes
- Timer information

## Customization

### Adjust Motor Timing
Modify the loop counters in `runMotorSequence()`:
```cpp
for (int i = 0; i < 100; i++) {  // 100 * 100ms = 10 seconds
```

### Change Button Colors
Edit the LVGL button styles in `create_ui()`:
```cpp
lv_obj_set_style_bg_color(startBtn, lv_color_make(0, 200, 0), 0);  // Green
lv_obj_set_style_bg_color(stopBtn, lv_color_make(200, 0, 0), 0);   // Red
```

### Modify Display Rotation
Change `LCD_ROTATION` value (0-3) for different orientations.

### Customize Timer Font
Change the timer font size in `create_ui()`:
```cpp
lv_obj_set_style_text_font(timerLabel, &lv_font_montserrat_48, 0);
```

## License

MIT License - feel free to modify and use for your projects.

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.
