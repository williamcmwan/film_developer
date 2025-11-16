#include <lvgl.h>
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

// Motor control pins
const int motorPin1 = 12;
const int motorPin2 = 13;

// Display and touch
Arduino_DataBus *bus = new Arduino_ESP32SPI(
  PIN_NUM_LCD_DC, PIN_NUM_LCD_CS,
  PIN_NUM_LCD_SCLK, PIN_NUM_LCD_MOSI, PIN_NUM_LCD_MISO);

Arduino_GFX *gfx = new Arduino_ST7789(
  bus, PIN_NUM_LCD_RST, LCD_ROTATION, true,
  LCD_H_RES, LCD_V_RES);

TwoWire touchWire = TwoWire(0);

// LVGL 8.3 display buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[SCREEN_WIDTH * 10];

// LVGL objects
lv_obj_t *timerLabel;
lv_obj_t *startBtn;
lv_obj_t *stopBtn;

// State variables
volatile bool stopRequested = false;
volatile bool timerRunning = false;
unsigned long timerStartMillis = 0;
unsigned long elapsedSeconds = 0;

// Display flush callback for LVGL 8.3
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);

  lv_disp_flush_ready(disp);
}

// Touch read callback for LVGL 8.3
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  uint16_t touch_x, touch_y;
  
  bsp_touch_read();
  
  if (bsp_touch_get_coordinates(&touch_x, &touch_y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch_x;
    data->point.y = touch_y;
    Serial.printf("[TOUCH] X=%d, Y=%d\n", touch_x, touch_y);
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// Motor control functions
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

// Update timer display once per second
void updateTimer() {
  if (timerRunning) {
    unsigned long currentSeconds = (millis() - timerStartMillis) / 1000;
    if (currentSeconds != elapsedSeconds) {
      elapsedSeconds = currentSeconds;
      char timeStr[6];
      sprintf(timeStr, "%02lu:%02lu", elapsedSeconds / 60, elapsedSeconds % 60);
      
      // Clear timer area and redraw
      gfx->fillRect(40, 50, 240, 80, BLACK);
      gfx->setTextColor(WHITE);
      gfx->setTextSize(6);
      gfx->setCursor(70, 75);
      gfx->print(timeStr);
    }
  }
}

// Check for stop button and update timer
void checkStopButton() {
  updateTimer();
  
  uint16_t touch_x, touch_y;
  bsp_touch_read();
  if (bsp_touch_get_coordinates(&touch_x, &touch_y)) {
    // Check if touch is on stop button area (bottom right)
    if (touch_x >= 170 && touch_x <= 310 && touch_y >= 180 && touch_y <= 240) {
      Serial.println("[BUTTON] STOP pressed");
      stopRequested = true;
      timerRunning = false;
      delay(200); // Debounce
    }
  }
}

// Motor sequence
void runMotorSequence() {
  stopRequested = false;

  Serial.println("[MOTOR] Clockwise rotation");
  motorClockwise();
  for (int i = 0; i < 100; i++) {
    if (stopRequested) break;
    checkStopButton();
    delay(100);
  }
  stopMotor();
  if (stopRequested) return;

  Serial.println("[MOTOR] Counter-clockwise rotation");
  motorCounterClockwise();
  for (int i = 0; i < 100; i++) {
    if (stopRequested) break;
    checkStopButton();
    delay(100);
  }
  stopMotor();
}

// Button event handlers
void start_btn_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  
  if (code == LV_EVENT_CLICKED) {
    Serial.println("[BUTTON] START pressed");
    
    // Visual feedback - flash button
    lv_obj_set_style_bg_color(startBtn, lv_color_make(100, 255, 100), 0);
    lv_timer_handler();
    delay(100);
    lv_obj_set_style_bg_color(startBtn, lv_color_make(0, 200, 0), 0);
    lv_timer_handler();
    
    // Start timer and draw it directly with GFX
    timerRunning = true;
    timerStartMillis = millis();
    elapsedSeconds = 0;
    
    // Draw initial timer directly with larger clear area
    gfx->fillRect(40, 50, 240, 80, BLACK);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(6);
    gfx->setCursor(70, 75);
    gfx->print("00:00");
    
    delay(200);
    
    stopRequested = false;
    runMotorSequence();
    
    timerRunning = false;
    
    Serial.printf("[MOTOR] Complete - %02lu:%02lu\n", 
                  elapsedSeconds / 60, elapsedSeconds % 60);
  }
}

void stop_btn_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  
  if (code == LV_EVENT_CLICKED) {
    Serial.println("[BUTTON] STOP pressed");
    
    // Visual feedback - flash button
    lv_obj_set_style_bg_color(stopBtn, lv_color_make(255, 100, 100), 0);
    lv_timer_handler();
    delay(100);
    lv_obj_set_style_bg_color(stopBtn, lv_color_make(200, 0, 0), 0);
    lv_timer_handler();
    
    stopRequested = true;
    timerRunning = false;
    stopMotor();
  }
}

// Create UI
void create_ui() {
  // Set black background
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
  
  // Timer label (center of screen) - LARGE white text
  timerLabel = lv_label_create(lv_scr_act());
  lv_label_set_text(timerLabel, "00:00");
  
  // Set text color explicitly to WHITE in multiple ways
  lv_obj_set_style_text_color(timerLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(timerLabel, lv_color_make(255, 255, 255), 0);
  lv_obj_set_style_text_opa(timerLabel, LV_OPA_COVER, 0); // Fully opaque
  
  lv_obj_set_style_text_font(timerLabel, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_letter_space(timerLabel, 10, 0);
  lv_obj_align(timerLabel, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_flag(timerLabel, LV_OBJ_FLAG_HIDDEN);
  

  
  // START button (bottom left) with press feedback
  startBtn = lv_btn_create(lv_scr_act());
  lv_obj_set_size(startBtn, 140, 60);
  lv_obj_align(startBtn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_set_style_bg_color(startBtn, lv_color_make(0, 200, 0), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(startBtn, lv_color_make(0, 150, 0), LV_STATE_PRESSED); // Darker when pressed
  lv_obj_set_style_radius(startBtn, 10, 0);
  lv_obj_set_style_shadow_width(startBtn, 10, LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(startBtn, 0, LV_STATE_PRESSED); // No shadow when pressed
  lv_obj_add_event_cb(startBtn, start_btn_event_handler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *startLabel = lv_label_create(startBtn);
  lv_label_set_text(startLabel, "START");
  lv_obj_set_style_text_color(startLabel, lv_color_black(), 0);
  lv_obj_set_style_text_font(startLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(startLabel);
  
  // STOP button (bottom right) with press feedback
  stopBtn = lv_btn_create(lv_scr_act());
  lv_obj_set_size(stopBtn, 140, 60);
  lv_obj_align(stopBtn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_style_bg_color(stopBtn, lv_color_make(200, 0, 0), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(stopBtn, lv_color_make(150, 0, 0), LV_STATE_PRESSED); // Darker when pressed
  lv_obj_set_style_radius(stopBtn, 10, 0);
  lv_obj_set_style_shadow_width(stopBtn, 10, LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(stopBtn, 0, LV_STATE_PRESSED); // No shadow when pressed
  lv_obj_add_event_cb(stopBtn, stop_btn_event_handler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *stopLabel = lv_label_create(stopBtn);
  lv_label_set_text(stopLabel, "STOP");
  lv_obj_set_style_text_color(stopLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(stopLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(stopLabel);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Film Developer Starting...");

  // Motor pins
  pinMode(motorPin1, OUTPUT);
  pinMode(motorPin2, OUTPUT);
  stopMotor();

  // Initialize display
  if (!gfx->begin()) {
    Serial.println("Display init failed!");
  }
  gfx->fillScreen(BLACK);

#ifdef PIN_NUM_LCD_BL
  pinMode(PIN_NUM_LCD_BL, OUTPUT);
  digitalWrite(PIN_NUM_LCD_BL, HIGH);
#endif

  // Initialize touch
  touchWire.begin(TOUCH_SDA, TOUCH_SCL);
  if (bsp_touch_init(&touchWire, TOUCH_ROTATION, SCREEN_WIDTH, SCREEN_HEIGHT)) {
    Serial.printf("[TOUCH] CST816 initialized - %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
  } else {
    Serial.println("[TOUCH] Failed to initialize");
  }

  // Initialize LVGL
  lv_init();

  // Setup display buffer (LVGL 8.3 API)
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_WIDTH * 10);

  // Initialize display driver (LVGL 8.3 API)
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Initialize touch driver (LVGL 8.3 API)
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // Create UI
  create_ui();

  Serial.println("Setup complete");
}

void loop() {
  updateTimer();
  lv_timer_handler();
  delay(5);
}
