#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Preferences.h>
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
Preferences preferences;

// LVGL display buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[SCREEN_WIDTH * 10];

// Screens
lv_obj_t *splashScreen;
lv_obj_t *mainMenuScreen;
lv_obj_t *settingsScreen1;
lv_obj_t *settingsScreen2;
lv_obj_t *developScreen;

// Settings structure
struct Settings {
  int devTime;      // seconds
  int stopTime;     // seconds
  int fixTime;      // seconds
  int rinseTime;    // seconds
  int reverseTime;  // seconds
  int speed;        // percentage
} settings;

// Development state
enum Stage { DEV, STOP_BATH, FIX, RINSE };
Stage currentStage = DEV;
int stageTime[4];
int currentTime = 0;
bool timerRunning = false;
unsigned long lastSecond = 0;

// Display flush callback
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(disp);
}

// Touch read callback
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  uint16_t touch_x, touch_y;
  bsp_touch_read();
  
  if (bsp_touch_get_coordinates(&touch_x, &touch_y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch_x;
    data->point.y = touch_y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// Motor control
void stopMotor() {
  digitalWrite(motorPin1, LOW);
  digitalWrite(motorPin2, LOW);
}

void startMotor() {
  digitalWrite(motorPin1, HIGH);
  digitalWrite(motorPin2, LOW);
}

// Load settings from flash
void loadSettings() {
  preferences.begin("filmdev", false);
  settings.devTime = preferences.getInt("devTime", 420);      // 7:00
  settings.stopTime = preferences.getInt("stopTime", 60);     // 1:00
  settings.fixTime = preferences.getInt("fixTime", 300);      // 5:00
  settings.rinseTime = preferences.getInt("rinseTime", 180);  // 3:00
  settings.reverseTime = preferences.getInt("revTime", 30);   // 0:30
  settings.speed = preferences.getInt("speed", 100);
  preferences.end();
  
  Serial.println("[SETTINGS] Loaded from flash");
}

// Save settings to flash
void saveSettings() {
  preferences.begin("filmdev", false);
  preferences.putInt("devTime", settings.devTime);
  preferences.putInt("stopTime", settings.stopTime);
  preferences.putInt("fixTime", settings.fixTime);
  preferences.putInt("rinseTime", settings.rinseTime);
  preferences.putInt("revTime", settings.reverseTime);
  preferences.putInt("speed", settings.speed);
  preferences.end();
  
  Serial.println("[SETTINGS] Saved to flash");
}

// Forward declarations
void createSplashScreen();
void createMainMenuScreen();
void createSettingsScreen1();
void createSettingsScreen2();
void createDevelopScreen();
void showScreen(lv_obj_t *screen);

// Splash screen with animation
void createSplashScreen() {
  splashScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(splashScreen, lv_color_black(), 0);
  
  // Main title
  lv_obj_t *titleLabel = lv_label_create(splashScreen);
  lv_label_set_text(titleLabel, "Film Developer");
  lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_28, 0);
  lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, -20);
  
  // Version label (smaller, below title)
  lv_obj_t *versionLabel = lv_label_create(splashScreen);
  lv_label_set_text(versionLabel, "v1.0");
  lv_obj_set_style_text_color(versionLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(versionLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(versionLabel, LV_ALIGN_CENTER, 0, 20);
  
  // Fade in animation for title
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, titleLabel);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_time(&a, 1000);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
  lv_anim_start(&a);
  
  // Fade in animation for version (delayed)
  lv_anim_t a2;
  lv_anim_init(&a2);
  lv_anim_set_var(&a2, versionLabel);
  lv_anim_set_values(&a2, 0, 255);
  lv_anim_set_time(&a2, 1000);
  lv_anim_set_delay(&a2, 300);
  lv_anim_set_exec_cb(&a2, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
  lv_anim_start(&a2);
}

// Main menu screen
void mainMenuStartHandler(lv_event_t *e);
void mainMenuSettingsHandler(lv_event_t *e);

void createMainMenuScreen() {
  mainMenuScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(mainMenuScreen, lv_color_black(), 0);
  
  // Title
  lv_obj_t *title = lv_label_create(mainMenuScreen);
  lv_label_set_text(title, "Film Developer");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  
  // Start Develop button
  lv_obj_t *startBtn = lv_btn_create(mainMenuScreen);
  lv_obj_set_size(startBtn, 200, 60);
  lv_obj_align(startBtn, LV_ALIGN_CENTER, 0, -30);
  lv_obj_set_style_bg_color(startBtn, lv_color_make(0, 150, 0), 0);
  lv_obj_add_event_cb(startBtn, mainMenuStartHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *startLabel = lv_label_create(startBtn);
  lv_label_set_text(startLabel, "Start Develop");
  lv_obj_set_style_text_font(startLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(startLabel);
  
  // Settings button
  lv_obj_t *settingsBtn = lv_btn_create(mainMenuScreen);
  lv_obj_set_size(settingsBtn, 200, 60);
  lv_obj_align(settingsBtn, LV_ALIGN_CENTER, 0, 40);
  lv_obj_set_style_bg_color(settingsBtn, lv_color_make(100, 100, 100), 0);
  lv_obj_add_event_cb(settingsBtn, mainMenuSettingsHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *settingsLabel = lv_label_create(settingsBtn);
  lv_label_set_text(settingsLabel, "Settings");
  lv_obj_set_style_text_font(settingsLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(settingsLabel);
}

void mainMenuStartHandler(lv_event_t *e) {
  Serial.println("[MENU] Start Develop");
  if (developScreen != NULL) {
    // Initialize stage times from settings
    stageTime[DEV] = settings.devTime;
    stageTime[STOP_BATH] = settings.stopTime;
    stageTime[FIX] = settings.fixTime;
    stageTime[RINSE] = settings.rinseTime;
    currentStage = DEV;
    currentTime = stageTime[DEV];
    timerRunning = false;
    
    showScreen(developScreen);
  }
}

void mainMenuSettingsHandler(lv_event_t *e) {
  Serial.println("[MENU] Settings");
  if (settingsScreen1 != NULL) {
    showScreen(settingsScreen1);
  }
}

// Settings screen labels
lv_obj_t *devTimeLabel, *stopTimeLabel, *fixTimeLabel, *rinseTimeLabel, *revTimeLabel, *speedLabel;

void settingsBackHandler(lv_event_t *e);
void settingsNextHandler(lv_event_t *e);
void settingsPrevHandler(lv_event_t *e);
void settingsValueHandler(lv_event_t *e);

void updateSettingsLabels() {
  char buf[16];
  
  // Only update labels that exist (not NULL)
  if (devTimeLabel != NULL) {
    sprintf(buf, "%02d:%02d", settings.devTime / 60, settings.devTime % 60);
    lv_label_set_text(devTimeLabel, buf);
  }
  
  if (stopTimeLabel != NULL) {
    sprintf(buf, "%02d:%02d", settings.stopTime / 60, settings.stopTime % 60);
    lv_label_set_text(stopTimeLabel, buf);
  }
  
  if (fixTimeLabel != NULL) {
    sprintf(buf, "%02d:%02d", settings.fixTime / 60, settings.fixTime % 60);
    lv_label_set_text(fixTimeLabel, buf);
  }
  
  if (rinseTimeLabel != NULL) {
    sprintf(buf, "%02d:%02d", settings.rinseTime / 60, settings.rinseTime % 60);
    lv_label_set_text(rinseTimeLabel, buf);
  }
  
  if (revTimeLabel != NULL) {
    sprintf(buf, "%02d:%02d", settings.reverseTime / 60, settings.reverseTime % 60);
    lv_label_set_text(revTimeLabel, buf);
  }
  
  if (speedLabel != NULL) {
    sprintf(buf, "%d%%", settings.speed);
    lv_label_set_text(speedLabel, buf);
  }
}

void createSettingRow(lv_obj_t *parent, const char *name, lv_obj_t **valueLabel, int y, int settingId) {
  // Name label
  lv_obj_t *nameLabel = lv_label_create(parent);
  lv_label_set_text(nameLabel, name);
  lv_obj_set_style_text_color(nameLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_pos(nameLabel, 10, y);
  
  // Minus button
  lv_obj_t *minusBtn = lv_btn_create(parent);
  lv_obj_set_size(minusBtn, 50, 45);
  lv_obj_set_pos(minusBtn, 150, y - 5);
  lv_obj_set_style_bg_color(minusBtn, lv_color_make(80, 80, 80), 0);
  lv_obj_add_event_cb(minusBtn, settingsValueHandler, LV_EVENT_CLICKED, (void*)(intptr_t)(settingId * 2));
  
  lv_obj_t *minusLabel = lv_label_create(minusBtn);
  lv_label_set_text(minusLabel, "-");
  lv_obj_set_style_text_font(minusLabel, &lv_font_montserrat_28, 0);
  lv_obj_center(minusLabel);
  
  // Value label
  *valueLabel = lv_label_create(parent);
  lv_label_set_text(*valueLabel, "00:00");
  lv_obj_set_style_text_color(*valueLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(*valueLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_pos(*valueLabel, 210, y);
  
  // Plus button
  lv_obj_t *plusBtn = lv_btn_create(parent);
  lv_obj_set_size(plusBtn, 50, 45);
  lv_obj_set_pos(plusBtn, 270, y - 5);
  lv_obj_set_style_bg_color(plusBtn, lv_color_make(80, 80, 80), 0);
  lv_obj_add_event_cb(plusBtn, settingsValueHandler, LV_EVENT_CLICKED, (void*)(intptr_t)(settingId * 2 + 1));
  
  lv_obj_t *plusLabel = lv_label_create(plusBtn);
  lv_label_set_text(plusLabel, "+");
  lv_obj_set_style_text_font(plusLabel, &lv_font_montserrat_28, 0);
  lv_obj_center(plusLabel);
}

// Settings Screen 1 (Developer, Stop Bath, Fixer)
void createSettingsScreen1() {
  settingsScreen1 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(settingsScreen1, lv_color_black(), 0);
  
  // Back button
  lv_obj_t *backBtn = lv_btn_create(settingsScreen1);
  lv_obj_set_size(backBtn, 60, 40);
  lv_obj_align(backBtn, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_event_cb(backBtn, settingsBackHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *backLabel = lv_label_create(backBtn);
  lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(backLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(backLabel);
  
  // Title
  lv_obj_t *title = lv_label_create(settingsScreen1);
  lv_label_set_text(title, "Settings 1/2");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  
  // Next button (right arrow)
  lv_obj_t *nextBtn = lv_btn_create(settingsScreen1);
  lv_obj_set_size(nextBtn, 60, 40);
  lv_obj_align(nextBtn, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_add_event_cb(nextBtn, settingsNextHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *nextLabel = lv_label_create(nextBtn);
  lv_label_set_text(nextLabel, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(nextLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(nextLabel);
  
  // Settings rows
  createSettingRow(settingsScreen1, "Developer", &devTimeLabel, 70, 0);
  createSettingRow(settingsScreen1, "Stop Bath", &stopTimeLabel, 130, 1);
  createSettingRow(settingsScreen1, "Fixer", &fixTimeLabel, 190, 2);
  
  updateSettingsLabels();
}

// Settings Screen 2 (Rinse, Reverse, Speed)
void createSettingsScreen2() {
  settingsScreen2 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(settingsScreen2, lv_color_black(), 0);
  
  // Back button (to previous settings page)
  lv_obj_t *backBtn = lv_btn_create(settingsScreen2);
  lv_obj_set_size(backBtn, 60, 40);
  lv_obj_align(backBtn, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_event_cb(backBtn, settingsPrevHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *backLabel = lv_label_create(backBtn);
  lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(backLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(backLabel);
  
  // Title
  lv_obj_t *title = lv_label_create(settingsScreen2);
  lv_label_set_text(title, "Settings 2/2");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  
  // Settings rows
  createSettingRow(settingsScreen2, "Rinse", &rinseTimeLabel, 70, 3);
  createSettingRow(settingsScreen2, "Reverse", &revTimeLabel, 130, 4);
  createSettingRow(settingsScreen2, "Speed", &speedLabel, 190, 5);
  
  updateSettingsLabels();
}

void settingsValueHandler(lv_event_t *e) {
  int action = (int)(intptr_t)lv_event_get_user_data(e);
  int settingId = action / 2;
  bool isPlus = (action % 2) == 1;
  int change = isPlus ? 1 : -1;
  
  switch(settingId) {
    case 0: // Developer
      settings.devTime += change * 5;
      if (settings.devTime < 0) settings.devTime = 0;
      if (settings.devTime > 3600) settings.devTime = 3600;
      break;
    case 1: // Stop Bath
      settings.stopTime += change * 5;
      if (settings.stopTime < 0) settings.stopTime = 0;
      if (settings.stopTime > 600) settings.stopTime = 600;
      break;
    case 2: // Fixer
      settings.fixTime += change * 5;
      if (settings.fixTime < 0) settings.fixTime = 0;
      if (settings.fixTime > 3600) settings.fixTime = 3600;
      break;
    case 3: // Rinse
      settings.rinseTime += change * 5;
      if (settings.rinseTime < 0) settings.rinseTime = 0;
      if (settings.rinseTime > 3600) settings.rinseTime = 3600;
      break;
    case 4: // Reverse
      settings.reverseTime += change * 5;
      if (settings.reverseTime < 0) settings.reverseTime = 0;
      if (settings.reverseTime > 300) settings.reverseTime = 300;
      break;
    case 5: // Speed
      settings.speed += change * 5;
      if (settings.speed < 10) settings.speed = 10;
      if (settings.speed > 200) settings.speed = 200;
      break;
  }
  
  updateSettingsLabels();
  saveSettings();
}

void settingsBackHandler(lv_event_t *e) {
  if (mainMenuScreen != NULL) {
    showScreen(mainMenuScreen);
  }
}

void settingsNextHandler(lv_event_t *e) {
  if (settingsScreen2 != NULL) {
    showScreen(settingsScreen2);
  }
}

void settingsPrevHandler(lv_event_t *e) {
  if (settingsScreen1 != NULL) {
    showScreen(settingsScreen1);
  }
}

// Develop screen (simplified - will expand)
void createDevelopScreen() {
  developScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(developScreen, lv_color_black(), 0);
  
  lv_obj_t *label = lv_label_create(developScreen);
  lv_label_set_text(label, "Develop screen\ncoming soon...");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

// Show screen
void showScreen(lv_obj_t *screen) {
  lv_scr_load(screen);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Film Developer Starting...");

  // Motor pins
  pinMode(motorPin1, OUTPUT);
  pinMode(motorPin2, OUTPUT);
  stopMotor();

  // Initialize display
  gfx->begin();
  gfx->fillScreen(BLACK);

#ifdef PIN_NUM_LCD_BL
  pinMode(PIN_NUM_LCD_BL, OUTPUT);
  digitalWrite(PIN_NUM_LCD_BL, HIGH);
#endif

  // Initialize touch
  touchWire.begin(TOUCH_SDA, TOUCH_SCL);
  bsp_touch_init(&touchWire, TOUCH_ROTATION, SCREEN_WIDTH, SCREEN_HEIGHT);

  // Initialize LVGL
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_WIDTH * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // Load settings
  loadSettings();

  // Create all screens
  createSplashScreen();
  createMainMenuScreen();
  createSettingsScreen1();
  createSettingsScreen2();
  createDevelopScreen();

  // Show splash screen
  showScreen(splashScreen);
  
  // Auto-transition to main menu after 3 seconds
  lv_timer_t *timer = lv_timer_create([](lv_timer_t *timer) {
    showScreen(mainMenuScreen);
    lv_timer_del(timer);
  }, 3000, NULL);

  Serial.println("Setup complete");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
