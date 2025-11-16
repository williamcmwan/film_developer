#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "bsp_cst816.h"

#define PIN_NUM_LCD_SCLK 39
#define PIN_NUM_LCD_MOSI 38
#define PIN_NUM_LCD_MISO 40
#define PIN_NUM_LCD_DC 42
#define PIN_NUM_LCD_RST -1
#define PIN_NUM_LCD_CS 45
#define PIN_NUM_LCD_BL 1

#define LCD_ROTATION 1
#define LCD_H_RES 240
#define LCD_V_RES 320

// With rotation 1, the screen becomes landscape: 320x240
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define TOUCH_SDA 48
#define TOUCH_SCL 47
#define TOUCH_ROTATION 1

// Motor control pins (adjust to your wiring)
const int motorPin1 = 12;
const int motorPin2 = 13;

Arduino_DataBus *bus = new Arduino_ESP32SPI(
  PIN_NUM_LCD_DC /* DC */, PIN_NUM_LCD_CS /* CS */,
  PIN_NUM_LCD_SCLK /* SCK */, PIN_NUM_LCD_MOSI /* MOSI */, PIN_NUM_LCD_MISO /* MISO */);

Arduino_GFX *gfx = new Arduino_ST7789(
  bus, PIN_NUM_LCD_RST /* RST */, LCD_ROTATION /* rotation */, true /* IPS */,
  LCD_H_RES /* width */, LCD_V_RES /* height */);

// Touch I2C instance
TwoWire touchWire = TwoWire(0);

struct Button {
  int x, y, w, h;
  const char* label;
  uint16_t color;
};

// Color conversion helper function
uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Buttons at bottom of screen (320x240 with rotation 1)
// Button width: 140, height: 50
// Y position: 240 - 60 = 180 (60px from bottom)
Button startButton = { 10, 180, 140, 50, "START", color565(0, 255, 0) }; // Green
Button stopButton = { 170, 180, 140, 50, "STOP", color565(255, 0, 0) };   // Red

volatile bool stopRequested = false;
volatile bool timerRunning = false;
unsigned long timerStartMillis = 0;
unsigned long elapsedSeconds = 0;

void drawButton(Button &btn, bool pressed = false) {
  uint16_t btnColor = pressed ? color565(255, 255, 255) : btn.color; // White when pressed
  gfx->fillRoundRect(btn.x, btn.y, btn.w, btn.h, 10, btnColor);
  gfx->setTextColor(0x0000);  // Black
  gfx->setTextSize(2);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx->getTextBounds(btn.label, 0, 0, &tbx, &tby, &tbw, &tbh);
  int tx = btn.x + (btn.w - tbw) / 2;
  int ty = btn.y + (btn.h - tbh) / 2;
  gfx->setCursor(tx, ty);
  gfx->print(btn.label);
}

void flashButton(Button &btn) {
  drawButton(btn, true);  // Flash white
  delay(100);
  drawButton(btn, false); // Back to normal
}

void clearTimerArea() {
  // Clear the middle area where timer is displayed
  gfx->fillRect(0, 40, SCREEN_WIDTH, 120, 0x0000);
}

void displayTimer(unsigned long seconds) {
  unsigned long minutes = seconds / 60;
  unsigned long secs = seconds % 60;
  
  char timeStr[6];
  sprintf(timeStr, "%02lu:%02lu", minutes, secs);
  
  gfx->setTextSize(6);
  gfx->setTextColor(color565(255, 255, 255)); // White
  
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  gfx->getTextBounds(timeStr, 0, 0, &tbx, &tby, &tbw, &tbh);
  
  int tx = (SCREEN_WIDTH - tbw) / 2;
  int ty = (SCREEN_HEIGHT - tbh) / 2 - 20; // Slightly above center
  
  clearTimerArea();
  gfx->setCursor(tx, ty);
  gfx->print(timeStr);
}

bool isInsideButton(int x, int y, Button &btn) {
  return x >= btn.x && x <= (btn.x + btn.w) && y >= btn.y && y <= (btn.y + btn.h);
}

// Read touch using CST816 touch controller
bool readTouch(int &x, int &y) {
  uint16_t touch_x, touch_y;
  
  // Read touch data from controller
  bsp_touch_read();
  
  // Get coordinates if touch detected
  if (bsp_touch_get_coordinates(&touch_x, &touch_y)) {
    x = (int)touch_x;
    y = (int)touch_y;
    Serial.printf("[TOUCH] Detected at X=%d, Y=%d\n", x, y);
    return true;
  }
  return false;
}

void stopMotor() {
  digitalWrite(motorPin1, LOW);
  digitalWrite(motorPin2, LOW);
}

void motorClockwise() {
  digitalWrite(motorPin1, HIGH);
  digitalWrite(motorPin2, LOW);
}

void motorCounterClockwise() {
  digitalWrite(motorPin1, LOW);
  digitalWrite(motorPin2, HIGH);
}

// Check for stop button press and update timer during motor sequence
void checkStopButton() {
  // Update timer display
  if (timerRunning) {
    unsigned long currentSeconds = (millis() - timerStartMillis) / 1000;
    if (currentSeconds != elapsedSeconds) {
      elapsedSeconds = currentSeconds;
      displayTimer(elapsedSeconds);
    }
  }
  
  // Check for touch
  int tx, ty;
  if (readTouch(tx, ty)) {
    if (isInsideButton(tx, ty, stopButton)) {
      Serial.println("[BUTTON] STOP pressed during sequence");
      flashButton(stopButton);
      stopRequested = true;
      timerRunning = false;
      delay(200); // Debounce
    }
  }
}

void runMotorSequence() {
  stopRequested = false;

  Serial.println("[MOTOR] Starting clockwise rotation");
  motorClockwise();
  for (int i = 0; i < 100; i++) {
    if (stopRequested) {
      Serial.println("[MOTOR] Clockwise rotation stopped");
      break;
    }
    delay(50);
    checkStopButton();
    delay(50);
  }
  stopMotor();
  if (stopRequested) return;

  Serial.println("[MOTOR] Starting counter-clockwise rotation");
  motorCounterClockwise();
  for (int i = 0; i < 100; i++) {
    if (stopRequested) {
      Serial.println("[MOTOR] Counter-clockwise rotation stopped");
      break;
    }
    delay(50);
    checkStopButton();
    delay(50);
  }
  stopMotor();
}

void setup(void) {
  Serial.begin(115200);
  Serial.println("Film Developer Starting...");

  pinMode(motorPin1, OUTPUT);
  pinMode(motorPin2, OUTPUT);
  stopMotor();

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(0x0000); // Black screen

#ifdef PIN_NUM_LCD_BL
  pinMode(PIN_NUM_LCD_BL, OUTPUT);
  digitalWrite(PIN_NUM_LCD_BL, HIGH);
#endif

  // Initialize touch controller with rotated dimensions
  touchWire.begin(TOUCH_SDA, TOUCH_SCL);
  if (bsp_touch_init(&touchWire, TOUCH_ROTATION, SCREEN_WIDTH, SCREEN_HEIGHT)) {
    Serial.printf("[TOUCH] CST816 initialized - Screen: %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
  } else {
    Serial.println("[TOUCH] Failed to initialize touch controller");
  }

  drawButton(startButton);
  drawButton(stopButton);

  Serial.println("Setup complete");
}

void loop() {
  // Update timer if running
  if (timerRunning) {
    unsigned long currentSeconds = (millis() - timerStartMillis) / 1000;
    if (currentSeconds != elapsedSeconds) {
      elapsedSeconds = currentSeconds;
      displayTimer(elapsedSeconds);
    }
  }
  
  int tx, ty;
  if (readTouch(tx, ty)) {
    if (isInsideButton(tx, ty, startButton)) {
      Serial.println("[BUTTON] START pressed - Beginning motor sequence");
      flashButton(startButton);
      
      // Start timer
      timerRunning = true;
      timerStartMillis = millis();
      elapsedSeconds = 0;
      displayTimer(0);
      
      stopRequested = false;
      runMotorSequence();
      
      timerRunning = false;
      Serial.printf("[MOTOR] Sequence complete - Total time: %02lu:%02lu\n", 
                    elapsedSeconds / 60, elapsedSeconds % 60);
    } else if (isInsideButton(tx, ty, stopButton)) {
      Serial.println("[BUTTON] STOP pressed - Stopping motor");
      flashButton(stopButton);
      stopRequested = true;
      timerRunning = false;
      stopMotor();
    } else {
      Serial.printf("[TOUCH] Touch outside buttons at X=%d, Y=%d\n", tx, ty);
    }
    delay(200); // Debounce delay
  }

  delay(50);
}
