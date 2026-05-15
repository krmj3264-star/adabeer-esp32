#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

// ===== LilyGo T-HMI V1.2 Official Pins =====
#define PWR_EN_PIN   10
#define PWR_ON_PIN   14
#define BK_LIGHT_PIN 38
#define BTN_SELECT    0
#define BTN_BACK     21

TFT_eSPI tft = TFT_eSPI();

// ===== Backlight =====
void setBrightness(uint8_t value) {
  static uint8_t steps = 16;
  static uint8_t _brightness = 0;
  if (_brightness == value) return;
  if (value > 16) value = 16;
  if (value == 0) {
    digitalWrite(BK_LIGHT_PIN, 0);
    delay(3);
    _brightness = 0;
    return;
  }
  if (_brightness == 0) {
    digitalWrite(BK_LIGHT_PIN, 1);
    _brightness = steps;
    delayMicroseconds(30);
  }
  int from = steps - _brightness;
  int to   = steps - value;
  int num  = (steps + to - from) % steps;
  for (int i = 0; i < num; i++) {
    digitalWrite(BK_LIGHT_PIN, 0);
    digitalWrite(BK_LIGHT_PIN, 1);
  }
  _brightness = value;
}

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

bool   portalRunning = false;
String apPassword    = "12345678";

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
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Dr.Passwords", 20, 10, 2);
    const char* items[] = {"Passwords", "Settings"};
    for (int i = 0; i < 2; i++) {
      if (i == selectedIndex) tft.setTextColor(TFT_BLACK, TFT_GREEN);
      else tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(items[i], 20, 55 + i*35, 2);
    }
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Click=scroll  Hold=select", 20, 130, 1);
    tft.drawString("BACK=back / Settings", 20, 145, 1);
  }

  else if (currentScreen == SCREEN_PASSWORDS) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Passwords", 20, 10, 2);
    if (passwordCount == 0) {
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.drawString("No passwords yet", 20, 70, 1);
      tft.drawString("Go to Settings to add", 20, 90, 1);
    }
    for (int i = 0; i < passwordCount; i++) {
      if (i == selectedIndex) tft.setTextColor(TFT_BLACK, TFT_GREEN);
      else tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(passwordNames[i], 20, 50 + i*28, 2);
    }
  }

  else if (currentScreen == SCREEN_ACTIONS) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(passwordNames[activePasswordIndex], 20, 10, 2);
    tft.drawFastHLine(0, 38, 240, TFT_DARKGREY);
    const char* actions[] = {"Send Username", "Send Password", "User+Tab+Pass"};
    for (int i = 0; i < 3; i++) {
      if (i == selectedIndex) tft.setTextColor(TFT_BLACK, TFT_GREEN);
      else tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(actions[i], 20, 55 + i*35, 2);
    }
  }

  else if (currentScreen == SCREEN_SETTINGS) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("WiFi Portal", 20, 10, 2);
    tft.drawFastHLine(0, 38, 240, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("SSID: Dr.Passwords", 20, 50, 2);
    tft.drawString("PASS: " + apPassword, 20, 78, 2);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Connect WiFi then open", 20, 115, 1);
    tft.drawString("Hold BACK to close portal", 20, 130, 1);
  }
}

// =====================================================
// SEND
// =====================================================
void sendSelected() {
  if (currentScreen != SCREEN_ACTIONS) return;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Sending...", 20, 60, 2);
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
    html += "<input name='n' placeholder='Name'>";
    html += "<input name='u' placeholder='Username or Email'>";
    html += "<input name='p' placeholder='Password' id='pw'>";
    html += "<button type='button' class='btn' style='background:#555;' onclick=\"var c='ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$';var s='';for(var i=0;i<16;i++)s+=c[Math.floor(Math.random()*c.length)];document.getElementById('pw').value=s;\">Generate</button>";
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
    server.sendHeader("Location", "/"); server.send(302);
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
    server.sendHeader("Location", "/"); server.send(302);
  });

  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302); });
  server.begin();
  portalRunning = true;
  drawMenu();
}

void stopPortal() {
  if (!portalRunning) return;
  server.stop(); dnsServer.stop();
  WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
  portalRunning = false;
}

// =====================================================
// SETUP - نفس ترتيب LilyGo الرسمي
// =====================================================
void setup() {
  // Power ON - نفس الترتيب الرسمي من LilyGo
  pinMode(PWR_EN_PIN, OUTPUT);
  digitalWrite(PWR_EN_PIN, HIGH);
  delay(100);

  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK,   INPUT_PULLUP);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  setBrightness(16);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Dr.Passwords", 20, 50, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Loading...", 20, 95, 1);
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
      btnPressed = true; pressStart = millis(); holdTriggered = false;
    } else if (!holdTriggered && millis() - pressStart > HOLD_MS) {
      holdTriggered = true;
      if (currentScreen == SCREEN_ACTIONS) {
        sendSelected();
      } else if (currentScreen == SCREEN_MAIN && selectedIndex == 0) {
        currentScreen = SCREEN_PASSWORDS; selectedIndex = 0; drawMenu();
      } else if (currentScreen == SCREEN_PASSWORDS && passwordCount > 0) {
        activePasswordIndex = selectedIndex;
        currentScreen = SCREEN_ACTIONS; selectedIndex = 0; drawMenu();
      }
    }
  } else {
    if (btnPressed && !holdTriggered) {
      int maxItems = 0;
      if (currentScreen == SCREEN_MAIN)           maxItems = 2;
      else if (currentScreen == SCREEN_PASSWORDS)  maxItems = max(1, passwordCount);
      else if (currentScreen == SCREEN_ACTIONS)    maxItems = 3;
      if (maxItems > 0) { selectedIndex = (selectedIndex+1) % maxItems; drawMenu(); }
    }
    btnPressed = false; holdTriggered = false;
  }

  if (digitalRead(BTN_BACK) == LOW) {
    delay(50);
    if (digitalRead(BTN_BACK) == LOW) {
      while (digitalRead(BTN_BACK) == LOW) delay(10);
      if (portalRunning) {
        stopPortal(); currentScreen = SCREEN_MAIN; selectedIndex = 0; drawMenu();
      } else if (currentScreen == SCREEN_MAIN) {
        currentScreen = SCREEN_SETTINGS; selectedIndex = 0; startPortal();
      } else if (currentScreen == SCREEN_PASSWORDS) {
        currentScreen = SCREEN_MAIN; selectedIndex = 0; drawMenu();
      } else if (currentScreen == SCREEN_ACTIONS) {
        currentScreen = SCREEN_PASSWORDS; selectedIndex = activePasswordIndex; drawMenu();
      }
    }
  }

  delay(20);
}
