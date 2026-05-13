#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <TOTP.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

TFT_eSPI tft = TFT_eSPI();
USBHIDKeyboard Keyboard;
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

#define TFT_BL    38
#define BTN_OK_UP  0
#define BTN_DOWN  14
#define PWR_EN_PIN 10

char wifi_ssid[32] = "";
char wifi_password[64] = "";
char totp_base32[128] = "aaaa bbbb cccc dddd eeee ffff gggg hhhh";

const byte DNS_PORT = 53;
bool settingsPortalActive = false;

void handlePortalRoot() {
  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Tadabeer</title>";
  html += "<style>body{margin:0;font-family:Arial;background:#0b0b0b;color:#f5f5f5;padding:22px;}.card{background:#161616;padding:24px;border-radius:20px;margin-bottom:22px;}input{width:100%;padding:14px;background:#0f0f0f;color:#fff;border:1px solid rgba(255,255,255,0.1);border-radius:12px;box-sizing:border-box;margin-bottom:12px;}.btn{padding:13px 22px;border-radius:12px;font-weight:700;cursor:pointer;border:none;}.btn-primary{background:#4d8ef7;color:#fff;}</style></head><body>";
  html += "<h2>System Settings</h2><div class='card'><form method='POST' action='/save_sys'>";
  html += "<label>WiFi SSID</label><input name='sys_ssid' value='" + String(wifi_ssid) + "'>";
  html += "<label>WiFi Password</label><input name='sys_pass' type='password' value='" + String(wifi_password) + "'>";
  html += "<label>Google TOTP Secret</label><input name='sys_totp' value='" + String(totp_base32) + "'>";
  html += "<button class='btn btn-primary' type='submit'>Save Settings</button></form></div></body></html>";
  server.send(200, "text/html", html);
}

void handleSaveSys() {
  server.arg("sys_ssid").toCharArray(wifi_ssid, sizeof(wifi_ssid));
  server.arg("sys_pass").toCharArray(wifi_password, sizeof(wifi_password));
  server.arg("sys_totp").toCharArray(totp_base32, sizeof(totp_base32));
  prefs.begin("system_cfg", false);
  prefs.putString("wifi_ssid", wifi_ssid);
  prefs.putString("wifi_pass", wifi_password);
  prefs.putString("totp_key", totp_base32);
  prefs.end();
  server.send(200, "text/html", "<script>alert('Saved!'); window.location='/';</script>");
}

void startSettingsPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Tadabeer_Setup");
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  server.on("/", handlePortalRoot);
  server.on("/save_sys", HTTP_POST, handleSaveSys);
  server.onNotFound(handlePortalRoot);
  server.begin();
  settingsPortalActive = true;

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("Portal Active!");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 60);
  tft.println("Connect to:");
  tft.setCursor(10, 85);
  tft.println("Tadabeer_Setup");
}

void setup() {
  pinMode(PWR_EN_PIN, OUTPUT);
  digitalWrite(PWR_EN_PIN, HIGH);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  pinMode(BTN_OK_UP, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(0xFCC0, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(40, 100);
  tft.println("TADABEER");

  prefs.begin("system_cfg", true);
  prefs.getString("wifi_ssid", "").toCharArray(wifi_ssid, sizeof(wifi_ssid));
  prefs.getString("wifi_pass", "").toCharArray(wifi_password, sizeof(wifi_password));
  prefs.getString("totp_key", "aaaa bbbb cccc dddd").toCharArray(totp_base32, sizeof(totp_base32));
  prefs.end();

  Keyboard.begin();
  USB.begin();
  delay(800);
}

void loop() {
  if (settingsPortalActive) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  if (digitalRead(BTN_OK_UP) == LOW && !settingsPortalActive) {
    delay(2000);
    if (digitalRead(BTN_OK_UP) == LOW) {
      startSettingsPortal();
    }
  }
}
