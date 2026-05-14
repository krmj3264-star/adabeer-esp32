#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <TOTP.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>

#define PIN_LCD_BL    38
#define PIN_POWER_ON  10
#define PIN_LCD_CS     6
#define PIN_LCD_DC     7
#define PIN_LCD_WR     8
#define PIN_LCD_D0    48
#define PIN_LCD_D1    47
#define PIN_LCD_D2    39
#define PIN_LCD_D3    40
#define PIN_LCD_D4    41
#define PIN_LCD_D5    42
#define PIN_LCD_D6    45
#define PIN_LCD_D7    46
#define BTN_1          0
#define BTN_2         14
#define LCD_W        240
#define LCD_H        320

#define BLACK  0x0000
#define WHITE  0xFFFF
#define GREEN  0x07E0
#define YELLOW 0xFFE0
#define BLUE   0x001F
#define ORANGE 0xFCC0

esp_lcd_panel_handle_t panel_handle = NULL;
uint16_t fb[LCD_W * LCD_H];

void lcd_flush() {
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_W, LCD_H, fb);
}

void lcd_fill(uint16_t color) {
    for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = color;
}

void lcd_rect(int x, int y, int w, int h, uint16_t c) {
    for (int j = y; j < y+h && j < LCD_H; j++)
        for (int i = x; i < x+w && i < LCD_W; i++)
            fb[j*LCD_W+i] = c;
}

static const uint8_t font5x7[][5] = {
    {0x7C,0x12,0x11,0x12,0x7C}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
};

void lcd_char(int x, int y, char c, uint16_t color, int sz) {
    int idx = -1;
    if (c >= 'A' && c <= 'Z') idx = c - 'A';
    else if (c >= 'a' && c <= 'z') idx = c - 'a';
    else if (c >= '0' && c <= '9') idx = 26 + (c - '0');
    else if (c == ':') idx = 36;
    if (idx < 0) return;
    for (int col = 0; col < 5; col++) {
        uint8_t line = font5x7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row))
                lcd_rect(x+col*sz, y+row*sz, sz, sz, color);
        }
    }
}

void lcd_text(int x, int y, const char* txt, uint16_t color, int sz) {
    for (int i = 0; txt[i]; i++) {
        lcd_char(x + i*6*sz, y, txt[i], color, sz);
    }
}

void lcd_init() {
    pinMode(PIN_POWER_ON, OUTPUT); digitalWrite(PIN_POWER_ON, HIGH);
    delay(100);
    pinMode(PIN_LCD_BL, OUTPUT); digitalWrite(PIN_LCD_BL, HIGH);

    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .wr_gpio_num = PIN_LCD_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
            PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7,
        },
        .bus_width = 8,
        .max_transfer_bytes = LCD_W * LCD_H * 2,
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    esp_lcd_new_i80_bus(&bus_config, &i80_bus);

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle);

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, true);
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, true, false);
    esp_lcd_panel_disp_on_off(panel_handle, true);
}

USBHIDKeyboard Keyboard;
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

char wifi_ssid[32] = "";
char wifi_password[64] = "";
char totp_base32[128] = "aaaa bbbb cccc dddd eeee ffff gggg hhhh";
const byte DNS_PORT = 53;
bool settingsPortalActive = false;

void showScreen(const char* l1, const char* l2, const char* l3, uint16_t bg) {
    lcd_fill(bg);
    if (l1) lcd_text(10, 20, l1, WHITE, 3);
    if (l2) lcd_text(10, 80, l2, YELLOW, 2);
    if (l3) lcd_text(10, 120, l3, WHITE, 2);
    lcd_flush();
}

void handlePortalRoot() {
    String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Tadabeer</title><style>body{margin:0;font-family:Arial;background:#0b0b0b;color:#fff;padding:22px;}input{width:100%;padding:12px;background:#111;color:#fff;border:1px solid #333;border-radius:8px;box-sizing:border-box;margin-bottom:10px;}.btn{padding:12px 20px;border-radius:8px;font-weight:700;cursor:pointer;border:none;background:#4d8ef7;color:#fff;width:100%;}</style></head><body>";
    html += "<h2>Tadabeer Settings</h2><form method='POST' action='/save'>";
    html += "<label>WiFi SSID</label><input name='ssid' value='" + String(wifi_ssid) + "'>";
    html += "<label>WiFi Password</label><input name='pass' type='password'>";
    html += "<label>TOTP Secret</label><input name='totp' value='" + String(totp_base32) + "'>";
    html += "<button class='btn' type='submit'>Save</button></form></body></html>";
    server.send(200, "text/html", html);
}

void handleSave() {
    server.arg("ssid").toCharArray(wifi_ssid, sizeof(wifi_ssid));
    server.arg("pass").toCharArray(wifi_password, sizeof(wifi_password));
    server.arg("totp").toCharArray(totp_base32, sizeof(totp_base32));
    prefs.begin("cfg", false);
    prefs.putString("ssid", wifi_ssid);
    prefs.putString("pass", wifi_password);
    prefs.putString("totp", totp_base32);
    prefs.end();
    server.send(200, "text/html", "<script>alert('Saved!');window.location='/';</script>");
}

void startPortal() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Tadabeer_Setup");
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    server.on("/", handlePortalRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handlePortalRoot);
    server.begin();
    settingsPortalActive = true;
    showScreen("PORTAL", "Connect to:", "Tadabeer Setup", 0x0010);
}

void setup() {
    lcd_init();
    delay(200);
    pinMode(BTN_1, INPUT_PULLUP);
    pinMode(BTN_2, INPUT_PULLUP);

    prefs.begin("cfg", true);
    prefs.getString("ssid", "").toCharArray(wifi_ssid, sizeof(wifi_ssid));
    prefs.getString("pass", "").toCharArray(wifi_password, sizeof(wifi_password));
    prefs.getString("totp", "aaaa bbbb").toCharArray(totp_base32, sizeof(totp_base32));
    prefs.end();

    Keyboard.begin();
    USB.begin();

    showScreen("TADABEER", "Hold BTN1", "For Settings", BLACK);
}

void loop() {
    if (settingsPortalActive) {
        dnsServer.processNextRequest();
        server.handleClient();
    }
    if (digitalRead(BTN_1) == LOW && !settingsPortalActive) {
        delay(2000);
        if (digitalRead(BTN_1) == LOW) startPortal();
    }
}
