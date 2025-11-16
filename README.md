# Film Developer Controller

An ESP32-based film developer controller with touch interface for automated film development processes.

## Features

- **Touch Interface**: CST816 capacitive touch controller with 320x240 display
- **Visual Feedback**: Buttons flash white when pressed for clear user feedback
- **Real-time Timer**: Large MM:SS format timer displays elapsed time during operation
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
   - Arduino_GFX_Library
   - Wire (built-in)
   - bsp_cst816 (custom touch library)
3. Clone this repository
4. Open `film_developer.ino` in Arduino IDE
5. Select your ESP32 board
6. Upload to your device

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
Edit the button definitions:
```cpp
Button startButton = { 10, 180, 140, 50, "START", color565(0, 255, 0) };
Button stopButton = { 170, 180, 140, 50, "STOP", color565(255, 0, 0) };
```

### Modify Display Rotation
Change `LCD_ROTATION` value (0-3) for different orientations.

## License

MIT License - feel free to modify and use for your projects.

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.
