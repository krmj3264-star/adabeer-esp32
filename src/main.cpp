#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <TOTP.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ===== LilyGo T-HMI Pins =====
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_Parallel8 _bus;
  lgfx::Light_PWM _light;
public:
  LGFX() {
    { auto cfg = _bus.config();
      cfg.port = 0;
      cfg.freq_write = 20000000;
      cfg.pin_wr = 8; cfg.pin_rd = -1; cfg.pin_rs = 7;
      cfg.pin_d0 = 48; cfg.pin_d1 = 47; cfg.pin_d2 = 39; cfg.pin_d3 = 40;
      cfg.pin_d4 = 41; cfg.pin_d5 = 42; cfg.pin_d6 = 45; cfg.pin_d7 = 46;
      _bus.config(cfg); _panel.setBus(&_bus); }
    { auto cfg = _panel.config();
      cfg.pin_cs = 6; cfg.pin_rst = -1; cfg.pin_busy = -1;
      cfg.panel_width = 240; cfg.panel_height = 320;
      cfg.offset_x = 0; cfg.offset_y = 0;
      cfg.offset_rotation = 1;
      cfg.dummy_read_pixel = 8;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg); }
    { auto cfg = _light.config();
      cfg.pin_bl = 38;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg); _panel.setLight(&_light); }
    setPanel(&_panel);
  }
};

LGFX tft;

// ===== Buttons =====
#define BTN_OK_UP   0
#define BTN_DOWN    14
#define PWR_PIN     10

// ===== WiFi / NTP =====
const char* NTP1 = "pool.ntp.org";
const char* NTP2 = "time.google.com";
const long  GMT_OFFSET = 3 * 3600;

// ===== TOTP =====
const char* GMAIL_TOTP_BASE32 = "aaaa bbbb cccc dddd eeee ffff gggg hhhh";
uint8_t gmailTotpSecret[64];
size_t  gmailTotpSecretLen = 0;
TOTP*   gmailTotp = nullptr;

// ===== Password Storage =====
#define MAX_ITEMS 12
int   passwordCount = 4;
char  pName[MAX_ITEMS][32];
char  pUser[MAX_ITEMS][128];
char  pPass[MAX_ITEMS][128];

// ===== State =====
enum Screen { SCR_MAIN, SCR_PASSWORDS, SCR_PWD_ACTIONS, SCR_TOTP, SCR_TOTP_VIEW, SCR_SETTINGS };
Screen currentScreen = SCR_MAIN;
int    selectedIndex  = 0;
int    scrollOffset   = 0;
bool   timeSynced     = false;
int    activePassIdx  = 0;
int    activeTotpIdx  = 0;
unsigned long lastTotpDraw = 0;
bool   portalActive   = false;
String portalPass     = "";

// ===== DNS/Server =====
WebServer  server(80);
DNSServer  dnsServer;
Preferences prefs;
USBHIDKeyboard Keyboard;

const byte DNS_PORT = 53;
const unsigned long HOLD_MS = 700;

bool btnOkPressed   = false;
bool btnOkHold      = false;
unsigned long btnOkStart = 0;
bool lastDown       = HIGH;
bool downHold       = false;
unsigned long downStart = 0;

// ===== Base32 =====
int b32val(char c) {
  if (c>='A'&&c<='Z') return c-'A';
  if (c>='a'&&c<='z') return c-'a';
  if (c>='2'&&c<='7') return c-'2'+26;
  return -1;
}
size_t decodeB32(const char* in, uint8_t* out, size_t maxLen) {
  int buf=0, bits=0; size_t len=0;
  while (*in) {
    char c=*in++;
    if (c==' '||c=='='||c=='-') continue;
    int v=b32val(c); if (v<0) continue;
    buf=(buf<<5)|v; bits+=5;
    if (bits>=8) { bits-=8; if (len>=maxLen) return 0; out[len++]=(buf>>bits)&0xFF; }
  }
  return len;
}

// ===== NTP Sync =====
bool syncTime(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  unsigned long t = millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-t<20000) delay(200);
  if (WiFi.status()!=WL_CONNECTED) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); return false; }
  configTime(GMT_OFFSET, 0, NTP1, NTP2);
  struct tm ti; t=millis();
  while (!getLocalTime(&ti) && millis()-t<15000) delay(200);
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  time_t now; time(&now);
  return now > 1700000000;
}

// ===== Storage =====
void loadPasswords() {
  // defaults
  strcpy(pName[0],"Gmail");   strcpy(pUser[0],"user@gmail.com");  strcpy(pPass[0],"GmailPass123");
  strcpy(pName[1],"GitHub");  strcpy(pUser[1],"myuser");          strcpy(pPass[1],"GitHubPass456");
  strcpy(pName[2],"AWS");     strcpy(pUser[2],"aws_user");        strcpy(pPass[2],"AwsPass789");
  strcpy(pName[3],"Bank");    strcpy(pUser[3],"bank_user");       strcpy(pPass[3],"BankPass000");

  prefs.begin("vault", true);
  int cnt = prefs.getInt("cnt", 4);
  if (cnt>0 && cnt<=MAX_ITEMS) passwordCount=cnt;
  for (int i=0;i<passwordCount;i++) {
    prefs.getString(("n"+String(i)).c_str(), "").toCharArray(pName[i], 32);
    prefs.getString(("u"+String(i)).c_str(), "").toCharArray(pUser[i], 128);
    prefs.getString(("p"+String(i)).c_str(), "").toCharArray(pPass[i], 128);
  }
  prefs.end();
}

void savePasswords() {
  prefs.begin("vault", false); prefs.clear();
  prefs.putInt("cnt", passwordCount);
  for (int i=0;i<passwordCount;i++) {
    prefs.putString(("n"+String(i)).c_str(), pName[i]);
    prefs.putString(("u"+String(i)).c_str(), pUser[i]);
    prefs.putString(("p"+String(i)).c_str(), pPass[i]);
  }
  prefs.end();
}

// ===== UI =====
void drawMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(0xFCC0, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 8);

  const char* titles[] = {"Dr.Passwords","Passwords","Send As","TOTP","TOTP","Settings"};
  tft.println(titles[currentScreen]);

  const char* mainItems[] = {"Passwords","TOTP","Settings"};
  const char* actionItems[] = {"Username","Password","User+Tab+Pass"};

  const char** items = nullptr;
  int count = 0;

  if (currentScreen==SCR_MAIN)        { items=mainItems;   count=3; }
  else if (currentScreen==SCR_TOTP)   { count=1; }
  else if (currentScreen==SCR_PWD_ACTIONS) { items=actionItems; count=3; }
  else if (currentScreen==SCR_PASSWORDS)   { count=passwordCount; }
  else if (currentScreen==SCR_SETTINGS)    { count=0; }

  const int startY=40, lineH=28, visible=4;
  if (selectedIndex<scrollOffset) scrollOffset=selectedIndex;
  if (selectedIndex>=scrollOffset+visible) scrollOffset=selectedIndex-visible+1;
  if (scrollOffset<0) scrollOffset=0;

  for (int i=scrollOffset; i<min(scrollOffset+visible,count); i++) {
    bool sel=(i==selectedIndex);
    if (sel) { tft.fillRect(6,startY+(i-scrollOffset)*lineH-2,228,22,0x2945); tft.setTextColor(TFT_GREEN,0x2945); }
    else tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.setCursor(10, startY+(i-scrollOffset)*lineH);
    tft.print(sel?"> ":"  ");
    if (currentScreen==SCR_PASSWORDS) tft.println(pName[i]);
    else if (currentScreen==SCR_TOTP) tft.println("Google TOTP");
    else if (items) tft.println(items[i]);
  }
}

void drawTotpScreen(const char* code, int secs) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(70,10); tft.println("Google TOTP");
  tft.drawRoundRect(20,40,200,54,10,TFT_LIGHTGREY);
  tft.setTextSize(4);
  tft.setTextColor(TFT_WHITE,TFT_BLACK);
  int cx = 20+(200-strlen(code)*24)/2;
  tft.setCursor(cx,50); tft.println(code);
  int bw=180, prog=(secs*bw)/30;
  tft.drawRoundRect(30,105,bw,12,6,TFT_DARKGREY);
  if (prog>0) tft.fillRoundRect(32,107,prog-2,8,4,TFT_GREEN);
  tft.setTextSize(2); tft.setTextColor(TFT_WHITE,TFT_BLACK);
  char t[8]; sprintf(t,"%ds",secs);
  tft.setCursor(215,103); tft.println(t);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY,TFT_BLACK);
  tft.setCursor(10,130); tft.println("Hold OK to type code | Hold DOWN to back");
}

void drawPortalScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(0xFCC0,TFT_BLACK); tft.setTextSize(2);
  tft.setCursor(10,8); tft.println("Settings Portal");
  tft.setTextColor(TFT_WHITE,TFT_BLACK);
  tft.setCursor(10,40); tft.println("SSID: Dr. Passwords");
  tft.setCursor(10,65); tft.print("PASS: "); tft.println(portalPass);
  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  tft.setCursor(10,100); tft.println("Connect & open browser");
  tft.setCursor(10,115); tft.println("Hold OK=type pass | Hold DOWN=close");
}

// ===== Portal HTML =====
String htmlEsc(const char* s) {
  String r=s; r.replace("&","&amp;"); r.replace("<","&lt;"); r.replace(">","&gt;"); r.replace("\"","&quot;"); return r;
}

void handleRoot() {
  int ei=-1;
  if (server.hasArg("edit")) { ei=server.arg("edit").toInt(); if(ei<0||ei>=passwordCount) ei=-1; }

  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Dr.Passwords</title>";
  html += "<style>body{margin:0;font-family:Arial;background:#0b0b0b;color:#fff;padding:20px;}";
  html += "input{width:100%;padding:12px;background:#1a1a1a;color:#fff;border:1px solid #333;border-radius:8px;box-sizing:border-box;margin-bottom:10px;}";
  html += ".btn{padding:12px 20px;border-radius:8px;font-weight:700;cursor:pointer;border:none;margin:4px;}";
  html += ".bp{background:#4d8ef7;color:#fff;} .bs{background:#555;color:#fff;} .bd{background:#c0392b;color:#fff;}";
  html += "table{width:100%;border-collapse:collapse;margin-top:20px;}";
  html += "th,td{padding:12px;border:1px solid #333;text-align:left;}";
  html += "th{background:#1a1a1a;}</style></head><body>";
  html += "<h2>Dr. Passwords</h2>";
  html += "<h3>" + String(ei>=0?"Edit":"Add") + " Password</h3>";
  html += "<form method='POST' action='/save'>";
  if (ei>=0) html += "<input type='hidden' name='idx' value='"+String(ei)+"'>";
  html += "<input name='nm' placeholder='Name' value='"+(ei>=0?htmlEsc(pName[ei]):String(""))+"'>";
  html += "<input name='us' placeholder='Username' value='"+(ei>=0?htmlEsc(pUser[ei]):String(""))+"'>";
  html += "<input name='pw' placeholder='Password' id='pw' value=''>";
  html += "<button type='button' class='btn bs' onclick=\"var c='ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#';var s='';for(var i=0;i<16;i++)s+=c[Math.floor(Math.random()*c.length)];document.getElementById('pw').value=s;\">Generate</button>";
  html += "<br><button class='btn bp' type='submit'>"+(ei>=0?"Save":"Add")+"</button>";
  if (ei>=0) html += "<a href='/'><button type='button' class='btn bs'>Cancel</button></a>";
  html += "</form>";
  html += "<table><tr><th>Name</th><th>User</th><th>Actions</th></tr>";
  for (int i=0;i<passwordCount;i++) {
    html += "<tr><td>"+htmlEsc(pName[i])+"</td><td>"+htmlEsc(pUser[i])+"</td>";
    html += "<td><a href='/?edit="+String(i)+"'><button class='btn bp'>Edit</button></a>";
    html += "<a href='/del?i="+String(i)+"'><button class='btn bd'>Del</button></a></td></tr>";
  }
  html += "</table></body></html>";
  server.send(200,"text/html",html);
}

void handleSave() {
  String nm=server.arg("nm"); nm.trim();
  String us=server.arg("us"); us.trim();
  String pw=server.arg("pw"); pw.trim();
  if (nm.length()==0) { server.sendHeader("Location","/"); server.send(302); return; }
  int idx=-1;
  if (server.hasArg("idx")) { idx=server.arg("idx").toInt(); if(idx<0||idx>=passwordCount) idx=-1; }
  if (idx>=0) {
    nm.toCharArray(pName[idx],32); us.toCharArray(pUser[idx],128);
    if (pw.length()>0) pw.toCharArray(pPass[idx],128);
  } else {
    if (passwordCount>=MAX_ITEMS) { server.sendHeader("Location","/"); server.send(302); return; }
    nm.toCharArray(pName[passwordCount],32);
    us.toCharArray(pUser[passwordCount],128);
    pw.toCharArray(pPass[passwordCount],128);
    passwordCount++;
  }
  savePasswords();
  server.sendHeader("Location","/"); server.send(302);
}

void handleDel() {
  if (!server.hasArg("i")) { server.sendHeader("Location","/"); server.send(302); return; }
  int i=server.arg("i").toInt();
  if (i<0||i>=passwordCount) { server.sendHeader("Location","/"); server.send(302); return; }
  for (int j=i;j<passwordCount-1;j++) {
    strcpy(pName[j],pName[j+1]); strcpy(pUser[j],pUser[j+1]); strcpy(pPass[j],pPass[j+1]);
  }
  passwordCount--;
  if (selectedIndex>=passwordCount && passwordCount>0) selectedIndex=passwordCount-1;
  savePasswords();
  server.sendHeader("Location","/"); server.send(302);
}

void startPortal() {
  WiFi.disconnect(true); delay(200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Dr. Passwords", portalPass.c_str());
  delay(300);
  dnsServer.start(DNS_PORT,"*",WiFi.softAPIP());
  server.on("/",HTTP_GET,handleRoot);
  server.on("/save",HTTP_POST,handleSave);
  server.on("/del",HTTP_GET,handleDel);
  server.onNotFound(handleRoot);
  server.begin();
  portalActive=true;
  drawPortalScreen();
}

void stopPortal() {
  if (!portalActive) return;
  server.stop(); dnsServer.stop();
  WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
  portalActive=false;
}

String genPass() {
  const char* c="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*";
  String s=""; for(int i=0;i<16;i++) s+=c[esp_random()%strlen(c)]; return s;
}

void selectItem() {
  if (currentScreen==SCR_TOTP_VIEW) {
    time_t now; time(&now);
    if (now<1700000000) return;
    char* code=gmailTotp->getCode(now);
    Keyboard.print(code); return;
  }
  if (portalActive) { Keyboard.print(portalPass); return; }

  if (currentScreen==SCR_MAIN) {
    if (selectedIndex==0) { currentScreen=SCR_PASSWORDS; selectedIndex=0; scrollOffset=0; drawMenu(); }
    else if (selectedIndex==1) {
      if (!timeSynced) {
        tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextSize(2);
        tft.setCursor(10,60); tft.println("Enter WiFi SSID:");
        // Simple: use stored wifi or show message
        tft.setCursor(10,90); tft.println("Set WiFi in portal");
        delay(2000);
        drawMenu(); return;
      }
      currentScreen=SCR_TOTP_VIEW; lastTotpDraw=0;
    }
    else if (selectedIndex==2) { currentScreen=SCR_SETTINGS; selectedIndex=0; scrollOffset=0; startPortal(); }
    return;
  }

  if (currentScreen==SCR_PASSWORDS) {
    activePassIdx=selectedIndex;
    currentScreen=SCR_PWD_ACTIONS;
    selectedIndex=0; scrollOffset=0; drawMenu(); return;
  }

  if (currentScreen==SCR_PWD_ACTIONS) {
    tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_CYAN,TFT_BLACK);
    tft.setTextSize(2); tft.setCursor(20,60); tft.println("Sending...");
    delay(200);
    if (selectedIndex==0) Keyboard.print(pUser[activePassIdx]);
    else if (selectedIndex==1) Keyboard.print(pPass[activePassIdx]);
    else if (selectedIndex==2) { Keyboard.print(pUser[activePassIdx]); Keyboard.write(KEY_TAB); Keyboard.print(pPass[activePassIdx]); }
    delay(300); drawMenu(); return;
  }

  if (currentScreen==SCR_TOTP) {
    if (!timeSynced) { return; }
    currentScreen=SCR_TOTP_VIEW; lastTotpDraw=0; return;
  }
}

void moveDown() {
  int cnt=0;
  if (currentScreen==SCR_MAIN) cnt=3;
  else if (currentScreen==SCR_PASSWORDS) cnt=passwordCount;
  else if (currentScreen==SCR_PWD_ACTIONS) cnt=3;
  else if (currentScreen==SCR_TOTP) cnt=1;
  if (cnt==0) return;
  selectedIndex=(selectedIndex+1)%cnt;
  drawMenu();
}

void moveUp() {
  int cnt=0;
  if (currentScreen==SCR_MAIN) cnt=3;
  else if (currentScreen==SCR_PASSWORDS) cnt=passwordCount;
  else if (currentScreen==SCR_PWD_ACTIONS) cnt=3;
  else if (currentScreen==SCR_TOTP) cnt=1;
  if (cnt==0) return;
  selectedIndex=(selectedIndex-1+cnt)%cnt;
  drawMenu();
}

void goBack() {
  if (currentScreen==SCR_TOTP_VIEW) { currentScreen=SCR_TOTP; selectedIndex=0; scrollOffset=0; drawMenu(); }
  else if (currentScreen==SCR_PWD_ACTIONS) { currentScreen=SCR_PASSWORDS; selectedIndex=activePassIdx; drawMenu(); }
  else if (portalActive) { stopPortal(); currentScreen=SCR_MAIN; selectedIndex=0; scrollOffset=0; drawMenu(); }
  else if (currentScreen!=SCR_MAIN) { currentScreen=SCR_MAIN; selectedIndex=0; scrollOffset=0; drawMenu(); }
}

void setup() {
  pinMode(PWR_PIN, OUTPUT); digitalWrite(PWR_PIN, HIGH);
  pinMode(BTN_OK_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  portalPass = genPass();
  loadPasswords();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(0xFCC0, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(30, 60);
  tft.println("Dr.Passwords");
  delay(1200);

  gmailTotpSecretLen = decodeB32(GMAIL_TOTP_BASE32, gmailTotpSecret, sizeof(gmailTotpSecret));
  if (gmailTotpSecretLen > 0) gmailTotp = new TOTP(gmailTotpSecret, gmailTotpSecretLen);

  drawMenu();
  Keyboard.begin(); USB.begin();
  delay(800);
}

void loop() {
  if (portalActive) { dnsServer.processNextRequest(); server.handleClient(); }

  if (currentScreen==SCR_TOTP_VIEW && gmailTotp) {
    time_t now; time(&now);
    if (now>1700000000 && lastTotpDraw!=(unsigned long)now) {
      int secs=30-(now%30);
      char* code=gmailTotp->getCode(now);
      drawTotpScreen(code, secs);
      lastTotpDraw=(unsigned long)now;
    }
  }

  bool okState  = digitalRead(BTN_OK_UP);
  bool dnState  = digitalRead(BTN_DOWN);
  unsigned long now = millis();

  // OK button
  if (okState==LOW) {
    if (!btnOkPressed) { btnOkPressed=true; btnOkStart=now; btnOkHold=false; }
    else if (!btnOkHold && now-btnOkStart>=HOLD_MS) { btnOkHold=true; selectItem(); }
  } else {
    if (btnOkPressed && !btnOkHold) {
      if (currentScreen!=SCR_TOTP_VIEW && !portalActive) moveDown();
    }
    btnOkPressed=false; btnOkHold=false;
  }

  // DOWN button
  if (dnState==LOW) {
    if (lastDown==HIGH) { downStart=now; downHold=false; }
    if (!downHold && now-downStart>=HOLD_MS) { downHold=true; goBack(); }
  } else {
    if (lastDown==LOW && !downHold && now-downStart<HOLD_MS) {
      if (currentScreen!=SCR_TOTP_VIEW && !portalActive) moveUp();
    }
    downHold=false;
  }
  lastDown=dnState;

  delay(20);
}
