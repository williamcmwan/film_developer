# Film Developer Controller

An ESP32-based film developer controller with LVGL touch interface for automated film development processes.

## Features

- **Multi-Screen Interface**: Splash screen, main menu, settings (2 pages), and development screen
- **4-Stage Development Process**: Developer, Stop Bath, Fixer, and Rinse stages
- **Countdown Timer**: Large 48pt font MM:SS format with adjustable time (5-second intervals)
- **Stage Selection**: Click to switch between stages with visual feedback
- **Motor Control**: Automatic motor start/stop synchronized with timer
- **Dynamic UI**: Buttons and controls adapt based on timer state
- **Persistent Settings**: All timing and speed settings saved to flash memory
- **Touch Interface**: CST816 capacitive touch controller with 320x240 display
- **LVGL Graphics**: Modern UI framework with smooth animations
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

### Main Menu
1. Power on the device - splash screen appears for 3 seconds
2. Main menu displays two options:
   - **Start Develop**: Begin the development process
   - **Settings**: Configure timing and motor speed

### Settings
Configure development parameters across two pages:

**Page 1:**
- Developer time (default: 7:00)
- Stop Bath time (default: 1:00)
- Fixer time (default: 5:00)

**Page 2:**
- Rinse time (default: 3:00)
- Reverse time (default: 0:30)
- Motor speed (default: 100%)

Use +/- buttons to adjust values in 5-second increments. Settings are automatically saved to flash memory.

### Start Develop Screen

**Stage Selection (Top Row):**
- Four stage buttons: **Dev**, **Stop**, **Fix**, **Rinse**
- Click any stage to switch (disabled during timer operation)
- Active stage highlighted in green

**Timer Display (Center):**
- Large MM:SS countdown display
- Down button (left) and Up button (right) to adjust time in 5-second intervals
- Adjustment buttons hidden during timer operation

**Control Buttons (Bottom):**

*When Stopped:*
- **← (Back)**: Return to main menu
- **Start**: Begin countdown and start motor
- **Reset**: Reset timer to default setting value

*When Running:*
- **Stop**: Full-width button to pause timer and stop motor
- All other buttons hidden for focused operation

## Screen Layouts

### Main Menu
```
┌─────────────────────────────┐
│    Film Developer           │
│                             │
│    ┌─────────────────┐      │
│    │  Start Develop  │      │
│    └─────────────────┘      │
│                             │
│    ┌─────────────────┐      │
│    │    Settings     │      │
│    └─────────────────┘      │
└─────────────────────────────┘
```

### Start Develop (Stopped)
```
┌─────────────────────────────┐
│ [Dev][Stop][Fix][Rinse]     │  ← Stage buttons
│                             │
│   ↓    MM:SS    ↑           │  ← Timer with adjust
│                             │
│                             │
│ [←][Start][Reset]           │  ← Controls
└─────────────────────────────┘
```

### Start Develop (Running)
```
┌─────────────────────────────┐
│ [Dev][Stop][Fix][Rinse]     │  ← Stage buttons (disabled)
│                             │
│        MM:SS                │  ← Timer only
│                             │
│                             │
│ [      Stop      ]          │  ← Full-width stop
└─────────────────────────────┘
```

## Serial Monitor Output

Connect at 115200 baud to see:
- Settings loaded/saved notifications
- Screen navigation events
- Stage changes
- Timer start/stop/reset events
- Motor state changes
- Touch events and button presses

## Customization

### Default Timer Values
Modify initial settings in `loadSettings()`:
```cpp
settings.devTime = preferences.getInt("devTime", 420);      // 7:00
settings.stopTime = preferences.getInt("stopTime", 60);     // 1:00
settings.fixTime = preferences.getInt("fixTime", 300);      // 5:00
settings.rinseTime = preferences.getInt("rinseTime", 180);  // 3:00
```

### Timer Adjustment Increment
Change the increment value in `timerUpHandler()` and `timerDownHandler()`:
```cpp
currentTime += 5;  // Change 5 to desired seconds
```

### Button Colors
Edit button colors in screen creation functions:
```cpp
lv_obj_set_style_bg_color(startBtn, lv_color_make(0, 150, 0), 0);  // Green
lv_obj_set_style_bg_color(stopBtn, lv_color_make(200, 100, 0), 0); // Orange
lv_obj_set_style_bg_color(resetBtn, lv_color_make(150, 0, 0), 0);  // Red
```

### Stage Button Layout
Adjust stage button size and spacing in `createDevelopScreen()`:
```cpp
int btnWidth = 75;
int btnSpacing = 5;
```

### Display Rotation
Change `LCD_ROTATION` value (0-3) for different orientations.

## License

MIT License - feel free to modify and use for your projects.

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.
