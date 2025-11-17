# Film Developer Controller

An ESP32-based film developer controller with LVGL touch interface for automated film development processes.

## Features

- **Multi-Screen Interface**: Splash screen, main menu, settings (3 pages), and development screen
- **4-Stage Development Process**: Developer, Stop Bath, Fixer, and Rinse stages
- **Countdown Timer**: Large 48pt font MM:SS format with adjustable time (5-second intervals)
- **Stage Selection**: Click to switch between stages with visual feedback
- **PWM Motor Control**: Variable speed control (100-200% actual) with automatic start/stop synchronized with timer
- **Automatic Motor Reversal**: Motor direction reverses at configurable intervals (default: 10 seconds)
- **Overtime Mode**: Timer counts up after 00:00, motor reduces to configurable overtime speed (default: 5%), blinking red display, melodic buzzer alert, auto-advances to next stage on Stop
- **Physical Button Control**: Single button on GPIO 9 for hands-free operation (no touch screen needed)
- **Screen Rotation**: 180° rotation toggle for left/right-handed use
- **Dynamic UI**: Buttons and controls adapt based on timer state
- **Persistent Settings**: All timing, speed, and rotation settings saved to flash memory
- **Touch Interface**: CST816 capacitive touch controller with 320x240 display
- **LVGL Graphics**: Modern UI framework with smooth animations
- **Serial Logging**: Comprehensive logging for debugging and monitoring

## Hardware Requirements

- ESP32 development board
- ST7789 240x320 LCD display
- CST816 touch controller
- L298N motor driver module (or compatible H-bridge driver)
- DC motor (6-12V recommended)
- Passive buzzer (3-pin)

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

### Motor Control (L298N)
- Motor ENA (PWM): GPIO 12 → L298N ENA pin (speed control)
- Motor IN1: GPIO 13 → L298N IN1 pin (direction control)
- Motor IN2: GPIO 14 → L298N IN2 pin (direction control)
- PWM Frequency: 5 KHz
- PWM Resolution: 8-bit (0-255)
- Speed Range: 0-100% UI (maps to 100-200% actual motor speed)

### Buzzer
- Buzzer Pin: GPIO 11 → Buzzer signal pin
- Type: Passive buzzer (3-pin)
- Plays melodic pattern (C5-E5-G5-C6) when timer reaches 00:00 (overtime mode)
- Repeats every 1.5 seconds during overtime
- Stops when user presses Stop button

### Physical Button
- Button Pin: GPIO 9 → Physical button input
- Type: Momentary push button (normally open)
- Wiring: Connect one side to GPIO 9, other side to GND
- Internal pull-up resistor enabled (active LOW)
- Debounce: 200ms

### Complete Wiring Diagram
```
ESP32 GPIO 12 (PWM) → L298N ENA (speed control)
ESP32 GPIO 13       → L298N IN1 (direction)
ESP32 GPIO 14       → L298N IN2 (direction)
ESP32 GND           → L298N GND
L298N OUT1          → Motor +
L298N OUT2          → Motor -
L298N +12V          → External power supply (6-12V)
L298N GND           → External power supply GND (common ground with ESP32)

ESP32 GPIO 11       → Buzzer signal pin
Buzzer VCC          → 3.3V or 5V
Buzzer GND          → GND

ESP32 GPIO 9        → Physical button (one side)
GND                 → Physical button (other side)
```

**Motor Direction:**
- Forward: IN1=HIGH, IN2=LOW
- Reverse: IN1=LOW, IN2=HIGH (automatically reverses every 10 seconds by default)
- Stop: IN1=LOW, IN2=LOW

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
Configure development parameters across three pages:

**Page 1/3:**
- Developer time (default: 7:00)
- Stop Bath time (default: 1:00)
- Fixer time (default: 5:00)

**Page 2/3:**
- Rinse time (default: 10:00)
- Reverse time (default: 0:10) - interval for automatic motor direction reversal
- Motor speed (default: 0%, range: 0-100%)
  - 0% = 100% actual motor speed
  - 100% = 200% actual motor speed

**Page 3/3:**
- Overtime Speed (default: 5%, range: 1-100%) - motor speed during overtime mode
- Rotate 180° toggle - flip screen orientation for left/right-handed use

Use +/- buttons to adjust values. Time settings adjust in 5-second increments, speed in 5% increments, overtime speed in 1% increments. All settings are automatically saved to flash memory and persist across reboots.

### Start Develop Screen

**Stage Selection (Top Row):**
- Four stage buttons: **Dev**, **Stop**, **Fix**, **Rinse**
- Click any stage to switch (disabled during timer operation)
- Active stage highlighted in green

**Timer Display (Center):**
- Large MM:SS countdown display
- Down button (left) and Up button (right) to adjust time in 5-second intervals
- Adjustment buttons hidden during timer operation
- During overtime: displays +MM:SS in blinking red/white

**Control Buttons (Bottom):**

*When Stopped:*
- **← (Back)**: Return to main menu
- **Start**: Begin countdown and start motor
- **Reset**: Reset timer to default setting value

*When Running:*
- **Stop**: Full-width button to pause timer and stop motor
- All other buttons hidden for focused operation

### Physical Button Operation

The physical button on GPIO 9 provides hands-free operation:

1. **Main Menu**: Press to jump directly to "Start Develop" screen
2. **Develop Screen (Idle)**: Press to start the timer and motor
3. **Develop Screen (Running)**: Press to stop the timer and motor
4. **Overtime Mode (Dev/Stop/Fix)**: Press to stop and advance to next stage
5. **Overtime Mode (Rinse)**: Press to stop and return to Dev stage

This allows complete operation without touching the screen!

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

### Settings (3 Pages)
```
┌─────────────────────────────┐
│ [←]  Settings 1/3      [→]  │
│                             │
│ Developer    [-] 07:00 [+]  │
│ Stop Bath    [-] 01:00 [+]  │
│ Fixer        [-] 05:00 [+]  │
└─────────────────────────────┘

┌─────────────────────────────┐
│ [←]  Settings 2/3      [→]  │
│                             │
│ Rinse        [-] 10:00 [+]  │
│ Reverse      [-] 00:10 [+]  │
│ Speed        [-]   0%  [+]  │
└─────────────────────────────┘

┌─────────────────────────────┐
│ [←]  Settings 3/3           │
│                             │
│ OT Speed     [-]   5%  [+]  │
│                             │
│ Rotate 180°         [Toggle]│
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

### Start Develop (Overtime)
```
┌─────────────────────────────┐
│ [Dev][Stop][Fix][Rinse]     │  ← Stage buttons (disabled)
│                             │
│       +MM:SS                │  ← Blinking red/white
│                             │
│                             │
│ [      Stop      ]          │  ← Stop & advance
└─────────────────────────────┘
```

## Serial Monitor Output

Connect at 115200 baud to see:
- Settings loaded/saved notifications
- Screen navigation events
- Stage changes
- Timer start/stop/reset events
- Motor state changes (speed, direction)
- Motor reversal events
- Overtime mode transitions
- Physical button press events
- Screen rotation changes
- Touch events and button presses

## Customization

### Default Timer Values
Modify initial settings in `loadSettings()`:
```cpp
settings.devTime = preferences.getInt("devTime", 420);          // 7:00
settings.stopTime = preferences.getInt("stopTime", 60);         // 1:00
settings.fixTime = preferences.getInt("fixTime", 300);          // 5:00
settings.rinseTime = preferences.getInt("rinseTime", 600);      // 10:00
settings.reverseTime = preferences.getInt("revTime", 10);       // 0:10
settings.speed = preferences.getInt("speed", 0);                // 0% UI = 100% actual
settings.overtimeSpeed = preferences.getInt("overtimeSpeed", 5); // 5% default
settings.rotateScreen = preferences.getBool("rotateScreen", false); // false = 0°
```

### Timer Adjustment Increment
Change the increment value in `timerUpHandler()` and `timerDownHandler()`:
```cpp
currentTime += 5;  // Change 5 to desired seconds
```

### Motor Speed Control
Adjust PWM frequency and resolution:
```cpp
const int pwmFreq = 5000;      // 5 KHz
const int pwmResolution = 8;   // 8-bit (0-255)
```

Change motor reversal interval:
```cpp
settings.reverseTime = 10;  // Reverse every 10 seconds
```

Change overtime motor speed default:
```cpp
settings.overtimeSpeed = 5;  // 5% speed during overtime (configurable in settings)
```

Change button debounce delay:
```cpp
const unsigned long debounceDelay = 200;  // 200ms
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
- Use the "Rotate 180°" toggle in Settings 3/3 for runtime rotation
- Or change `LCD_ROTATION` value (0-3) in code for different default orientations
- Rotation setting persists across reboots

## License

MIT License - feel free to modify and use for your projects.

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.
