#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
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

// Motor control pins (L298N)
const int motorENA = 12;   // PWM pin for speed control (ENA)
const int motorIN1 = 13;   // Direction control 1 (IN1)
const int motorIN2 = 14;   // Direction control 2 (IN2)

// Buzzer pin
const int buzzerPin = 11;  // Buzzer signal pin (GPIO 11)

// Physical button pin
const int buttonPin = 9;  // Physical button (GPIO 9)

// Button debounce variables
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200;  // 200ms debounce

// PWM configuration for motor
const int pwmFreq = 5000;      // 5 KHz
const int pwmResolution = 8;   // 8-bit resolution (0-255)

// Display and touch
Arduino_DataBus *bus = new Arduino_ESP32SPI(
  PIN_NUM_LCD_DC, PIN_NUM_LCD_CS,
  PIN_NUM_LCD_SCLK, PIN_NUM_LCD_MOSI, PIN_NUM_LCD_MISO);

Arduino_GFX *gfx = new Arduino_ST7789(
  bus, PIN_NUM_LCD_RST, LCD_ROTATION, true,
  LCD_H_RES, LCD_V_RES);

TwoWire touchWire = TwoWire(0);
Preferences preferences;

// WiFi and Web Server
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// WiFi configuration
struct WiFiConfig {
  char ssid[33];
  char password[65];
  bool configured;
} wifiConfig;

bool wifiConnected = false;
bool apMode = true;
const char* AP_SSID = "Film-Developer";
const char* AP_PASS = "12345678";
const char* MDNS_NAME = "filmdeveloper";

// WiFi screen
lv_obj_t *wifiScreen;
lv_obj_t *wifiStatusLabel;
lv_obj_t *wifiSSIDLabel;
lv_obj_t *wifiIPLabel;

// LVGL display buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[SCREEN_WIDTH * 10];

// Screens
lv_obj_t *splashScreen;
lv_obj_t *mainMenuScreen;
lv_obj_t *settingsScreen1;
lv_obj_t *settingsScreen2;
lv_obj_t *settingsScreen3;
lv_obj_t *settingsScreen4;  // WiFi settings
lv_obj_t *developScreen;

// Settings structure
struct Settings {
  int devTime;        // seconds
  int stopTime;       // seconds
  int fixTime;        // seconds
  int rinseTime;      // seconds
  int reverseTime;    // seconds
  int speed;          // percentage
  int overtimeSpeed;  // percentage (1-100%)
  bool rotateScreen;  // screen rotation (false = 0°, true = 180°)
} settings;

// Development state
enum Stage { DEV, STOP_BATH, FIX, RINSE };
Stage currentStage = DEV;
int stageTime[4];
int currentTime = 0;
bool timerRunning = false;
bool isOvertime = false;
bool motorDirection = true;  // true = forward, false = reverse
int reverseCounter = 0;
unsigned long lastSecond = 0;
unsigned long lastBlink = 0;
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;

// Develop screen UI elements
lv_obj_t *stageButtons[4];
lv_obj_t *timerLabel;
lv_obj_t *startBtn, *stopBtn, *resetBtn;
lv_obj_t *upBtn, *downBtn;
lv_obj_t *backBtn;

// Forward declarations for UI update functions
void updateTimerDisplay();
void updateStageButtons();
void updateControlButtons();
void updateSettingsLabels();
void applyScreenRotation();
void showScreen(lv_obj_t *screen);

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

// Motor control with PWM (L298N)
void stopMotor() {
  ledcWrite(motorENA, 0);
  digitalWrite(motorIN1, LOW);
  digitalWrite(motorIN2, LOW);
}

void setMotorDirection(bool forward) {
  if (forward) {
    digitalWrite(motorIN1, HIGH);
    digitalWrite(motorIN2, LOW);
    Serial.println("[MOTOR] Direction: FORWARD");
  } else {
    digitalWrite(motorIN1, LOW);
    digitalWrite(motorIN2, HIGH);
    Serial.println("[MOTOR] Direction: REVERSE");
  }
  motorDirection = forward;
}

void startMotor() {
  // Map UI speed (0-100%) to actual speed (100-200%)
  // UI 0% = 100% actual, UI 100% = 200% actual
  int actualSpeed = map(settings.speed, 0, 100, 100, 200);
  int pwmValue = map(actualSpeed, 0, 200, 0, 255);
  ledcWrite(motorENA, pwmValue);
  // Set initial direction to forward
  setMotorDirection(true);
  reverseCounter = 0;
  Serial.print("[MOTOR] Speed set to ");
  Serial.print(settings.speed);
  Serial.print("% (UI) = ");
  Serial.print(actualSpeed);
  Serial.print("% (actual), PWM: ");
  Serial.print(pwmValue);
  Serial.println(")");
}

void setMotorSpeed(int speedPercent) {
  // Map UI speed (0-100%) to actual speed (100-200%)
  int actualSpeed = map(speedPercent, 0, 100, 100, 200);
  int pwmValue = map(actualSpeed, 0, 200, 0, 255);
  ledcWrite(motorENA, pwmValue);
  Serial.print("[MOTOR] Speed adjusted to ");
  Serial.print(speedPercent);
  Serial.print("% (UI) = ");
  Serial.print(actualSpeed);
  Serial.print("% (actual), PWM: ");
  Serial.print(pwmValue);
  Serial.println(")");
}

void setMotorSpeedActual(int actualSpeedPercent) {
  // Set motor speed using actual percentage (0-200%)
  int pwmValue = map(actualSpeedPercent, 0, 200, 0, 255);
  ledcWrite(motorENA, pwmValue);
  Serial.print("[MOTOR] Speed set to ");
  Serial.print(actualSpeedPercent);
  Serial.print("% (actual), PWM: ");
  Serial.print(pwmValue);
  Serial.println(")");
}

void reverseMotor() {
  setMotorDirection(!motorDirection);
}

// Buzzer - melody for overtime notification
const int melodyNotes[] = {523, 659, 784, 1047};  // C5, E5, G5, C6
const int noteDurations[] = {200, 200, 200, 400};
const int noteGap = 50;        // Gap between notes in ms
const int melodyPause = 1500;  // Pause before repeating melody
int currentNote = 0;
unsigned long noteStartTime = 0;
bool inNoteGap = false;
bool inMelodyPause = false;

void startBuzzer() {
  buzzerActive = true;
  currentNote = 0;
  noteStartTime = millis();
  inNoteGap = false;
  inMelodyPause = false;
  tone(buzzerPin, melodyNotes[0]);
}

void stopBuzzer() {
  buzzerActive = false;
  noTone(buzzerPin);
  digitalWrite(buzzerPin, LOW);
}

void updateBuzzer() {
  if (!buzzerActive) return;
  
  unsigned long elapsed = millis() - noteStartTime;
  
  // Handle melody pause (between melody repetitions)
  if (inMelodyPause) {
    if (elapsed >= melodyPause) {
      // Restart melody
      currentNote = 0;
      inMelodyPause = false;
      noteStartTime = millis();
      tone(buzzerPin, melodyNotes[0]);
    }
    return;
  }
  
  // Handle gap between notes
  if (inNoteGap) {
    if (elapsed >= noteGap) {
      inNoteGap = false;
      currentNote++;
      
      // Check if melody finished
      if (currentNote >= 4) {
        // Enter melody pause
        inMelodyPause = true;
        noteStartTime = millis();
        return;
      }
      
      // Play next note
      noteStartTime = millis();
      tone(buzzerPin, melodyNotes[currentNote]);
    }
    return;
  }
  
  // Check if current note finished
  if (elapsed >= noteDurations[currentNote]) {
    noTone(buzzerPin);
    inNoteGap = true;
    noteStartTime = millis();
  }
}

// Load settings from flash
void loadSettings() {
  preferences.begin("filmdev", false);
  settings.devTime = preferences.getInt("devTime", 420);      // 7:00
  settings.stopTime = preferences.getInt("stopTime", 60);     // 1:00
  settings.fixTime = preferences.getInt("fixTime", 300);      // 5:00
  settings.rinseTime = preferences.getInt("rinseTime", 600);  // 10:00
  settings.reverseTime = preferences.getInt("revTime", 10);   // 0:10
  settings.speed = preferences.getInt("speed", 0);  // 0% UI = 100% actual
  settings.overtimeSpeed = preferences.getInt("overtimeSpeed", 5);  // 5% default
  settings.rotateScreen = preferences.getBool("rotateScreen", false);  // false = 0°, true = 180°
  preferences.end();
  
  Serial.println("[SETTINGS] Loaded from flash");
}

// Load WiFi settings from flash
void loadWiFiSettings() {
  preferences.begin("wifi", true);
  wifiConfig.configured = preferences.getBool("configured", false);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("password", "");
  strncpy(wifiConfig.ssid, ssid.c_str(), sizeof(wifiConfig.ssid) - 1);
  strncpy(wifiConfig.password, pass.c_str(), sizeof(wifiConfig.password) - 1);
  preferences.end();
  
  Serial.print("[WIFI] Settings loaded, configured: ");
  Serial.println(wifiConfig.configured ? "yes" : "no");
}

// Save WiFi settings to flash
void saveWiFiSettings() {
  preferences.begin("wifi", false);
  preferences.putBool("configured", wifiConfig.configured);
  preferences.putString("ssid", wifiConfig.ssid);
  preferences.putString("password", wifiConfig.password);
  preferences.end();
  
  Serial.println("[WIFI] Settings saved");
}

// Reset WiFi settings
void resetWiFiSettings() {
  wifiConfig.configured = false;
  memset(wifiConfig.ssid, 0, sizeof(wifiConfig.ssid));
  memset(wifiConfig.password, 0, sizeof(wifiConfig.password));
  saveWiFiSettings();
  
  Serial.println("[WIFI] Settings reset");
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
  preferences.putInt("overtimeSpeed", settings.overtimeSpeed);
  preferences.putBool("rotateScreen", settings.rotateScreen);
  preferences.end();
  
  Serial.println("[SETTINGS] Saved to flash");
}

// HTML page templates
const char* getMenuPageHTML() {
  static char html[1500];
  
  snprintf(html, sizeof(html), R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Film Developer</title>
<style>*{touch-action:manipulation}body{font-family:Arial;background:#000;color:#fff;margin:0;padding:20px;min-height:100vh;display:flex;align-items:center;justify-content:center}.c{max-width:320px;width:100%%;text-align:center}h1{color:#fff;font-size:28px;margin-bottom:10px}.v{color:#888;font-size:14px;margin-bottom:20px}.b{display:block;width:100%%;padding:20px;margin-bottom:15px;border:none;border-radius:8px;color:#fff;font-size:18px;text-align:center;transition:opacity .1s}.b:active{opacity:.7}.g{background:#090}.gr{background:#646464}</style></head>
<body><div class="c"><h1>Film Developer</h1><p class="v">v1.0</p><button class="b g" ontouchend="location='/develop'" onclick="location='/develop'">Start Develop</button><button class="b gr" ontouchend="location='/settings'" onclick="location='/settings'">Settings</button></div></body></html>
)rawliteral");
  
  return html;
}

const char* getDevelopPageHTML() {
  static char html[2800];
  
  snprintf(html, sizeof(html), R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>Develop</title>
<style>*{-webkit-tap-highlight-color:transparent;touch-action:manipulation}body{font-family:Arial;background:#000;color:#fff;margin:0;padding:15px}.c{max-width:320px;margin:0 auto}.st{display:flex;gap:5px;margin-bottom:20px}.st button{flex:1;padding:12px 5px;border:none;border-radius:5px;background:#3c3c3c;color:#fff;font-size:14px}.st button.a{background:#090}.st button:disabled{opacity:.6}.tr{display:flex;align-items:center;justify-content:center;gap:15px;margin:30px 0}.tm{font-size:48px;font-family:monospace;min-width:140px;text-align:center}.tm.ot{color:#f44}.ab{width:50px;height:50px;border:none;border-radius:5px;background:#505050;color:#fff;font-size:24px}.ct{display:flex;gap:10px}.btn{flex:1;padding:15px;border:none;border-radius:5px;color:#fff;font-size:16px}.bk{background:#3c3c3c;flex:0 0 60px}.go{background:#090}.sp{background:#c86400}.rs{background:#900}</style></head>
<body><div class="c"><h1 style="text-align:center;font-size:20px;margin:0 0 15px">Film Development</h1><div class="st" id="st"></div><div class="tr"><button class="ab" id="dn" onclick="adj(-5)">&darr;</button><div class="tm" id="tm">00:00</div><button class="ab" id="up" onclick="adj(5)">&uarr;</button></div><div class="ct" id="ct"></div></div>
<script>let r=0,o=0;const sn=['Dev','Stop','Fix','Rinse'];function u(){fetch('/status').then(x=>x.json()).then(d=>{r=d.running;o=d.overtime;document.getElementById('tm').textContent=d.time;document.getElementById('tm').className='tm'+(o?' ot':'');document.getElementById('dn').style.display=r?'none':'block';document.getElementById('up').style.display=r?'none':'block';let s=document.getElementById('st');s.innerHTML='';sn.forEach((n,i)=>{let b=document.createElement('button');b.className=i===d.stage?'a':'';b.textContent=n;b.disabled=r;b.onclick=()=>ss(i);s.appendChild(b)});let c=document.getElementById('ct');c.innerHTML=r?'<button class="btn sp" style="width:100%%" onclick="sp()">Stop</button>':'<button class="btn bk" onclick="location.href=\'/\'">&larr;</button><button class="btn go" onclick="go()">Start</button><button class="btn rs" onclick="rs()">Reset</button>'})}function go(){fetch('/start-dev').then(u)}function sp(){fetch('/stop').then(u)}function rs(){fetch('/reset').then(u)}function ss(i){fetch('/stage?s='+i).then(u)}function adj(d){fetch('/adjust?d='+d).then(u)}u();setInterval(u,1000)</script></body></html>
)rawliteral");
  
  return html;
}

const char* getConfigPageHTML() {
  static char html[2500];
  
  snprintf(html, sizeof(html), R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>WiFi Setup</title>
<style>*{touch-action:manipulation}body{font-family:Arial;background:#000;color:#fff;margin:0;padding:15px}.c{max-width:320px;margin:0 auto}.bk{color:#888;font-size:16px;margin-bottom:15px;display:inline-block;padding:10px 0}.bk:active{color:#fff}h1{text-align:center;color:#fff;font-size:20px}.fg{margin-bottom:15px}label{display:block;margin-bottom:5px;font-size:14px}input{width:100%%;padding:10px;border:1px solid #444;border-radius:5px;background:#333;color:#fff;box-sizing:border-box}.btn{width:100%%;padding:15px;border:none;border-radius:5px;background:#090;color:#fff;font-size:16px;margin-top:10px}.btn:active{background:#070}.info{text-align:center;color:#888;margin-top:20px;font-size:12px}.cur{background:#333;padding:12px;border-radius:5px;margin-bottom:15px;font-size:14px}</style></head>
<body><div class="c"><span class="bk" ontouchend="location='/settings'" onclick="location='/settings'">&larr; Back to Settings</span><h1>WiFi Setup</h1>
<div class="cur"><p>SSID: <strong>%s</strong></p><p>IP: <strong>%s</strong></p><p>Status: <strong>%s</strong></p></div>
<form action="/save-wifi" method="POST"><div class="fg"><label>WiFi Network (SSID)</label><input type="text" name="ssid" required maxlength="32"></div><div class="fg"><label>Password</label><input type="password" name="password" maxlength="64"></div><button type="submit" class="btn">Save and Connect</button></form>
<div class="info"><p>Device will restart and connect to your WiFi.</p><p>http://filmdeveloper.local</p></div></div></body></html>
)rawliteral", 
    wifiConfig.configured ? wifiConfig.ssid : "Not configured",
    apMode ? WiFi.softAPIP().toString().c_str() : (wifiConnected ? WiFi.localIP().toString().c_str() : "--"),
    wifiConnected ? "Connected" : (apMode ? "AP Mode" : "Disconnected"));
  
  return html;
}

const char* getSettingsPageHTML() {
  static char html[4000];
  
  snprintf(html, sizeof(html), R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>Settings</title>
<style>*{touch-action:manipulation}body{font-family:Arial;background:#000;color:#fff;margin:0;padding:15px}.c{max-width:360px;margin:0 auto}.h{display:flex;align-items:center;justify-content:space-between;margin-bottom:15px}.h h1{margin:0;font-size:20px}.b{color:#888;font-size:18px;padding:10px 15px;margin:-10px -15px}.b:active{color:#fff}.r{display:flex;align-items:center;justify-content:space-between;padding:12px;background:#333;border-radius:8px;margin-bottom:8px}.n{font-size:18px}.ct{display:flex;align-items:center;gap:8px}.v{font-size:22px;font-family:monospace;min-width:65px;text-align:center}button{width:48px;height:48px;border:none;border-radius:8px;background:#505050;color:#fff;font-size:24px}button:active{background:#707070}.sw{position:relative;width:56px;height:32px}.sw input{opacity:0;width:0;height:0}.sl{position:absolute;cursor:pointer;inset:0;background:#555;border-radius:32px;transition:.2s}.sl:before{position:absolute;content:"";height:26px;width:26px;left:3px;bottom:3px;background:#fff;border-radius:50%%;transition:.2s}input:checked+.sl{background:#090}input:checked+.sl:before{transform:translateX(24px)}.s{color:#090;font-size:12px;margin:15px 0 8px;text-transform:uppercase}.w{display:block;text-align:center;margin-top:20px;padding:16px;background:#333;border-radius:8px;color:#090;font-size:18px}.w:active{background:#444}</style></head>
<body><div class="c"><div class="h"><span class="b" ontouchend="location='/'" onclick="location='/'">&larr; Back</span><h1>Settings</h1><div style="width:70px"></div></div>
<div class="s">Stage Times</div>
<div class="r"><span class="n">Developer</span><div class="ct"><button onclick="a('devTime',-5)">-</button><span class="v" id="devTime">%02d:%02d</span><button onclick="a('devTime',5)">+</button></div></div>
<div class="r"><span class="n">Stop Bath</span><div class="ct"><button onclick="a('stopTime',-5)">-</button><span class="v" id="stopTime">%02d:%02d</span><button onclick="a('stopTime',5)">+</button></div></div>
<div class="r"><span class="n">Fixer</span><div class="ct"><button onclick="a('fixTime',-5)">-</button><span class="v" id="fixTime">%02d:%02d</span><button onclick="a('fixTime',5)">+</button></div></div>
<div class="r"><span class="n">Rinse</span><div class="ct"><button onclick="a('rinseTime',-5)">-</button><span class="v" id="rinseTime">%02d:%02d</span><button onclick="a('rinseTime',5)">+</button></div></div>
<div class="s">Motor</div>
<div class="r"><span class="n">Reverse</span><div class="ct"><button onclick="a('reverseTime',-5)">-</button><span class="v" id="reverseTime">%02d:%02d</span><button onclick="a('reverseTime',5)">+</button></div></div>
<div class="r"><span class="n">Speed</span><div class="ct"><button onclick="a('speed',-5)">-</button><span class="v" id="speed">%d%%</span><button onclick="a('speed',5)">+</button></div></div>
<div class="r"><span class="n">OT Speed</span><div class="ct"><button onclick="a('overtimeSpeed',-1)">-</button><span class="v" id="overtimeSpeed">%d%%</span><button onclick="a('overtimeSpeed',1)">+</button></div></div>
<div class="s">Display</div>
<div class="r"><span class="n">Rotate 180</span><label class="sw"><input type="checkbox" id="rot" %s onchange="t()"><span class="sl"></span></label></div>
<div class="w" ontouchend="location='/config'" onclick="location='/config'">Setup WiFi</div></div>
<script>function a(s,d){fetch('/setting-adj?s='+s+'&d='+d).then(r=>r.json()).then(x=>{if(x.value!==undefined){let e=document.getElementById(s);e.textContent=(s==='speed'||s==='overtimeSpeed')?x.value+'%%':String(Math.floor(x.value/60)).padStart(2,'0')+':'+String(x.value%%60).padStart(2,'0')}})}function t(){fetch('/setting-rotate?v='+(document.getElementById('rot').checked?'1':'0'))}</script>
</body></html>
)rawliteral",
    settings.devTime / 60, settings.devTime % 60,
    settings.stopTime / 60, settings.stopTime % 60,
    settings.fixTime / 60, settings.fixTime % 60,
    settings.rinseTime / 60, settings.rinseTime % 60,
    settings.reverseTime / 60, settings.reverseTime % 60,
    settings.speed,
    settings.overtimeSpeed,
    settings.rotateScreen ? "checked" : "");
  
  return html;
}

void sendHTML(const char* html) {
  server.send(200, "text/html", html);
}

// Web server handlers
void handleRoot() {
  if (apMode && !wifiConfig.configured) {
    sendHTML(getConfigPageHTML());
  } else {
    sendHTML(getMenuPageHTML());
  }
}

void handleDevelop() {
  // Send HTML first for fast response
  sendHTML(getDevelopPageHTML());
  
  // Then initialize stage times from settings
  stageTime[DEV] = settings.devTime;
  stageTime[STOP_BATH] = settings.stopTime;
  stageTime[FIX] = settings.fixTime;
  stageTime[RINSE] = settings.rinseTime;
  currentStage = DEV;
  currentTime = stageTime[DEV];
  timerRunning = false;
  isOvertime = false;
  
  // Navigate touchscreen to develop screen (after response sent)
  if (developScreen != NULL) {
    showScreen(developScreen);
    updateTimerDisplay();
    updateStageButtons();
    updateControlButtons();
  }
  
  Serial.println("[WEB] Entered develop screen");
}

void handleConfig() {
  sendHTML(getConfigPageHTML());
}

void handleSaveWiFi() {
  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    strncpy(wifiConfig.ssid, ssid.c_str(), sizeof(wifiConfig.ssid) - 1);
    strncpy(wifiConfig.password, password.c_str(), sizeof(wifiConfig.password) - 1);
    wifiConfig.configured = true;
    saveWiFiSettings();
    
    // Send page with auto-redirect to filmdeveloper.local
    server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WiFi Saved</title>
  <style>
    body { font-family: Arial; background: #1a1a1a; color: white; text-align: center; padding: 50px; }
    .spinner { border: 4px solid #333; border-top: 4px solid #4CAF50; border-radius: 50%; width: 40px; height: 40px; animation: spin 1s linear infinite; margin: 20px auto; }
    @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    a { color: #4CAF50; }
  </style>
</head>
<body>
  <h1>WiFi Saved!</h1>
  <div class="spinner"></div>
  <p>Connecting to your network...</p>
  <p>You will be redirected to <a href="http://filmdeveloper.local">http://filmdeveloper.local</a></p>
  <p style="color:#888;font-size:14px;">If redirect doesn't work, connect to your WiFi network and open the link manually.</p>
  <script>
    setTimeout(function() {
      window.location.href = 'http://filmdeveloper.local';
    }, 10000);
  </script>
</body>
</html>
)rawliteral");
    delay(500);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing SSID");
  }
}

void handleStatus() {
  char json[128];
  char timeStr[16];
  
  if (isOvertime) {
    snprintf(timeStr, sizeof(timeStr), "+%02d:%02d", currentTime / 60, currentTime % 60);
  } else {
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", currentTime / 60, currentTime % 60);
  }
  
  snprintf(json, sizeof(json), 
    "{\"running\":%s,\"overtime\":%s,\"stage\":%d,\"time\":\"%s\"}",
    timerRunning ? "true" : "false",
    isOvertime ? "true" : "false",
    currentStage,
    timeStr);
  
  server.send(200, "application/json", json);
}

void handleStart() {
  server.send(200, "text/plain", "OK");
  
  if (!timerRunning) {
    timerRunning = true;
    isOvertime = false;
    lastSecond = millis();
    startMotor();
    updateControlButtons();
    updateStageButtons();
    updateTimerDisplay();
    Serial.println("[WEB] Timer started");
  }
}

// Start timer from develop screen (web)
void handleStartDev() {
  server.send(200, "text/plain", "OK");
  
  if (!timerRunning) {
    timerRunning = true;
    isOvertime = false;
    lastSecond = millis();
    
    // Navigate touchscreen to develop screen if not already there
    if (developScreen != NULL) {
      showScreen(developScreen);
    }
    
    startMotor();
    updateControlButtons();
    updateStageButtons();
    updateTimerDisplay();
    
    Serial.println("[WEB] Timer started");
  }
}

void handleStop() {
  server.send(200, "text/plain", "OK");
  
  if (timerRunning) {
    timerRunning = false;
    stopMotor();
    stopBuzzer();
    
    if (isOvertime) {
      isOvertime = false;
      if (currentStage < RINSE) {
        currentStage = (Stage)(currentStage + 1);
        currentTime = stageTime[currentStage];
      } else {
        currentTime = stageTime[currentStage];
      }
      updateTimerDisplay();
    }
    
    updateControlButtons();
    updateStageButtons();
    Serial.println("[WEB] Timer stopped");
  }
}

void handleReset() {
  server.send(200, "text/plain", "OK");
  
  if (!timerRunning) {
    isOvertime = false;
    switch(currentStage) {
      case DEV: stageTime[DEV] = settings.devTime; break;
      case STOP_BATH: stageTime[STOP_BATH] = settings.stopTime; break;
      case FIX: stageTime[FIX] = settings.fixTime; break;
      case RINSE: stageTime[RINSE] = settings.rinseTime; break;
    }
    currentTime = stageTime[currentStage];
    updateTimerDisplay();
    Serial.println("[WEB] Timer reset");
  }
}

void handleStage() {
  server.send(200, "text/plain", "OK");
  
  if (!timerRunning && server.hasArg("s")) {
    int stage = server.arg("s").toInt();
    if (stage >= 0 && stage <= 3) {
      currentStage = (Stage)stage;
      // Always load time from settings when switching stages
      switch(currentStage) {
        case DEV: stageTime[DEV] = settings.devTime; break;
        case STOP_BATH: stageTime[STOP_BATH] = settings.stopTime; break;
        case FIX: stageTime[FIX] = settings.fixTime; break;
        case RINSE: stageTime[RINSE] = settings.rinseTime; break;
      }
      currentTime = stageTime[currentStage];
      updateStageButtons();
      updateTimerDisplay();
      Serial.print("[WEB] Stage changed to: ");
      Serial.println(stage);
    }
  }
}

void handleAdjust() {
  server.send(200, "text/plain", "OK");
  
  if (!timerRunning && server.hasArg("d")) {
    int delta = server.arg("d").toInt();
    currentTime += delta;
    if (currentTime < 0) currentTime = 0;
    if (currentTime > 3600) currentTime = 3600;
    stageTime[currentStage] = currentTime;
    updateTimerDisplay();
    Serial.print("[WEB] Time adjusted by: ");
    Serial.println(delta);
  }
}

void handleNotFound() {
  // Captive portal redirect
  if (apMode) {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not Found");
  }
}

void handleSettings() {
  sendHTML(getSettingsPageHTML());
}

void handleSettingAdj() {
  if (server.hasArg("s") && server.hasArg("d")) {
    String setting = server.arg("s");
    int delta = server.arg("d").toInt();
    int newValue = 0;
    
    if (setting == "devTime") {
      settings.devTime += delta;
      if (settings.devTime < 0) settings.devTime = 0;
      if (settings.devTime > 3600) settings.devTime = 3600;
      newValue = settings.devTime;
    } else if (setting == "stopTime") {
      settings.stopTime += delta;
      if (settings.stopTime < 0) settings.stopTime = 0;
      if (settings.stopTime > 600) settings.stopTime = 600;
      newValue = settings.stopTime;
    } else if (setting == "fixTime") {
      settings.fixTime += delta;
      if (settings.fixTime < 0) settings.fixTime = 0;
      if (settings.fixTime > 3600) settings.fixTime = 3600;
      newValue = settings.fixTime;
    } else if (setting == "rinseTime") {
      settings.rinseTime += delta;
      if (settings.rinseTime < 0) settings.rinseTime = 0;
      if (settings.rinseTime > 3600) settings.rinseTime = 3600;
      newValue = settings.rinseTime;
    } else if (setting == "reverseTime") {
      settings.reverseTime += delta;
      if (settings.reverseTime < 0) settings.reverseTime = 0;
      if (settings.reverseTime > 300) settings.reverseTime = 300;
      newValue = settings.reverseTime;
    } else if (setting == "speed") {
      settings.speed += delta;
      if (settings.speed < 0) settings.speed = 0;
      if (settings.speed > 100) settings.speed = 100;
      newValue = settings.speed;
    } else if (setting == "overtimeSpeed") {
      settings.overtimeSpeed += delta;
      if (settings.overtimeSpeed < 1) settings.overtimeSpeed = 1;
      if (settings.overtimeSpeed > 100) settings.overtimeSpeed = 100;
      newValue = settings.overtimeSpeed;
    }
    
    saveSettings();
    updateSettingsLabels();
    
    char json[32];
    snprintf(json, sizeof(json), "{\"value\":%d}", newValue);
    server.send(200, "application/json", json);
    Serial.print("[WEB] Setting ");
    Serial.print(setting);
    Serial.print(" adjusted to: ");
    Serial.println(newValue);
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleSettingRotate() {
  if (server.hasArg("v")) {
    server.send(200, "text/plain", "OK");
    
    settings.rotateScreen = server.arg("v") == "1";
    saveSettings();
    applyScreenRotation();
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    Serial.print("[WEB] Screen rotation set to: ");
    Serial.println(settings.rotateScreen ? "180" : "0");
  } else {
    server.send(400, "text/plain", "Missing parameter");
  }
}

// Setup web server routes
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/develop", handleDevelop);
  server.on("/config", handleConfig);
  server.on("/save-wifi", HTTP_POST, handleSaveWiFi);
  server.on("/status", handleStatus);
  server.on("/start", handleStart);
  server.on("/start-dev", handleStartDev);
  server.on("/stop", handleStop);
  server.on("/reset", handleReset);
  server.on("/stage", handleStage);
  server.on("/adjust", handleAdjust);
  server.on("/settings", handleSettings);
  server.on("/setting-adj", handleSettingAdj);
  server.on("/setting-rotate", handleSettingRotate);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[WEB] Server started");
}

// Start AP mode
void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  apMode = true;
  wifiConnected = false;
  
  // Start DNS server for captive portal
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  
  Serial.print("[WIFI] AP Mode started. SSID: ");
  Serial.println(AP_SSID);
  Serial.print("[WIFI] AP IP: ");
  Serial.println(WiFi.softAPIP());
}

// Connect to WiFi
void connectToWiFi() {
  if (!wifiConfig.configured) {
    startAPMode();
    return;
  }
  
  Serial.print("[WIFI] Connecting to: ");
  Serial.println(wifiConfig.ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiConfig.ssid, wifiConfig.password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    apMode = false;
    
    // Start mDNS
    if (MDNS.begin(MDNS_NAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("[WIFI] mDNS started: http://filmdeveloper.local");
    }
    
    Serial.println();
    Serial.print("[WIFI] Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("[WIFI] Connection failed, starting AP mode");
    startAPMode();
  }
}

// Forward declarations
void createSplashScreen();
void createMainMenuScreen();
void createSettingsScreen1();
void createSettingsScreen2();
void createSettingsScreen3();
void createSettingsScreen4();
void createDevelopScreen();
void showScreen(lv_obj_t *screen);
void updateWiFiStatusLabels();
void updateTimerDisplay();
void updateStageButtons();
void updateControlButtons();
void updateSettingsLabels();
void applyScreenRotation();

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
    isOvertime = false;
    
    showScreen(developScreen);
    updateTimerDisplay();
    updateStageButtons();
    updateControlButtons();
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
lv_obj_t *overtimeSpeedLabel;
lv_obj_t *rotateToggle;

void settingsBackHandler(lv_event_t *e);
void settingsNextHandler(lv_event_t *e);
void settingsPrevHandler(lv_event_t *e);
void settingsValueHandler(lv_event_t *e);
void rotateToggleHandler(lv_event_t *e);

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
  
  if (overtimeSpeedLabel != NULL) {
    sprintf(buf, "%d%%", settings.overtimeSpeed);
    lv_label_set_text(overtimeSpeedLabel, buf);
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
  lv_label_set_text(title, "Settings 1/4");
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
  
  // Settings rows with more spacing
  createSettingRow(settingsScreen1, "Developer", &devTimeLabel, 70, 0);
  createSettingRow(settingsScreen1, "Stop Bath", &stopTimeLabel, 130, 1);
  createSettingRow(settingsScreen1, "Fixer", &fixTimeLabel, 190, 2);
  
  updateSettingsLabels();
}

// Settings Screen 2 (Rinse, Reverse, Speed, Overtime Speed, Rotate Screen)
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
  lv_label_set_text(title, "Settings 2/4");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  
  // Next button (right arrow)
  lv_obj_t *nextBtn = lv_btn_create(settingsScreen2);
  lv_obj_set_size(nextBtn, 60, 40);
  lv_obj_align(nextBtn, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_add_event_cb(nextBtn, settingsNextHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *nextLabel = lv_label_create(nextBtn);
  lv_label_set_text(nextLabel, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(nextLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(nextLabel);
  
  // Settings rows with more spacing
  createSettingRow(settingsScreen2, "Rinse", &rinseTimeLabel, 70, 3);
  createSettingRow(settingsScreen2, "Reverse", &revTimeLabel, 130, 4);
  createSettingRow(settingsScreen2, "Speed", &speedLabel, 190, 5);
  
  updateSettingsLabels();
}

// Settings Screen 3 (OT Speed, Rotate Screen)
void createSettingsScreen3() {
  settingsScreen3 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(settingsScreen3, lv_color_black(), 0);
  
  // Back button (to previous settings page)
  lv_obj_t *backBtn = lv_btn_create(settingsScreen3);
  lv_obj_set_size(backBtn, 60, 40);
  lv_obj_align(backBtn, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_event_cb(backBtn, settingsPrevHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *backLabel = lv_label_create(backBtn);
  lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(backLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(backLabel);
  
  // Title
  lv_obj_t *title = lv_label_create(settingsScreen3);
  lv_label_set_text(title, "Settings 3/4");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  
  // Next button (right arrow)
  lv_obj_t *nextBtn = lv_btn_create(settingsScreen3);
  lv_obj_set_size(nextBtn, 60, 40);
  lv_obj_align(nextBtn, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_add_event_cb(nextBtn, settingsNextHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *nextLabel = lv_label_create(nextBtn);
  lv_label_set_text(nextLabel, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(nextLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(nextLabel);
  
  // Settings rows with more spacing
  createSettingRow(settingsScreen3, "OT Speed", &overtimeSpeedLabel, 70, 6);
  
  // Rotate Screen toggle
  lv_obj_t *rotateLabel = lv_label_create(settingsScreen3);
  lv_label_set_text(rotateLabel, "Rotate 180");
  lv_obj_set_style_text_color(rotateLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(rotateLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_pos(rotateLabel, 10, 130);
  
  // Toggle switch
  rotateToggle = lv_switch_create(settingsScreen3);
  lv_obj_set_size(rotateToggle, 50, 25);
  lv_obj_set_pos(rotateToggle, 260, 128);
  lv_obj_add_event_cb(rotateToggle, rotateToggleHandler, LV_EVENT_VALUE_CHANGED, NULL);
  
  // Set initial state
  if (settings.rotateScreen) {
    lv_obj_add_state(rotateToggle, LV_STATE_CHECKED);
  }
  
  updateSettingsLabels();
}

// WiFi Reset handler
void wifiResetHandler(lv_event_t *e) {
  resetWiFiSettings();
  Serial.println("[WIFI] Settings reset, restarting...");
  
  // Update display before restart
  if (wifiStatusLabel != NULL) {
    lv_label_set_text(wifiStatusLabel, "Resetting...");
  }
  lv_refr_now(NULL);
  
  delay(500);
  ESP.restart();
}

// Update WiFi status labels
void updateWiFiStatusLabels() {
  if (wifiStatusLabel != NULL) {
    if (apMode) {
      lv_label_set_text(wifiStatusLabel, "AP Mode");
    } else if (wifiConnected) {
      lv_label_set_text(wifiStatusLabel, "Connected");
    } else {
      lv_label_set_text(wifiStatusLabel, "Disconnected");
    }
  }
  
  if (wifiSSIDLabel != NULL) {
    if (apMode) {
      char buf[48];
      snprintf(buf, sizeof(buf), "SSID: %s", AP_SSID);
      lv_label_set_text(wifiSSIDLabel, buf);
    } else if (wifiConfig.configured) {
      char buf[48];
      snprintf(buf, sizeof(buf), "SSID: %s", wifiConfig.ssid);
      lv_label_set_text(wifiSSIDLabel, buf);
    } else {
      lv_label_set_text(wifiSSIDLabel, "SSID: Not configured");
    }
  }
  
  if (wifiIPLabel != NULL) {
    char buf[32];
    if (apMode) {
      snprintf(buf, sizeof(buf), "IP: %s", WiFi.softAPIP().toString().c_str());
    } else if (wifiConnected) {
      snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
    } else {
      snprintf(buf, sizeof(buf), "IP: --");
    }
    lv_label_set_text(wifiIPLabel, buf);
  }
}

// Settings Screen 4 (WiFi)
void createSettingsScreen4() {
  settingsScreen4 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(settingsScreen4, lv_color_black(), 0);
  
  // Back button (to previous settings page)
  lv_obj_t *backBtn = lv_btn_create(settingsScreen4);
  lv_obj_set_size(backBtn, 60, 40);
  lv_obj_align(backBtn, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_event_cb(backBtn, settingsPrevHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *backLabel = lv_label_create(backBtn);
  lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(backLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(backLabel);
  
  // Title
  lv_obj_t *title = lv_label_create(settingsScreen4);
  lv_label_set_text(title, "WiFi 4/4");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  
  // WiFi Status
  lv_obj_t *statusTitle = lv_label_create(settingsScreen4);
  lv_label_set_text(statusTitle, "Status:");
  lv_obj_set_style_text_color(statusTitle, lv_color_make(150, 150, 150), 0);
  lv_obj_set_style_text_font(statusTitle, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(statusTitle, 10, 60);
  
  wifiStatusLabel = lv_label_create(settingsScreen4);
  lv_label_set_text(wifiStatusLabel, "...");
  lv_obj_set_style_text_color(wifiStatusLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(wifiStatusLabel, &lv_font_montserrat_18, 0);
  lv_obj_set_pos(wifiStatusLabel, 80, 58);
  
  // WiFi SSID
  wifiSSIDLabel = lv_label_create(settingsScreen4);
  lv_label_set_text(wifiSSIDLabel, "SSID: ...");
  lv_obj_set_style_text_color(wifiSSIDLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(wifiStatusLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_pos(wifiSSIDLabel, 10, 90);
  
  // WiFi IP
  wifiIPLabel = lv_label_create(settingsScreen4);
  lv_label_set_text(wifiIPLabel, "IP: ...");
  lv_obj_set_style_text_color(wifiIPLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(wifiIPLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_pos(wifiIPLabel, 10, 120);
  
  // mDNS info
  lv_obj_t *mdnsLabel = lv_label_create(settingsScreen4);
  lv_label_set_text(mdnsLabel, "http://filmdeveloper.local");
  lv_obj_set_style_text_color(mdnsLabel, lv_color_make(100, 200, 100), 0);
  lv_obj_set_style_text_font(mdnsLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(mdnsLabel, 10, 150);
  
  // Reset WiFi button
  lv_obj_t *resetBtn = lv_btn_create(settingsScreen4);
  lv_obj_set_size(resetBtn, 200, 50);
  lv_obj_align(resetBtn, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_bg_color(resetBtn, lv_color_make(180, 50, 50), 0);
  lv_obj_add_event_cb(resetBtn, wifiResetHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *resetLabel = lv_label_create(resetBtn);
  lv_label_set_text(resetLabel, "Reset WiFi");
  lv_obj_set_style_text_font(resetLabel, &lv_font_montserrat_18, 0);
  lv_obj_center(resetLabel);
  
  updateWiFiStatusLabels();
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
      if (settings.speed < 0) settings.speed = 0;
      if (settings.speed > 100) settings.speed = 100;
      break;
    case 6: // Overtime Speed
      settings.overtimeSpeed += change * 1;
      if (settings.overtimeSpeed < 1) settings.overtimeSpeed = 1;
      if (settings.overtimeSpeed > 100) settings.overtimeSpeed = 100;
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
  lv_obj_t *currentScreen = lv_scr_act();
  
  if (currentScreen == settingsScreen1 && settingsScreen2 != NULL) {
    showScreen(settingsScreen2);
  } else if (currentScreen == settingsScreen2 && settingsScreen3 != NULL) {
    showScreen(settingsScreen3);
  } else if (currentScreen == settingsScreen3 && settingsScreen4 != NULL) {
    updateWiFiStatusLabels();
    showScreen(settingsScreen4);
  }
}

void settingsPrevHandler(lv_event_t *e) {
  lv_obj_t *currentScreen = lv_scr_act();
  
  if (currentScreen == settingsScreen2 && settingsScreen1 != NULL) {
    showScreen(settingsScreen1);
  } else if (currentScreen == settingsScreen3 && settingsScreen2 != NULL) {
    showScreen(settingsScreen2);
  } else if (currentScreen == settingsScreen4 && settingsScreen3 != NULL) {
    showScreen(settingsScreen3);
  }
}

void applyScreenRotation() {
  if (settings.rotateScreen) {
    gfx->setRotation(3);  // 180° rotation (LCD_ROTATION 1 + 2 = 3)
    bsp_touch_init(&touchWire, 3, SCREEN_WIDTH, SCREEN_HEIGHT);  // Reinit touch with 180° rotation
    Serial.println("[SETTINGS] Screen rotated 180°");
  } else {
    gfx->setRotation(1);  // Normal rotation (LCD_ROTATION)
    bsp_touch_init(&touchWire, 1, SCREEN_WIDTH, SCREEN_HEIGHT);  // Reinit touch with normal rotation
    Serial.println("[SETTINGS] Screen rotation normal (0°)");
  }
}

void rotateToggleHandler(lv_event_t *e) {
  lv_obj_t *toggle = lv_event_get_target(e);
  settings.rotateScreen = lv_obj_has_state(toggle, LV_STATE_CHECKED);
  
  // Save settings first
  saveSettings();
  
  // Apply rotation immediately
  applyScreenRotation();
  
  // Force a full screen refresh by invalidating the display
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(NULL);
  
  Serial.println("[SETTINGS] Screen rotation applied and refreshed");
}

// Develop screen handlers
void stageButtonHandler(lv_event_t *e);
void timerUpHandler(lv_event_t *e);
void timerDownHandler(lv_event_t *e);
void startButtonHandler(lv_event_t *e);
void stopButtonHandler(lv_event_t *e);
void resetButtonHandler(lv_event_t *e);
void developBackHandler(lv_event_t *e);

void updateTimerDisplay() {
  char buf[16];
  if (isOvertime) {
    sprintf(buf, "+%02d:%02d", currentTime / 60, currentTime % 60);
  } else {
    sprintf(buf, "%02d:%02d", currentTime / 60, currentTime % 60);
  }
  lv_label_set_text(timerLabel, buf);
  
  // Set color based on overtime state
  if (isOvertime) {
    // Blinking will be handled in loop
  } else {
    lv_obj_set_style_text_color(timerLabel, lv_color_white(), 0);
  }
}

void updateStageButtons() {
  const char* stageNames[] = {"Dev", "Stop", "Fix", "Rinse"};
  
  for (int i = 0; i < 4; i++) {
    if (i == currentStage) {
      lv_obj_set_style_bg_color(stageButtons[i], lv_color_make(0, 150, 0), 0);
    } else {
      lv_obj_set_style_bg_color(stageButtons[i], lv_color_make(60, 60, 60), 0);
    }
    
    // Disable stage buttons when timer is running
    if (timerRunning) {
      lv_obj_add_state(stageButtons[i], LV_STATE_DISABLED);
    } else {
      lv_obj_clear_state(stageButtons[i], LV_STATE_DISABLED);
    }
  }
}

void updateControlButtons() {
  if (timerRunning) {
    // When running: show only Stop button (full width), hide everything else
    lv_obj_add_flag(startBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(resetBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(upBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(downBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(backBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(stopBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(stopBtn, 300, 50);
    lv_obj_set_pos(stopBtn, 10, 180);
  } else {
    // When stopped: show Back, Start and Reset buttons, show up/down buttons
    lv_obj_clear_flag(startBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(resetBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(upBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(downBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(backBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(stopBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(startBtn, 115, 50);
    lv_obj_set_pos(startBtn, 80, 180);
    lv_obj_set_size(resetBtn, 115, 50);
    lv_obj_set_pos(resetBtn, 205, 180);
  }
}

void createDevelopScreen() {
  developScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(developScreen, lv_color_black(), 0);
  
  // Stage buttons (top row)
  const char* stageNames[] = {"Dev", "Stop", "Fix", "Rinse"};
  int btnWidth = 75;
  int btnSpacing = 5;
  int startX = (SCREEN_WIDTH - (btnWidth * 4 + btnSpacing * 3)) / 2;
  
  for (int i = 0; i < 4; i++) {
    stageButtons[i] = lv_btn_create(developScreen);
    lv_obj_set_size(stageButtons[i], btnWidth, 45);
    lv_obj_set_pos(stageButtons[i], startX + i * (btnWidth + btnSpacing), 10);
    lv_obj_add_event_cb(stageButtons[i], stageButtonHandler, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    
    lv_obj_t *label = lv_label_create(stageButtons[i]);
    lv_label_set_text(label, stageNames[i]);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
  }
  
  // Timer display with up/down buttons (centered vertically)
  // Down button (left side)
  downBtn = lv_btn_create(developScreen);
  lv_obj_set_size(downBtn, 50, 50);
  lv_obj_set_pos(downBtn, 40, 95);
  lv_obj_set_style_bg_color(downBtn, lv_color_make(80, 80, 80), 0);
  lv_obj_add_event_cb(downBtn, timerDownHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *downLabel = lv_label_create(downBtn);
  lv_label_set_text(downLabel, LV_SYMBOL_DOWN);
  lv_obj_set_style_text_font(downLabel, &lv_font_montserrat_28, 0);
  lv_obj_center(downLabel);
  
  // Timer label (larger font)
  timerLabel = lv_label_create(developScreen);
  lv_label_set_text(timerLabel, "00:00");
  lv_obj_set_style_text_color(timerLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(timerLabel, &lv_font_montserrat_48, 0);
  lv_obj_align(timerLabel, LV_ALIGN_CENTER, 0, 0);
  
  // Up button (right side)
  upBtn = lv_btn_create(developScreen);
  lv_obj_set_size(upBtn, 50, 50);
  lv_obj_set_pos(upBtn, 230, 95);
  lv_obj_set_style_bg_color(upBtn, lv_color_make(80, 80, 80), 0);
  lv_obj_add_event_cb(upBtn, timerUpHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *upLabel = lv_label_create(upBtn);
  lv_label_set_text(upLabel, LV_SYMBOL_UP);
  lv_obj_set_style_text_font(upLabel, &lv_font_montserrat_28, 0);
  lv_obj_center(upLabel);
  
  // Control buttons (bottom row)
  // Back button (bottom left)
  backBtn = lv_btn_create(developScreen);
  lv_obj_set_size(backBtn, 60, 50);
  lv_obj_set_pos(backBtn, 10, 180);
  lv_obj_set_style_bg_color(backBtn, lv_color_make(60, 60, 60), 0);
  lv_obj_add_event_cb(backBtn, developBackHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *backLabel = lv_label_create(backBtn);
  lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(backLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(backLabel);
  
  // Start button
  startBtn = lv_btn_create(developScreen);
  lv_obj_set_size(startBtn, 75, 50);
  lv_obj_set_pos(startBtn, 80, 180);
  lv_obj_set_style_bg_color(startBtn, lv_color_make(0, 150, 0), 0);
  lv_obj_add_event_cb(startBtn, startButtonHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *startLabel = lv_label_create(startBtn);
  lv_label_set_text(startLabel, "Start");
  lv_obj_set_style_text_font(startLabel, &lv_font_montserrat_18, 0);
  lv_obj_center(startLabel);
  
  // Stop button
  stopBtn = lv_btn_create(developScreen);
  lv_obj_set_size(stopBtn, 75, 50);
  lv_obj_set_pos(stopBtn, 165, 180);
  lv_obj_set_style_bg_color(stopBtn, lv_color_make(200, 100, 0), 0);
  lv_obj_add_event_cb(stopBtn, stopButtonHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *stopLabel = lv_label_create(stopBtn);
  lv_label_set_text(stopLabel, "Stop");
  lv_obj_set_style_text_font(stopLabel, &lv_font_montserrat_18, 0);
  lv_obj_center(stopLabel);
  
  // Reset button
  resetBtn = lv_btn_create(developScreen);
  lv_obj_set_size(resetBtn, 60, 50);
  lv_obj_set_pos(resetBtn, 250, 180);
  lv_obj_set_style_bg_color(resetBtn, lv_color_make(150, 0, 0), 0);
  lv_obj_add_event_cb(resetBtn, resetButtonHandler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *resetLabel = lv_label_create(resetBtn);
  lv_label_set_text(resetLabel, "Reset");
  lv_obj_set_style_text_font(resetLabel, &lv_font_montserrat_18, 0);
  lv_obj_center(resetLabel);
  
  updateStageButtons();
  updateTimerDisplay();
  updateControlButtons();
}

void stageButtonHandler(lv_event_t *e) {
  int stage = (int)(intptr_t)lv_event_get_user_data(e);
  currentStage = (Stage)stage;
  // Always load time from settings when switching stages
  switch(currentStage) {
    case DEV: stageTime[DEV] = settings.devTime; break;
    case STOP_BATH: stageTime[STOP_BATH] = settings.stopTime; break;
    case FIX: stageTime[FIX] = settings.fixTime; break;
    case RINSE: stageTime[RINSE] = settings.rinseTime; break;
  }
  currentTime = stageTime[currentStage];
  updateStageButtons();
  updateTimerDisplay();
  Serial.print("[DEVELOP] Stage changed to: ");
  Serial.println(stage);
}

void timerUpHandler(lv_event_t *e) {
  if (!timerRunning) {
    currentTime += 5;
    if (currentTime > 3600) currentTime = 3600;
    stageTime[currentStage] = currentTime;
    updateTimerDisplay();
  }
}

void timerDownHandler(lv_event_t *e) {
  if (!timerRunning) {
    currentTime -= 5;
    if (currentTime < 0) currentTime = 0;
    stageTime[currentStage] = currentTime;
    updateTimerDisplay();
  }
}

void startButtonHandler(lv_event_t *e) {
  timerRunning = true;
  isOvertime = false;
  lastSecond = millis();
  startMotor();
  updateControlButtons();
  updateStageButtons();
  updateTimerDisplay();
  Serial.println("[DEVELOP] Timer started, motor ON");
}

void stopButtonHandler(lv_event_t *e) {
  timerRunning = false;
  stopMotor();
  stopBuzzer();
  
  // If in overtime, advance to next stage
  if (isOvertime) {
    isOvertime = false;
    
    // Advance to next stage
    if (currentStage < RINSE) {
      currentStage = (Stage)(currentStage + 1);
      currentTime = stageTime[currentStage];
      Serial.print("[DEVELOP] Advanced to next stage: ");
      Serial.println(currentStage);
    } else {
      // Already at last stage, just reset
      currentTime = stageTime[currentStage];
      Serial.println("[DEVELOP] At final stage, reset timer");
    }
    
    updateTimerDisplay();
  }
  
  updateControlButtons();
  updateStageButtons();
  Serial.println("[DEVELOP] Timer stopped, motor OFF");
}

void resetButtonHandler(lv_event_t *e) {
  timerRunning = false;
  isOvertime = false;
  stopMotor();
  
  // Reset current stage time to default from settings
  switch(currentStage) {
    case DEV:
      stageTime[DEV] = settings.devTime;
      break;
    case STOP_BATH:
      stageTime[STOP_BATH] = settings.stopTime;
      break;
    case FIX:
      stageTime[FIX] = settings.fixTime;
      break;
    case RINSE:
      stageTime[RINSE] = settings.rinseTime;
      break;
  }
  
  currentTime = stageTime[currentStage];
  updateTimerDisplay();
  updateControlButtons();
  updateStageButtons();
  Serial.println("[DEVELOP] Timer reset to default");
}

void developBackHandler(lv_event_t *e) {
  timerRunning = false;
  stopMotor();
  if (mainMenuScreen != NULL) {
    showScreen(mainMenuScreen);
  }
}

// Physical button handler
void handlePhysicalButton() {
  lv_obj_t *currentScreen = lv_scr_act();
  
  // 1. Main menu - jump to Start Develop
  if (currentScreen == mainMenuScreen) {
    Serial.println("[BUTTON] Main menu -> Start Develop");
    mainMenuStartHandler(NULL);
    return;
  }
  
  // 2-5. Develop screen
  if (currentScreen == developScreen) {
    if (!timerRunning) {
      // 2. Timer not started - trigger Start
      Serial.println("[BUTTON] Develop screen -> Start timer");
      startButtonHandler(NULL);
    } else if (isOvertime) {
      // 4 & 5. During overtime - trigger Stop and advance
      Serial.println("[BUTTON] Overtime -> Stop and advance");
      
      timerRunning = false;
      stopMotor();
      stopBuzzer();
      isOvertime = false;
      
      // Advance to next stage or reset to Dev
      if (currentStage < RINSE) {
        // Not at last stage - advance to next
        currentStage = (Stage)(currentStage + 1);
        currentTime = stageTime[currentStage];
        Serial.print("[BUTTON] Advanced to next stage: ");
        Serial.println(currentStage);
      } else {
        // 5. At Rinse stage - jump back to Dev
        currentStage = DEV;
        currentTime = stageTime[DEV];
        Serial.println("[BUTTON] At Rinse, jumped back to Dev");
      }
      
      updateTimerDisplay();
      updateControlButtons();
      updateStageButtons();
    } else {
      // 3. Timer running (not overtime) - trigger Stop
      Serial.println("[BUTTON] Timer running -> Stop");
      stopButtonHandler(NULL);
    }
    return;
  }
}

// Show screen
void showScreen(lv_obj_t *screen) {
  lv_scr_load(screen);
}

void setup() {
  Serial.begin(115200);
  delay(100);  // Let serial stabilize
  Serial.println("Film Developer Starting...");

  // Buzzer - initialize as output and ensure it's silent
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  noTone(buzzerPin);

  // Physical button - initialize with internal pull-up
  pinMode(buttonPin, INPUT_PULLUP);
  Serial.println("[BUTTON] Physical button initialized on pin 9");

  // Configure PWM for motor control (ESP32 Arduino 3.x API)
  ledcAttach(motorENA, pwmFreq, pwmResolution);
  
  // Motor direction pins
  pinMode(motorIN1, OUTPUT);
  pinMode(motorIN2, OUTPUT);
  stopMotor();

  // Initialize display
  gfx->begin();
  gfx->fillScreen(BLACK);

#ifdef PIN_NUM_LCD_BL
  pinMode(PIN_NUM_LCD_BL, OUTPUT);
  digitalWrite(PIN_NUM_LCD_BL, HIGH);
#endif

  // Load settings first (before initializing touch/display rotation)
  loadSettings();
  loadWiFiSettings();

  // Initialize touch with saved rotation
  touchWire.begin(TOUCH_SDA, TOUCH_SCL);
  int touchRotation = settings.rotateScreen ? 3 : TOUCH_ROTATION;
  bsp_touch_init(&touchWire, touchRotation, SCREEN_WIDTH, SCREEN_HEIGHT);
  
  // Apply screen rotation
  if (settings.rotateScreen) {
    gfx->setRotation(3);  // 180° rotation
  }

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

  // Create all screens
  createSplashScreen();
  createMainMenuScreen();
  createSettingsScreen1();
  createSettingsScreen2();
  createSettingsScreen3();
  createSettingsScreen4();
  createDevelopScreen();

  // Show splash screen
  showScreen(splashScreen);
  
  // Initialize WiFi
  connectToWiFi();
  setupWebServer();
  
  // Auto-transition to main menu after 3 seconds
  lv_timer_t *timer = lv_timer_create([](lv_timer_t *timer) {
    showScreen(mainMenuScreen);
    lv_timer_del(timer);
  }, 3000, NULL);

  Serial.println("Setup complete");
}

void loop() {
  // Handle web server requests first for faster response
  server.handleClient();
  
  // Handle DNS for captive portal in AP mode
  if (apMode) {
    dnsServer.processNextRequest();
  }
  
  // Then handle LVGL
  lv_timer_handler();
  
  // Check physical button (active LOW with pull-up)
  if (digitalRead(buttonPin) == LOW) {
    unsigned long now = millis();
    if (now - lastButtonPress > debounceDelay) {
      lastButtonPress = now;
      handlePhysicalButton();
      Serial.println("[BUTTON] Button pressed");
    }
  }
  
  // Handle countdown timer
  if (timerRunning) {
    unsigned long now = millis();
    
    // Update timer every second
    if (now - lastSecond >= 1000) {
      lastSecond = now;
      
      if (!isOvertime) {
        // Normal countdown
        currentTime--;
        reverseCounter++;
        updateTimerDisplay();
        
        // Check if it's time to reverse motor direction
        if (reverseCounter >= settings.reverseTime) {
          reverseMotor();
          reverseCounter = 0;
        }
        
        if (currentTime == 0) {
          // Enter overtime mode
          isOvertime = true;
          currentTime = 0;
          // Map OT Speed UI (1-100%) to actual speed (100-200%) same as normal speed
          int actualOvertimeSpeed = map(settings.overtimeSpeed, 0, 100, 100, 200);
          setMotorSpeedActual(actualOvertimeSpeed);
          startBuzzer();
          Serial.print("[DEVELOP] Timer reached 00:00, entering overtime mode at ");
          Serial.print(settings.overtimeSpeed);
          Serial.print("% (UI) = ");
          Serial.print(actualOvertimeSpeed);
          Serial.println("% (actual)");
        }
      } else {
        // Overtime count up
        currentTime++;
        reverseCounter++;
        updateTimerDisplay();
        
        // Continue reversing in overtime mode
        if (reverseCounter >= settings.reverseTime) {
          reverseMotor();
          reverseCounter = 0;
        }
      }
    }
    
    // Handle blinking in overtime mode
    if (isOvertime) {
      if (now - lastBlink >= 500) {
        lastBlink = now;
        // Toggle between red and white
        static bool isRed = false;
        if (isRed) {
          lv_obj_set_style_text_color(timerLabel, lv_color_make(255, 0, 0), 0);
        } else {
          lv_obj_set_style_text_color(timerLabel, lv_color_white(), 0);
        }
        isRed = !isRed;
      }
    }
  }
  
  // Update buzzer
  updateBuzzer();
  
  delay(5);
}
