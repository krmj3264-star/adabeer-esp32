#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

// ===== LilyGo T-HMI V1.2 Official Pins =====
#define PWR_ON_PIN  10
#define BTN_SELECT   0
#define BTN_BACK    21

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_Parallel8 _bus;
  lgfx::Light_PWM     _light;
public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.freq_write = 20000000;
      cfg.pin_wr = 8;
      cfg.pin_rd = -1;
      cfg.pin_rs = 7;
      cfg.pin_d0 = 48;
      cfg.pin_d1 = 47;
      cfg.pin_d2 = 39;
      cfg.pin_d3 = 40;
      cfg.pin_d4 = 41;
      cfg.pin_d5 = 42;
      cfg.pin_d6 = 45;
      cfg.pin_d7 = 46;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 6;
      cfg.pin_rst  = -1;
      cfg.pin_busy = -1;
      cfg.panel_width  = 240;
      cfg.panel_height = 320;
      cfg.offset_rotation = 1;
      cfg.invert     = true;
      cfg.readable   = false;
      cfg.rgb_order  = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl      = 38;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

LGFX tft;

// ===== Portal =====
const byte DNS_PORT = 53;
WebServer   server(80);
DNSServer   dnsServer;
Preferences prefs;
USBHIDKeyboard Keyboard;

// ===== Password Storage =====
const int MAX_PASSWORDS = 20;
char passwordNames[MAX_PASSWORDS][32];
char usernames[MAX_PASSWORDS][64];
char passwords[MAX_PASSWORDS][64];
int  passwordCount = 0;

// ===== Menu =====
enum ScreenState { SCREEN_MAIN, SCREEN_PASSWORDS, SCREEN_ACTIONS, SCREEN_SETTINGS };
ScreenState currentScreen  = SCREEN_MAIN;
int selectedIndex          = 0;
int activePasswordIndex    = 0;

// ===== Portal =====
bool   portalRunning = false;
String apPassword    = "12345678";

// ===== Button State =====
bool btnPressed    = false;
bool holdTriggered = false;
unsigned long pressStart = 0;
const unsigned long HOLD_MS = 700;

// =====================================================
// LOAD / SAVE
// =====================================================
void loadPasswords() {
  prefs.begin("vault", true);
  passwordCount = prefs.getInt("count", 0);
  if (passwordCount > MAX_PASSWORDS) passwordCount = MAX_PASSWORDS;
  for (int i = 0; i < passwordCount; i++) {
    prefs.getString(("n"+String(i)).c_str(), "").toCharArray(passwordNames[i], 32);
    prefs.getString(("u"+String(i)).c_str(), "").toCharArray(usernames[i], 64);
    prefs.getString(("p"+String(i)).c_str(), "").toCharArray(passwords[i], 64);
  }
  prefs.end();
}

void savePasswords() {
  prefs.begin("vault", false);
  prefs.clear();
  prefs.putInt("count", passwordCount);
  for (int i = 0; i < passwordCount; i++) {
    prefs.putString(("n"+String(i)).c_str(), passwordNames[i]);
    prefs.putString(("u"+String(i)).c_str(), usernames[i]);
    prefs.putString(("p"+String(i)).c_str(), passwords[i]);
  }
  prefs.end();
}

// =====================================================
// DRAW MENU
// =====================================================
void drawMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);

  if (currentScreen == SCREEN_MAIN) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(20, 10);
    tft.println("Dr.Passwords");
    const char* items[] = {"Passwords", "Settings"};
    for (int i = 0; i < 2; i++) {
      if (i == selectedIndex) tft.setTextColor(TFT_BLACK, TFT_GREEN);
      else tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(20, 55 + i*35);
      tft.println(items[i]);
    }
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(20, 130);
    tft.println("Click=scroll  Hold=select");
    tft.setCursor(20, 145);
    tft.println("BACK=go back / Settings");
  }

  else if (currentScreen == SCREEN_PASSWORDS) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(20, 10);
    tft.println("Passwords");
    if (passwordCount == 0) {
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.setCursor(20, 70); tft.println("No passwords yet");
      tft.setCursor(20, 90); tft.println("Go to Settings to add");
    }
    for (int i = 0; i < passwordCount; i++) {
      if (i == selectedIndex) tft.setTextColor(TFT_BLACK, TFT_GREEN);
      else tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(20, 50 + i*28);
      tft.println(passwordNames[i]);
    }
  }

  else if (currentScreen == SCREEN_ACTIONS) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(20, 10);
    tft.println(passwordNames[activePasswordIndex]);
    tft.drawFastHLine(0, 38, 240, TFT_DARKGREY);
    const char* actions[] = {"Send Username", "Send Password", "User+Tab+Pass"};
    for (int i = 0; i < 3; i++) {
      if (i == selectedIndex) tft.setTextColor(TFT_BLACK, TFT_GREEN);
      else tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(20, 55 + i*35);
      tft.println(actions[i]);
    }
  }

  else if (currentScreen == SCREEN_SETTINGS) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(20, 10);
    tft.println("WiFi Portal");
    tft.drawFastHLine(0, 38, 240, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(20, 50); tft.println("SSID: Dr.Passwords");
    tft.setCursor(20, 78); tft.print("PASS: "); tft.println(apPassword);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(20, 115); tft.println("Connect WiFi then open browser");
    tft.setCursor(20, 130); tft.println("Hold BACK button to close portal");
  }
}

// =====================================================
// SEND
// =====================================================
void sendSelected() {
  if (currentScreen != SCREEN_ACTIONS) return;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 60);
  tft.println("Sending...");
  delay(300);
  if (selectedIndex == 0) {
    Keyboard.print(usernames[activePasswordIndex]);
  } else if (selectedIndex == 1) {
    Keyboard.print(passwords[activePasswordIndex]);
  } else if (selectedIndex == 2) {
    Keyboard.print(usernames[activePasswordIndex]);
    Keyboard.write(KEY_TAB);
    Keyboard.print(passwords[activePasswordIndex]);
  }
  delay(500);
  drawMenu();
}

// =====================================================
// PORTAL
// =====================================================
void startPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Dr.Passwords", apPassword.c_str());
  delay(300);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/", []() {
    String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Dr.Passwords</title>";
    html += "<style>body{background:#111;color:#fff;font-family:Arial;padding:20px;}";
    html += "input{width:100%;padding:10px;background:#222;color:#fff;border:1px solid #444;border-radius:6px;box-sizing:border-box;margin-bottom:10px;}";
    html += ".btn{padding:10px 20px;background:#4d8ef7;color:#fff;border:none;border-radius:6px;cursor:pointer;width:100%;font-size:16px;margin-bottom:8px;}";
    html += ".del{background:#c0392b;color:#fff;border:none;border-radius:4px;padding:6px 12px;cursor:pointer;}";
    html += "table{width:100%;border-collapse:collapse;margin-top:20px;}";
    html += "th,td{padding:10px;border:1px solid #333;text-align:left;}th{background:#1a1a1a;}</style></head><body>";
    html += "<h2>Dr. Passwords</h2>";
    html += "<form method='POST' action='/save'>";
    html += "<input name='n' placeholder='Name (e.g. Gmail)'>";
    html += "<input name='u' placeholder='Username or Email'>";
    html += "<input name='p' placeholder='Password' id='pw'>";
    html += "<button type='button' class='btn' style='background:#555;' onclick=\"var c='ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$';var s='';for(var i=0;i<16;i++)s+=c[Math.floor(Math.random()*c.length)];document.getElementById('pw').value=s;\">Generate Password</button>";
    html += "<button class='btn' type='submit'>Add Password</button></form>";
    if (passwordCount > 0) {
      html += "<table><tr><th>Name</th><th>Username</th><th></th></tr>";
      for (int i = 0; i < passwordCount; i++) {
        html += "<tr><td>" + String(passwordNames[i]) + "</td>";
        html += "<td>" + String(usernames[i]) + "</td>";
        html += "<td><a href='/del?i=" + String(i) + "'><button class='del'>Del</button></a></td></tr>";
      }
      html += "</table>";
    }
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    if (passwordCount >= MAX_PASSWORDS) { server.sendHeader("Location", "/"); server.send(302); return; }
    String nm = server.arg("n"); nm.trim();
    String us = server.arg("u"); us.trim();
    String pw = server.arg("p"); pw.trim();
    if (nm.length() == 0) { server.sendHeader("Location", "/"); server.send(302); return; }
    nm.toCharArray(passwordNames[passwordCount], 32);
    us.toCharArray(usernames[passwordCount], 64);
    pw.toCharArray(passwords[passwordCount], 64);
    passwordCount++;
    savePasswords();
    server.sendHeader("Location", "/");
    server.send(302);
  });

  server.on("/del", []() {
    if (!server.hasArg("i")) { server.sendHeader("Location", "/"); server.send(302); return; }
    int i = server.arg("i").toInt();
    if (i < 0 || i >= passwordCount) { server.sendHeader("Location", "/"); server.send(302); return; }
    for (int j = i; j < passwordCount - 1; j++) {
      strcpy(passwordNames[j], passwordNames[j+1]);
      strcpy(usernames[j], usernames[j+1]);
      strcpy(passwords[j], passwords[j+1]);
    }
    passwordCount--;
    if (selectedIndex >= passwordCount && passwordCount > 0) selectedIndex = passwordCount - 1;
    savePasswords();
    server.sendHeader("Location", "/");
    server.send(302);
  });

  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302); });
  server.begin();
  portalRunning = true;
  drawMenu();
}

void stopPortal() {
  if (!portalRunning) return;
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  portalRunning = false;
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  // Power ON - مهم جداً للوحة T-HMI V1.2
  pinMode(PWR_ON_PIN, OUTPUT);
  digitalWrite(PWR_ON_PIN, HIGH);
  delay(100);

  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK,   INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.setBrightness(255);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(3);
  tft.setCursor(20, 50);
  tft.println("Dr.Passwords");
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(20, 95);
  tft.println("Loading...");
  delay(1200);

  loadPasswords();
  drawMenu();

  Keyboard.begin();
  USB.begin();
  delay(800);
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  if (portalRunning) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  bool btn = digitalRead(BTN_SELECT);

  if (btn == LOW) {
    if (!btnPressed) {
      btnPressed    = true;
      pressStart    = millis();
      holdTriggered = false;
    } else if (!holdTriggered && millis() - pressStart > HOLD_MS) {
      holdTriggered = true;
      if (currentScreen == SCREEN_ACTIONS) {
        sendSelected();
      } else if (currentScreen == SCREEN_MAIN) {
        if (selectedIndex == 0) {
          currentScreen = SCREEN_PASSWORDS;
          selectedIndex = 0;
          drawMenu();
        }
      } else if (currentScreen == SCREEN_PASSWORDS && passwordCount > 0) {
        activePasswordIndex = selectedIndex;
        currentScreen = SCREEN_ACTIONS;
        selectedIndex = 0;
        drawMenu();
      }
    }
  } else {
    if (btnPressed && !holdTriggered) {
      int maxItems = 0;
      if (currentScreen == SCREEN_MAIN)           maxItems = 2;
      else if (currentScreen == SCREEN_PASSWORDS)  maxItems = max(1, passwordCount);
      else if (currentScreen == SCREEN_ACTIONS)    maxItems = 3;
      if (maxItems > 0) {
        selectedIndex = (selectedIndex + 1) % maxItems;
        drawMenu();
      }
    }
    btnPressed    = false;
    holdTriggered = false;
  }

  // BACK button
  if (digitalRead(BTN_BACK) == LOW) {
    delay(50);
    if (digitalRead(BTN_BACK) == LOW) {
      unsigned long backStart = millis();
      while (digitalRead(BTN_BACK) == LOW) delay(10);
      unsigned long held = millis() - backStart;

      if (portalRunning) {
        stopPortal();
        currentScreen = SCREEN_MAIN;
        selectedIndex = 0;
        drawMenu();
      } else if (currentScreen == SCREEN_MAIN) {
        currentScreen = SCREEN_SETTINGS;
        selectedIndex = 0;
        startPortal();
      } else if (currentScreen == SCREEN_PASSWORDS) {
        currentScreen = SCREEN_MAIN;
        selectedIndex = 0;
        drawMenu();
      } else if (currentScreen == SCREEN_ACTIONS) {
        currentScreen = SCREEN_PASSWORDS;
        selectedIndex = activePasswordIndex;
        drawMenu();
      }
    }
  }

  delay(20);
}
