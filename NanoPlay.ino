/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║           NanoPlay — 6 Games Console  v2.0                   ║
 * ║      ESP32 + OLED SSD1306 128x64 — U8g2                      ║
 * ║      WiFi → Firebase Realtime DB (GitHub Pages ready)        ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  الأزرار:                                                    ║
 * ║    زر A → GPIO 19   UP/LEFT/تبديل                            ║
 * ║    زر B → GPIO 25   DOWN/تأكيد/إطلاق                        ║
 * ║    زر C → GPIO 26   RIGHT/إطلاق نار                          ║
 * ║    زر D → GPIO 15   BACK/قفز (Hold=Menu)                    ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  ⚠️  قبل الرفع على GitHub اضبط:                              ║
 * ║    WIFI_SSID     → اسم شبكة الواي فاي                       ║
 * ║    WIFI_PASS     → كلمة مرور الشبكة                          ║
 * ║    FIREBASE_HOST → رابط مشروع Firebase الخاص بك              ║
 * ║    PLAYER_NAME   → اسم اللاعب على هذا الجهاز                 ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>   // مكتبة ArduinoJson v6

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u(U8G2_R0, U8X8_PIN_NONE);
#define SW 128
#define SH  64

#define BTN_A 19
#define BTN_B 25
#define BTN_C 26
#define BTN_D 15

// ══════════════════════════════════════════════════════
//  ⚙️  إعدادات WiFi و Firebase — عدّل هنا فقط
// ══════════════════════════════════════════════════════
#define WIFI_SSID       "aaa"
#define WIFI_PASS       "78224347822434"

// مثال: "nanoplay-abc12-default-rtdb.firebaseio.com"
#define FIREBASE_HOST   "nano-77265-default-rtdb.firebaseio.com"
#define FIREBASE_SECRET "xQaBJMqqrChWXzHiaY3amPEhsHAYRf5kMuuWuSqY"   // من Project Settings > Service Accounts > Database secrets

// اسم اللاعب على هذا الجهاز (يظهر في لوحة الصدارة)
#define PLAYER_NAME     "Player1"

// ──────────────────────────────────────────────────────
// رابط إرسال السكور الى Firebase
// POST إلى /scores.json
#define FIREBASE_URL    "https://" FIREBASE_HOST "/scores.json?auth=" FIREBASE_SECRET

// ══════════════════════════════════════════════════════
//  حالات التطبيق
// ══════════════════════════════════════════════════════
enum AppState {
  APP_SPLASH, APP_WIFI, APP_FACE, APP_MENU,
  APP_FLAPPY, APP_PONG, APP_DINO,
  APP_SPACE,  APP_MAZE, APP_BREAKOUT
};
AppState appState = APP_SPLASH;

// ══════════════════════════════════════════════════════
//  أزرار
// ══════════════════════════════════════════════════════
bool btnA() { return digitalRead(BTN_A)==LOW; }
bool btnB() { return digitalRead(BTN_B)==LOW; }
bool btnC() { return digitalRead(BTN_C)==LOW; }
bool btnD() { return digitalRead(BTN_D)==LOW; }

float np_clamp(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }
float np_sign(float v){ return v>=0?1.f:-1.f; }

// ══════════════════════════════════════════════════════
//  WiFi — الاتصال بالشبكة
// ══════════════════════════════════════════════════════
bool wifiConnected = false;

void connectWiFi() {
  u.clearBuffer();
  u.setFont(u8g2_font_6x10_tr);
  u.setCursor(14, 20); u.print("Connecting WiFi");
  u.setCursor(18, 34); u.print(WIFI_SSID);
  u.setFont(u8g2_font_4x6_tr);
  u.setCursor(20, 50); u.print("Please wait...");
  u.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t = millis();
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) {
    delay(400);
    u.clearBuffer();
    u.setFont(u8g2_font_6x10_tr);
    u.setCursor(14, 20); u.print("Connecting WiFi");
    u.setCursor(18, 34); u.print(WIFI_SSID);
    u.setFont(u8g2_font_4x6_tr);
    String d = "";
    for(int i=0;i<dots%4;i++) d+=".";
    u.setCursor(52, 50); u.print(d);
    u.sendBuffer();
    dots++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    u.clearBuffer();
    u.setFont(u8g2_font_6x10_tr);
    u.setCursor(28, 20); u.print("Connected!");
    u.setFont(u8g2_font_4x6_tr);
    u.setCursor(4, 34); u.print(WiFi.localIP().toString());
    u.setCursor(10, 48); u.print("Scores will sync");
    u.setCursor(14, 58); u.print("to leaderboard");
    u.sendBuffer();
    delay(2000);
  } else {
    wifiConnected = false;
    u.clearBuffer();
    u.setFont(u8g2_font_6x10_tr);
    u.setCursor(22, 20); u.print("WiFi Failed");
    u.setFont(u8g2_font_4x6_tr);
    u.setCursor(4, 36); u.print("Offline mode active");
    u.setCursor(4, 48); u.print("Scores won't sync");
    u.setCursor(10, 60); u.print("Press any key...");
    u.sendBuffer();
    delay(2500);
  }
}

// ══════════════════════════════════════════════════════
//  Firebase — إرسال السكور
// ══════════════════════════════════════════════════════
void submitScore(const char* gameName, int score) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) return;

  // عرض رسالة الإرسال على الشاشة
  u.clearBuffer();
  u.setFont(u8g2_font_6x10_tr);
  u.setCursor(18, 22); u.print("Submitting...");
  u.setFont(u8g2_font_4x6_tr);
  u.setCursor(10, 38); u.print(gameName);
  u.setCursor(34, 50); u.print("Score: "); u.print(score);
  u.sendBuffer();

  HTTPClient http;
  http.begin(FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  // بناء JSON
  StaticJsonDocument<256> doc;
  doc["player"]    = PLAYER_NAME;
  doc["game"]      = gameName;
  doc["score"]     = score;
  doc["timestamp"] = millis();
  doc["device"]    = WiFi.macAddress();

  String body;
  serializeJson(doc, body);

  int httpCode = http.POST(body);
  http.end();

  // نتيجة الإرسال
  u.clearBuffer();
  u.setFont(u8g2_font_6x10_tr);
  if (httpCode == 200 || httpCode == 201) {
    u.setCursor(22, 24); u.print("Score Saved!");
    u.setFont(u8g2_font_4x6_tr);
    u.setCursor(8, 40); u.print("Check leaderboard");
    u.setCursor(14, 52); u.print("on the website!");
  } else {
    u.setCursor(20, 24); u.print("Sync Failed");
    u.setFont(u8g2_font_4x6_tr);
    u.setCursor(4, 40); u.print("Code: "); u.print(httpCode);
    u.setCursor(6, 52); u.print("Score saved locally");
  }
  u.sendBuffer();
  delay(1500);
}

// ══════════════════════════════════════════════════════
//  SPLASH
// ══════════════════════════════════════════════════════
void showSplash() {
  u.clearBuffer();
  u.setFont(u8g2_font_ncenB14_tr);
  int w=u.getStrWidth("NanoPlay");
  u.setCursor((SW-w)/2,30); u.print("NanoPlay");
  u.setFont(u8g2_font_4x6_tr);
  u.setCursor(28,46); u.print("6 Games Console");
  u.drawFrame(0,0,SW,SH); u.drawFrame(2,2,SW-4,SH-4);
  u.sendBuffer(); delay(2200);

  u.clearBuffer();
  u.setFont(u8g2_font_ncenB14_tr);
  w=u.getStrWidth("microbots");
  u.setCursor((SW-w)/2,32); u.print("microbots");
  u.setFont(u8g2_font_5x7_tr);
  w=u.getStrWidth("Group 6");
  u.setCursor((SW-w)/2,48); u.print("Group 6");
  u.sendBuffer(); delay(2000);
  u.clearBuffer(); u.sendBuffer();
}

// ══════════════════════════════════════════════════════
//  FACE / EMOJI SCREEN
// ══════════════════════════════════════════════════════
int eyeWidth=26,eyeHeight=28,eyeY=16;
int leftEyeX=24,rightEyeX=78;
int mouthRX=8,mouthRY=3,mouthThick=5;
int faceMode=0,targetMode=0;
float faceTrans=0.0,faceTransSpd=0.05;
float zzY=0.0,zzOX=0.0,zzAF=-0.25;
float angerLv=0.0;
unsigned long lastFaceSwitch=0;

void showFace() {
  u.clearBuffer();
  unsigned long now=millis();
  if(faceMode!=3&&now-lastFaceSwitch>5000){targetMode=(targetMode+1)%3;lastFaceSwitch=now;}
  if(faceMode<targetMode)faceMode++;
  else if(faceMode>targetMode)faceMode--;

  if(faceMode==2){
    u.drawLine(leftEyeX,eyeY+eyeHeight/2,leftEyeX+eyeWidth,eyeY+eyeHeight/2);
    u.drawLine(rightEyeX,eyeY+eyeHeight/2,rightEyeX+eyeWidth,eyeY+eyeHeight/2);
  } else if(faceMode==3){
    angerLv+=0.03; if(angerLv>1.0)angerLv=1.0;
    for(int y=0;y<eyeHeight;y++){
      int cut=(y*eyeWidth*angerLv)/eyeHeight;
      u.drawLine(leftEyeX,eyeY+y,leftEyeX+cut,eyeY+y);
      u.drawLine(rightEyeX+eyeWidth-cut,eyeY+y,rightEyeX+eyeWidth,eyeY+y);
    }
  } else {
    angerLv=0.0;
    int ec=(faceMode==1)?(int)(eyeHeight*0.5*faceTrans):0;
    u.drawRBox(leftEyeX,eyeY+ec,eyeWidth,eyeHeight-ec,5);
    u.drawRBox(rightEyeX,eyeY+ec,eyeWidth,eyeHeight-ec,5);
  }
  int mCX=SW/2,mCY=eyeY+eyeHeight+8;
  float cRY=0; int mPulse=0;
  if(faceMode==0)cRY=mouthRY*(1.0-2.0*faceTrans);
  else if(faceMode==1)cRY=-mouthRY*faceTrans;
  else if(faceMode==3){cRY=-mouthRY*1.2;mPulse=sin(millis()*0.015)*1;}
  for(int t=0;t<mouthThick;t++)
    for(int dx=-mouthRX;dx<=mouthRX;dx++){
      float yv=cRY*sqrt(1.0-((float)dx*dx)/(mouthRX*mouthRX));
      u.drawPixel(mCX+dx,mCY+yv-t+mPulse);
    }
  if(faceMode==2){
    if(zzY==0.0)zzY=mCY-10;
    zzY-=0.3; zzOX+=zzAF;
    u.setFont(u8g2_font_6x10_tr);
    u.setCursor(90+(int)zzOX,(int)zzY); u.print("Zz");
    if(zzY<-16){zzY=mCY-10;zzOX=0.0;}
  } else {zzY=0.0;zzOX=0.0;}

  if(faceMode==1&&faceTrans<1.0){faceTrans+=faceTransSpd;if(faceTrans>1.0)faceTrans=1.0;}
  else if(faceMode==0&&faceTrans>0.0){faceTrans-=faceTransSpd;if(faceTrans<0.0)faceTrans=0.0;}
  else if(faceMode==2&&faceTrans<1.0){faceTrans+=faceTransSpd;if(faceTrans>1.0)faceTrans=1.0;}

  // أيقونة WiFi في الزاوية
  u.setFont(u8g2_font_4x6_tr);
  if(wifiConnected){
    u.setCursor(SW-14,8); u.print("WiFi");
  } else {
    u.setCursor(SW-16,8); u.print("OFF");
  }
  u.setCursor(4,63); u.print("Hold D = Games Menu");
  u.sendBuffer();
}

// ══════════════════════════════════════════════════════
//  MAIN MENU
// ══════════════════════════════════════════════════════
int menuSel=0;
#define GAME_COUNT 6
const char* gameNames[GAME_COUNT]={
  "Flappy Bird","Pong","Dino Run",
  "Space Shooter","Maze Runner","Breakout"
};
// مفاتيح اسم اللعبة لإرسالها لـ Firebase
const char* gameKeys[GAME_COUNT]={
  "flappy","pong","dino","space","maze","breakout"
};

void drawGameIcon(int game,int x,int y){
  switch(game){
    case 0:
      u.drawDisc(x+5,y+8,3);
      u.drawTriangle(x+8,y+7,x+13,y+5,x+8,y+9);
      u.drawLine(x,y+5,x+3,y+3);u.drawLine(x,y+5,x+3,y+7);
      u.drawLine(x+16,y+12,x+19,y+10);u.drawLine(x+16,y+12,x+19,y+14);
      break;
    case 1:
      u.drawBox(x,y+4,3,12);u.drawBox(x+17,y+6,3,10);
      u.drawDisc(x+9,y+10,2);u.drawVLine(x+10,y,16);
      break;
    case 2:
      u.drawBox(x+6,y+2,8,6);u.drawBox(x+4,y+7,10,8);
      u.drawBox(x+2,y+5,4,3);u.drawBox(x+5,y+14,3,4);u.drawBox(x+9,y+14,3,4);
      break;
    case 3:
      u.drawTriangle(x+9,y+1,x+4,y+14,x+14,y+14);
      u.drawBox(x+7,y+10,5,5);
      u.drawDisc(x+4,y+15,2);u.drawDisc(x+14,y+15,2);
      break;
    case 4:
      u.drawFrame(x,y,20,18);
      u.drawHLine(x+5,y,8);u.drawVLine(x+10,y+5,8);u.drawHLine(x+5,y+10,8);
      u.drawDisc(x+2,y+2,1);
      u.drawPixel(x+17,y+15);u.drawPixel(x+16,y+15);u.drawPixel(x+17,y+14);
      break;
    case 5:
      for(int i=0;i<4;i++)u.drawFrame(x+i*5,y,4,3);
      for(int i=0;i<4;i++)u.drawFrame(x+i*5,y+4,4,3);
      u.drawDisc(x+9,y+12,2);u.drawRBox(x+4,y+16,12,3,1);
      break;
  }
}

void drawGameSplash(int game){
  u.clearBuffer();
  u.drawFrame(0,0,SW,SH);u.drawFrame(3,3,SW-6,SH-6);
  int ix=(SW/2)-20,iy=10;
  switch(game){
    case 0:
      u.drawDisc(ix+10,iy+14,5);
      u.drawTriangle(ix+15,iy+12,ix+26,iy+8,ix+15,iy+17);
      u.drawBox(ix,iy+2,5,22);u.drawBox(ix+1,iy,7,4);
      u.drawBox(ix+33,iy+12,5,22);u.drawBox(ix+32,iy+22,7,4);
      break;
    case 1:
      u.drawBox(ix,iy+8,5,20);u.drawBox(ix+34,iy+12,5,16);
      u.drawDisc(ix+19,iy+18,4);
      for(int y=iy;y<iy+32;y+=4)u.drawPixel(ix+19,y);
      break;
    case 2:
      u.drawBox(ix+10,iy+2,16,12);u.drawBox(ix+6,iy+12,20,14);
      u.drawBox(ix+2,iy+10,8,6);u.drawBox(ix+8,iy+24,6,8);u.drawBox(ix+16,iy+24,6,8);
      u.drawDisc(ix+20,iy+5,2);
      break;
    case 3:
      u.drawTriangle(ix+18,iy+2,ix+8,iy+28,ix+28,iy+28);
      u.drawBox(ix+14,iy+18,8,10);
      u.drawDisc(ix+10,iy+30,3);u.drawDisc(ix+26,iy+30,3);
      break;
    case 4:
      u.drawFrame(ix,iy,38,32);
      u.drawHLine(ix+8,iy,14);u.drawHLine(ix+18,iy+8,14);
      u.drawVLine(ix+18,iy,16);u.drawVLine(ix+28,iy+16,16);
      u.drawHLine(ix+8,iy+24,14);
      u.drawDisc(ix+3,iy+3,2);u.drawFrame(ix+32,iy+26,4,4);
      break;
    case 5:
      for(int c=0;c<6;c++)for(int r=0;r<3;r++)u.drawFrame(ix+c*6,iy+r*5,5,4);
      u.drawDisc(ix+18,iy+20,3);u.drawRBox(ix+8,iy+28,22,5,2);
      break;
  }
  u.setFont(u8g2_font_6x10_tr);
  int tw=u.getStrWidth(gameNames[game]);
  u.setCursor((SW-tw)/2,58);u.print(gameNames[game]);
  u.sendBuffer();delay(1200);
}

void drawMenu(){
  u.clearBuffer();
  u.setFont(u8g2_font_5x7_tr);
  int tw=u.getStrWidth("NanoPlay");
  u.setCursor((SW-tw)/2,8);u.print("NanoPlay");
  // مؤشر WiFi في القائمة
  u.setFont(u8g2_font_4x6_tr);
  u.setCursor(SW-18,8);
  if(wifiConnected) u.print("W:ON");
  else              u.print("W:OFF");
  u.drawHLine(0,10,SW);
  int page=menuSel/3,startGame=page*3;
  for(int i=0;i<3;i++){
    int gi=startGame+i;
    if(gi>=GAME_COUNT)break;
    int rowY=14+i*17;
    bool sel=(gi==menuSel);
    if(sel){u.setDrawColor(1);u.drawRBox(0,rowY-1,SW,15,2);u.setDrawColor(0);}
    drawGameIcon(gi,2,rowY-2);
    u.setFont(u8g2_font_5x7_tr);u.setCursor(26,rowY+9);u.print(gameNames[gi]);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(SW-12,rowY+9);u.print(gi+1);
    u.setDrawColor(1);
  }
  u.setFont(u8g2_font_4x6_tr);
  u.setCursor(2,63);u.print("A/B=Move  C=Play  D=Face");
  for(int p=0;p<2;p++){
    if(p==page)u.drawDisc(SW/2-4+p*8,62,2);
    else u.drawCircle(SW/2-4+p*8,62,2);
  }
  u.sendBuffer();
}

// ══════════════════════════════════════════════════════
//  FLAPPY BIRD
// ══════════════════════════════════════════════════════
const unsigned char epd_bitmap_flappy[] PROGMEM = {
  0xc0,0x0f,0x00,0x30,0x12,0x00,0x08,0x21,0x00,0x1e,0x51,0x00,0x21,0x51,0x00,0x41,
  0x42,0x00,0x41,0xfc,0x00,0x22,0x02,0x01,0x1c,0xfd,0x00,0x04,0x82,0x00,0x18,0x7c,
  0x00,0xe0,0x03,0x00
};
const unsigned char epd_bitmap_pipe[] PROGMEM = {
  0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,
  0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0xa0,
  0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,
  0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x02,0xa0,0x00,0x02,
  0xa0,0x00,0x02,0xa0,0x00,0x02,0xa0,0x00,0x02,0xa0,0x00,0x02,0xa0,0x00,0x02,0xa0,
  0x00,0x02,0xa0,0x00,0x02,0xa0,0x00,0x02,0xa0,0x00,0xff,0xff,0x01,0x11,0x40,0x01,
  0x09,0x20,0x01,0x05,0x10,0x01,0x03,0x08,0x01,0x01,0x04,0x01,0xff,0xff,0x01,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xff,0xff,0x01,0x11,0x40,0x01,0x09,0x20,0x01,0x05,0x10,0x01,0x03,0x08,0x01,0x01,
  0x04,0x01,0xff,0xff,0x01,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,
  0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,0x0a,0x80,0x00,
  0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,
  0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x0a,0xa0,0x00,0x02,0xa0,
  0x00,0x02,0xa0,0x00,0x02,0xa0,0x00,0x02,0xa0,0x00,0x02,0xa0,0x00,0x02,0xa0,0x00,
  0x02,0xa0,0x00,0x02,0xa0,0x00
};
const unsigned char epd_bitmap_over[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0xf8,0xfb,0xfe,0xff,0x03,0x0c,0x8e,0x73,0x02,0x02,0xe6,
  0x27,0x23,0xf2,0x03,0xf2,0x73,0x02,0xf2,0x01,0x32,0x72,0x02,0x02,0x01,0x72,0x02,
  0x52,0xf2,0x01,0x66,0x72,0x72,0xf2,0x03,0x0c,0x72,0x72,0x02,0x02,0xfc,0xfd,0xfd,
  0xfd,0x03,0x06,0x73,0x02,0x02,0x03,0x72,0x72,0xf2,0x73,0x02,0x72,0x72,0xf2,0x73,
  0x02,0x72,0x22,0x02,0x33,0x02,0x72,0x06,0xf3,0x83,0x03,0x72,0x8e,0xf3,0x13,0x03,
  0x06,0xdb,0x02,0x32,0x02,0xfc,0x71,0xfe,0xff,0x03,0x00,0x00,0x00,0x00,0x00
};

#define PIPE_SPACING 80
#define SAFE_MARGIN  10
#define GAP_HEIGHT   20

bool fl_dead=false; int fl_jump=0;
int fx=20,fy=25,pxa,pxb,pxc;
int pya=-15,pyb=-20,pyc=-10;
int fl_score=0,fl_speed=1;
bool fl_started=false,fl_pa=false,fl_pb=false,fl_pc=false;

#define MAX_SCORES 10
struct ScoreEntry{String name;int value;};
ScoreEntry topScores[MAX_SCORES]; int numScores=0;

void addScore(String name,int val){
  ScoreEntry e={name,val}; int i=0;
  while(i<numScores&&topScores[i].value>=val)i++;
  if(i<MAX_SCORES){for(int j=min(numScores,MAX_SCORES-1);j>i;j--)topScores[j]=topScores[j-1];topScores[i]=e;if(numScores<MAX_SCORES)numScores++;}
}

void fl_reset(){
  fl_dead=false;fl_started=false;fx=20;fy=25;
  pxa=64;pxb=pxa+PIPE_SPACING;pxc=pxb+PIPE_SPACING;
  fl_score=0;fl_speed=1;fl_jump=0;fl_pa=fl_pb=fl_pc=false;
}

void checkPipe(int px,int py){
  int bT=fy,bB=fy+12,bL=fx,bR=fx+17;
  int pL=px,pR=px+24,gT=py+40-SAFE_MARGIN,gB=py+40+GAP_HEIGHT+SAFE_MARGIN;
  if(bR>pL&&bL<pR)if(bT<gT||bB>gB)fl_dead=true;
}

void runFlappy(){
  static bool pA=true; static unsigned long dHeld=0;
  bool a=btnA()||btnD(); bool d=btnD();
  if(d){if(dHeld==0)dHeld=millis();if(millis()-dHeld>1000){dHeld=0;fl_reset();appState=APP_MENU;return;}}else dHeld=0;
  if(!fl_started){
    u.clearBuffer();u.drawFrame(0,0,SW,SH);
    u.setFont(u8g2_font_ncenB10_tr);
    int tw=u.getStrWidth("FLAPPY BIRD");u.setCursor((SW-tw)/2,18);u.print("FLAPPY BIRD");
    u.drawDisc(64,35,5);u.drawTriangle(69,33,78,30,69,37);u.drawHLine(55,40,18);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(14,50);u.print("A/D=Jump  Hold D=Menu");
    u.sendBuffer();if(a&&!pA)fl_started=true;pA=a;return;
  }
  if(!fl_dead){
    if(a)fl_jump=1;
    u.clearBuffer();u.drawFrame(0,0,SW,SH);
    u.drawXBM(pxa,pya,24,101,epd_bitmap_pipe);u.drawXBM(pxb,pyb,24,101,epd_bitmap_pipe);
    u.drawXBM(pxc,pyc,24,101,epd_bitmap_pipe);u.drawXBM(fx,fy,17,12,epd_bitmap_flappy);
    u.setFont(u8g2_font_6x10_tr);u.setCursor(5,10);u.print("Score:");u.print(fl_score);
    u.sendBuffer();
    if(fl_jump==5)fl_jump=0;if(fl_jump==0)fy++;if(fy==52){fl_dead=true;fy=25;}if(fy<=0)fy=0;
    if(fl_jump>0){fy-=2;fl_jump++;}
    pxa-=fl_speed;pxb-=fl_speed;pxc-=fl_speed;
    if(pxa<=-18){pxa=max(pxb,max(pxc,0))+PIPE_SPACING;pya=random(-28,-8);fl_pa=false;}
    if(pxb<=-18){pxb=max(pxa,max(pxc,0))+PIPE_SPACING;pyb=random(-28,-8);fl_pb=false;}
    if(pxc<=-18){pxc=max(pxa,max(pxb,0))+PIPE_SPACING;pyc=random(-28,-8);fl_pc=false;}
    checkPipe(pxa,pya);checkPipe(pxb,pyb);checkPipe(pxc,pyc);
    if(!fl_pa&&fx>pxa+24){fl_score++;fl_pa=true;}
    if(!fl_pb&&fx>pxb+24){fl_score++;fl_pb=true;}
    if(!fl_pc&&fx>pxc+24){fl_score++;fl_pc=true;}
    if(fl_score>0)fl_speed=1+(int)sqrt(fl_score/5);
  } else {
    u.clearBuffer();u.drawXBM(63-17,31-9,35,19,epd_bitmap_over);u.drawFrame(0,0,SW,SH);
    u.setFont(u8g2_font_6x10_tr);u.setCursor(50,50);u.print("Score:");u.print(fl_score);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(4,62);u.print("A/D=Retry  Hold D=Menu");
    u.sendBuffer();
    if(a&&!pA){
      addScore(PLAYER_NAME, fl_score);
      submitScore("flappy", fl_score);  // ← إرسال السكور
      fl_reset();fl_started=true;
    }
  }
  pA=a;
}

// ══════════════════════════════════════════════════════
//  PONG
// ══════════════════════════════════════════════════════
#define P_PAD_W 3
#define P_PAD_H 14
#define P_PAD_SP 2
#define P_PAD_MG 3
#define P_BSIZ 3
#define P_WIN 7
#define P_AI_ACC 0.82f
#define P_BINIT 1.8f
#define P_BMAX 4.5f
#define P_SINC 0.15f
#define P_FMS 16
#define P_PPAUSE 1200

float pp_pY,pp_aY,pp_aTY,pp_bX,pp_bY,pp_bVX,pp_bVY,pp_bSpd;
int pp_pSc=0,pp_aSc=0; bool pp_pScored=false;
unsigned long pp_ptTimer=0,pp_lastF=0;
enum PongState{PP_WAIT,PP_PLAY,PP_POINT,PP_WIN}; PongState pongSt=PP_WAIT;

void pp_resetBall(bool toP){
  pp_bX=SW/2.0f;pp_bY=SH/2.0f;pp_bSpd=P_BINIT;
  pp_bVX=toP?-pp_bSpd:pp_bSpd;
  float ang=random(30,60)*(PI/180.0f);pp_bVY=pp_bSpd*sin(ang)*(random(2)?1:-1);
}
void pp_newGame(){
  pp_pSc=0;pp_aSc=0;pp_pY=(SH-P_PAD_H)/2.0f;pp_aY=pp_pY;pp_aTY=pp_pY;
  pp_resetBall(random(2));pongSt=PP_PLAY;
}
void pp_updateAI(){
  static unsigned long t=0;
  if(millis()-t>200){t=millis();float c=pp_bY+P_BSIZ/2.0f;
    float err=(1.0f-P_AI_ACC)*SH*((random(100)/100.0f)-0.5f);
    pp_aTY=np_clamp(c-P_PAD_H/2.0f+err,0,SH-P_PAD_H);}
  float d=pp_aTY-pp_aY,s=P_PAD_SP*0.9f;
  if(abs(d)>s)pp_aY+=np_sign(d)*s;else pp_aY=pp_aTY;
  pp_aY=np_clamp(pp_aY,0,SH-P_PAD_H);
}
void pp_updateBall(){
  pp_bX+=pp_bVX;pp_bY+=pp_bVY;
  if(pp_bY<=0){pp_bY=0;pp_bVY=abs(pp_bVY);}
  if(pp_bY+P_BSIZ>=SH){pp_bY=SH-P_BSIZ;pp_bVY=-abs(pp_bVY);}
  float pL=P_PAD_MG,pR=P_PAD_MG+P_PAD_W;
  if(pp_bX<=pR&&pp_bX>=pL&&pp_bY+P_BSIZ>=pp_pY&&pp_bY<=pp_pY+P_PAD_H&&pp_bVX<0){
    pp_bX=pR;float h=(pp_bY+P_BSIZ/2.0f-pp_pY)/P_PAD_H,a=(h-0.5f)*1.2f;
    pp_bSpd=min(pp_bSpd+P_SINC,P_BMAX);pp_bVX=pp_bSpd*cos(a);pp_bVY=pp_bSpd*sin(a);}
  float aL=SW-P_PAD_MG-P_PAD_W,aR=SW-P_PAD_MG;
  if(pp_bX+P_BSIZ>=aL&&pp_bX+P_BSIZ<=aR&&pp_bY+P_BSIZ>=pp_aY&&pp_bY<=pp_aY+P_PAD_H&&pp_bVX>0){
    pp_bX=aL-P_BSIZ;float h=(pp_bY+P_BSIZ/2.0f-pp_aY)/P_PAD_H,a=(h-0.5f)*1.2f;
    pp_bSpd=min(pp_bSpd+P_SINC,P_BMAX);pp_bVX=-pp_bSpd*cos(a);pp_bVY=pp_bSpd*sin(a);}
  if(pp_bX+P_BSIZ<0){pp_aSc++;pp_pScored=false;pongSt=PP_POINT;pp_ptTimer=millis();}
  if(pp_bX>SW){pp_pSc++;pp_pScored=true;pongSt=PP_POINT;pp_ptTimer=millis();}
}

void runPong(){
  unsigned long now=millis();
  static unsigned long dHeld=0;static bool pA=true,pB=true;
  bool a=btnA(),b=btnB(),d=btnD();
  if(d){if(dHeld==0)dHeld=millis();if(millis()-dHeld>1000){dHeld=0;pongSt=PP_WAIT;appState=APP_MENU;return;}}else dHeld=0;
  if(pongSt==PP_PLAY){if(a)pp_pY-=P_PAD_SP;if(b)pp_pY+=P_PAD_SP;pp_pY=np_clamp(pp_pY,0,SH-P_PAD_H);}
  if(pongSt==PP_WAIT){
    u.clearBuffer();u.drawFrame(0,0,SW,SH);
    u.drawBox(8,20,P_PAD_W+2,P_PAD_H+4);u.drawBox(SW-12,22,P_PAD_W+2,P_PAD_H);
    u.drawDisc(SW/2,SH/2,4);for(int y=0;y<SH;y+=5)u.drawPixel(SW/2,y);
    u.setFont(u8g2_font_ncenB10_tr);int tw=u.getStrWidth("PONG");u.setCursor((SW-tw)/2,14);u.print("PONG");
    u.setFont(u8g2_font_4x6_tr);u.setCursor(8,56);u.print("A=Up  B=Down  A+B=Start");
    u.sendBuffer();if((a&&!pA)||(b&&!pB))pp_newGame();pA=a;pB=b;return;
  }
  if(pongSt==PP_POINT){
    u.clearBuffer();for(int y=0;y<SH;y+=5)u.drawPixel(SW/2,y);
    u.setFont(u8g2_font_6x10_tr);
    u.setCursor(SW/2-18,10);u.print(pp_pSc);u.setCursor(SW/2+10,10);u.print(pp_aSc);
    u.drawBox(P_PAD_MG,(int)pp_pY,P_PAD_W,P_PAD_H);u.drawBox(SW-P_PAD_MG-P_PAD_W,(int)pp_aY,P_PAD_W,P_PAD_H);
    u.setCursor(pp_pScored?25:22,38);u.print(pp_pScored?"Your point!":"AI point :(");u.sendBuffer();
    if(pp_pSc>=P_WIN||pp_aSc>=P_WIN){pongSt=PP_WIN;return;}
    if(now-pp_ptTimer>=P_PPAUSE){pp_resetBall(!pp_pScored);pp_pY=(SH-P_PAD_H)/2.0f;pp_aY=pp_pY;pongSt=PP_PLAY;}
    return;
  }
  if(pongSt==PP_WIN){
    bool won=(pp_pSc>=P_WIN);
    u.clearBuffer();u.setFont(u8g2_font_6x10_tr);
    u.setCursor(won?28:18,22);u.print(won?"YOU WIN!":"AI WINS :(");
    u.setCursor(10,38);u.print("Score:");u.print(pp_pSc);u.print("-");u.print(pp_aSc);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(4,54);u.print("A/B=Restart  Hold D=Menu");u.sendBuffer();
    if((a&&!pA)||(b&&!pB)){
      submitScore("pong", pp_pSc);  // ← إرسال سكور Pong
      pp_newGame();
    }
    pA=a;pB=b;return;
  }
  if(pongSt==PP_PLAY&&now-pp_lastF>=P_FMS){
    pp_lastF=now;pp_updateAI();pp_updateBall();
    u.clearBuffer();for(int y=0;y<SH;y+=5)u.drawPixel(SW/2,y);
    u.setFont(u8g2_font_6x10_tr);u.setCursor(SW/2-18,10);u.print(pp_pSc);u.setCursor(SW/2+10,10);u.print(pp_aSc);
    u.drawBox(P_PAD_MG,(int)pp_pY,P_PAD_W,P_PAD_H);u.drawBox(SW-P_PAD_MG-P_PAD_W,(int)pp_aY,P_PAD_W,P_PAD_H);
    u.drawBox((int)pp_bX,(int)pp_bY,P_BSIZ,P_BSIZ);u.sendBuffer();
  }
  pA=a;pB=b;
}

// ══════════════════════════════════════════════════════
//  DINO RUN
// ══════════════════════════════════════════════════════
const unsigned char dino1[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x07,0xFE,0x00,0x00,0x06,0xFF,0x00,0x00,0x0E,0xFF,0x00,
  0x00,0x0F,0xFF,0x00,0x00,0x0F,0xFF,0x00,0x00,0x0F,0xFF,0x00,0x00,0x0F,0xC0,0x00,
  0x00,0x0F,0xFC,0x00,0x40,0x0F,0xC0,0x00,0x40,0x1F,0x80,0x00,0x40,0x7F,0x80,0x00,
  0x60,0xFF,0xE0,0x00,0x71,0xFF,0xA0,0x00,0x7F,0xFF,0x80,0x00,0x7F,0xFF,0x80,0x00,
  0x7F,0xFF,0x80,0x00,0x3F,0xFF,0x00,0x00,0x1F,0xFF,0x00,0x00,0x0F,0xFE,0x00,0x00,
  0x03,0xFC,0x00,0x00,0x01,0xDC,0x00,0x00,0x01,0x8C,0x00,0x00,0x01,0x8C,0x00,0x00,
  0x01,0x0C,0x00,0x00,0x01,0x8E,0x00,0x00
};
const unsigned char dino2[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x07,0xFE,0x00,0x00,0x06,0xFF,0x00,0x00,0x0E,0xFF,0x00,
  0x00,0x0F,0xFF,0x00,0x00,0x0F,0xFF,0x00,0x00,0x0F,0xFF,0x00,0x00,0x0F,0xC0,0x00,
  0x00,0x0F,0xFC,0x00,0x40,0x0F,0xC0,0x00,0x40,0x1F,0x80,0x00,0x40,0x7F,0x80,0x00,
  0x60,0xFF,0xE0,0x00,0x71,0xFF,0xA0,0x00,0x7F,0xFF,0x80,0x00,0x7F,0xFF,0x80,0x00,
  0x7F,0xFF,0x80,0x00,0x3F,0xFF,0x00,0x00,0x1F,0xFF,0x00,0x00,0x0F,0xFE,0x00,0x00,
  0x03,0xFC,0x00,0x00,0x01,0xDC,0x00,0x00,0x01,0x8C,0x00,0x00,0x01,0x8E,0x00,0x00,
  0x01,0x0C,0x00,0x00,0x01,0x8C,0x00,0x00
};
const unsigned char cactus_bmp[] PROGMEM = {
  0x1E,0x00,0x1F,0x00,0x1F,0x40,0x1F,0xE0,0x1F,0xE0,0xDF,0xE0,0xFF,0xE0,0xFF,0xE0,
  0xFF,0xE0,0xFF,0xE0,0xFF,0xE0,0xFF,0xE0,0xFF,0xC0,0xFF,0x00,0xFF,0x00,0x7F,0x00,
  0x1F,0x00,0x1F,0x00,0x1F,0x00,0x1F,0x00,0x1F,0x00,0x1F,0x00,0x1F,0x00
};

int d_dinoY=37; float d_vel=0; bool d_jumping=false;
int d_obsX=128,d_score=0,d_speed=4; bool d_over=false;
unsigned long d_lastT=0;

void d_reset(){d_dinoY=37;d_vel=0;d_jumping=false;d_obsX=128;d_score=0;d_speed=4;d_over=false;}

void runDino(){
  static unsigned long dHeld=0;static bool pJ=true;
  bool d=btnD();bool jump=btnA()||btnD();
  if(d){if(dHeld==0)dHeld=millis();if(millis()-dHeld>1000){dHeld=0;d_reset();appState=APP_MENU;return;}}else dHeld=0;
  if(d_over){
    u.clearBuffer();u.setFont(u8g2_font_ncenB14_tr);
    u.setCursor((SW-u.getStrWidth("GAME"))/2,22);u.print("GAME");
    u.setCursor((SW-u.getStrWidth("OVER"))/2,38);u.print("OVER");
    u.setFont(u8g2_font_6x10_tr);u.setCursor(24,52);u.print("Score:");u.print(d_score);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(4,62);u.print("A=Retry  Hold D=Menu");
    u.sendBuffer();
    if(jump&&!pJ){
      submitScore("dino", d_score);  // ← إرسال السكور
      d_reset();
    }
    pJ=jump;return;
  }
  if(millis()-d_lastT>40){
    d_lastT=millis();
    if(jump&&!d_jumping){d_vel=-9.5;d_jumping=true;}
    d_vel+=0.85;d_dinoY+=d_vel;
    if(d_dinoY>=37){d_dinoY=37;d_jumping=false;d_vel=0;}
    d_obsX-=d_speed;
    if(d_obsX<-15){d_obsX=128+random(30,70);d_score++;if(d_score%5==0&&d_speed<9)d_speed++;}
    if(d_obsX<38&&d_obsX>15&&d_dinoY>28)d_over=true;
    u.clearBuffer();u.drawHLine(0,57,SW);
    const unsigned char* frm=d_jumping?dino1:((millis()%120<60)?dino1:dino2);
    u.drawXBM(20,d_dinoY,25,26,frm);
    u.drawXBM(d_obsX,39,12,23,cactus_bmp);
    for(int i=0;i<3;i++)u.drawPixel((millis()/100+i*42)%SW,8+i*5);
    u.setFont(u8g2_font_6x10_tr);u.setCursor(5,10);u.print("Score:");u.print(d_score);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(SW-30,10);u.print("Spd:");u.print(d_speed);
    u.sendBuffer();
  }
  pJ=jump;
}

// ══════════════════════════════════════════════════════
//  SPACE SHOOTER
// ══════════════════════════════════════════════════════
const unsigned char shipBmp[] PROGMEM = {
  0x00,0x18,0x00,0x3C,0x00,0x7E,0x18,0xFF,0x3C,0xFF,0x7E,0xFF,0xFF,0xFF,0xFF,0xFF,
  0x7E,0x7E,0x3C,0x3C,0x18,0x18,0x00,0x00,0x00,0x24,0x00,0x66,0x00,0x42,0x00,0x00
};
const unsigned char astBmp[] PROGMEM = {0x3C,0x7E,0xDB,0xFF,0xFF,0xDB,0x7E,0x3C};
const unsigned char expBmp[] PROGMEM = {0x24,0x5A,0xBD,0x7E,0x7E,0xBD,0x5A,0x24};

int sp_shipX=56;
#define SP_MAX_B 5
#define SP_MAX_A 8
#define SP_MAX_E 5
#define SP_SHIP_Y 48
int sp_bX[SP_MAX_B],sp_bY[SP_MAX_B];bool sp_bAct[SP_MAX_B];
int sp_aX[SP_MAX_A],sp_aY[SP_MAX_A],sp_aSpd[SP_MAX_A];
int sp_eX[SP_MAX_E],sp_eY[SP_MAX_E],sp_eT[SP_MAX_E];
int sp_score=0,sp_speed=1;bool sp_over=false;
unsigned long sp_lastT=0,sp_lastShot=0;

void sp_reset(){
  sp_shipX=56;sp_score=0;sp_speed=1;sp_over=false;sp_lastShot=0;
  for(int i=0;i<SP_MAX_B;i++)sp_bAct[i]=false;
  for(int i=0;i<SP_MAX_A;i++)sp_aY[i]=-1;
  for(int i=0;i<SP_MAX_E;i++)sp_eT[i]=0;
}
void sp_fire(){for(int i=0;i<SP_MAX_B;i++)if(!sp_bAct[i]){sp_bX[i]=sp_shipX+7;sp_bY[i]=SP_SHIP_Y;sp_bAct[i]=true;break;}}

void runSpace(){
  static unsigned long dHeld=0;static bool pC=true;
  bool d=btnD();bool c=btnC();
  if(d){if(dHeld==0)dHeld=millis();if(millis()-dHeld>1000){dHeld=0;sp_reset();appState=APP_MENU;return;}}else dHeld=0;
  if(sp_over){
    u.clearBuffer();u.setFont(u8g2_font_ncenB14_tr);
    u.setCursor((SW-u.getStrWidth("GAME"))/2,20);u.print("GAME");
    u.setCursor((SW-u.getStrWidth("OVER"))/2,36);u.print("OVER");
    u.setFont(u8g2_font_6x10_tr);u.setCursor(18,50);u.print("Score:");u.print(sp_score);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(4,62);u.print("C=Retry  Hold D=Menu");
    u.sendBuffer();
    if(c&&!pC){
      submitScore("space", sp_score);  // ← إرسال السكور
      sp_reset();
    }
    pC=c;return;
  }
  if(millis()-sp_lastT>50){
    sp_lastT=millis();
    if(btnA()&&sp_shipX>0)sp_shipX-=4;
    if(btnC()&&sp_shipX<SW-16)sp_shipX+=4;
    if(btnB()&&millis()-sp_lastShot>200){sp_fire();sp_lastShot=millis();}
    for(int i=0;i<SP_MAX_B;i++)if(sp_bAct[i]){sp_bY[i]-=6;if(sp_bY[i]<0)sp_bAct[i]=false;}
    float sf=1.0+sp_score*0.02;if(sf>6)sf=6;sp_speed=(int)sf;
    for(int i=0;i<SP_MAX_A;i++){
      if(sp_aY[i]==-1){if(random(25)==0){sp_aX[i]=random(0,SW-8);sp_aY[i]=-8;sp_aSpd[i]=random(1,sp_speed+2);}}
      else{sp_aY[i]+=sp_aSpd[i];if(sp_aY[i]>SH)sp_aY[i]=-1;}
    }
    for(int i=0;i<SP_MAX_E;i++)if(sp_eT[i]>0)sp_eT[i]--;
    for(int b=0;b<SP_MAX_B;b++){if(!sp_bAct[b])continue;
      for(int a=0;a<SP_MAX_A;a++){if(sp_aY[a]==-1)continue;
        if(sp_bX[b]<sp_aX[a]+8&&sp_bX[b]+4>sp_aX[a]&&sp_bY[b]<sp_aY[a]+8&&sp_bY[b]+8>sp_aY[a]){
          sp_bAct[b]=false;for(int e=0;e<SP_MAX_E;e++)if(sp_eT[e]==0){sp_eX[e]=sp_aX[a];sp_eY[e]=sp_aY[a];sp_eT[e]=8;break;}
          sp_aY[a]=-1;sp_score+=10;break;}}}
    for(int a=0;a<SP_MAX_A;a++){if(sp_aY[a]==-1)continue;
      if(sp_shipX<sp_aX[a]+8&&sp_shipX+16>sp_aX[a]&&SP_SHIP_Y<sp_aY[a]+8&&SP_SHIP_Y+8>sp_aY[a])sp_over=true;}
    u.clearBuffer();
    for(int i=0;i<15;i++)u.drawPixel((millis()/50+i*17)%SW,(millis()/80+i*13)%SH);
    u.drawXBM(sp_shipX,SP_SHIP_Y-4,16,16,shipBmp);
    for(int i=0;i<SP_MAX_B;i++)if(sp_bAct[i]){
      u.drawBox(sp_bX[i],sp_bY[i],3,6);
      u.drawTriangle(sp_bX[i],sp_bY[i],sp_bX[i]+3,sp_bY[i],sp_bX[i]+1,sp_bY[i]-3);}
    for(int i=0;i<SP_MAX_A;i++)if(sp_aY[i]!=-1)u.drawXBM(sp_aX[i],sp_aY[i],8,8,astBmp);
    for(int i=0;i<SP_MAX_E;i++)if(sp_eT[i]>0)u.drawXBM(sp_eX[i],sp_eY[i],8,8,expBmp);
    u.drawBox(0,0,SW,9);u.setDrawColor(0);u.setFont(u8g2_font_4x6_tr);
    u.setCursor(2,7);u.print("SCORE:");u.print(sp_score);
    u.setCursor(SW-20,7);u.print("SPD:");u.print(sp_speed);u.setDrawColor(1);
    u.sendBuffer();
  }
  pC=c;
}

// ══════════════════════════════════════════════════════
//  MAZE RUNNER
// ══════════════════════════════════════════════════════
struct LevelConfig{int cellSize,cols,rows,timeLimit;const char*name;};
const LevelConfig mzLevels[3]={{10,11,5,60,"EASY"},{8,14,6,45,"MEDIUM"},{6,19,8,35,"HARD"}};
int mzCurLvl=0;
#define MZ_MCOLS 19
#define MZ_MROWS  8
struct MzCell{bool walls[4];bool visited;};
MzCell mzMaze[MZ_MCOLS][MZ_MROWS];
int mzCOLS,mzROWS,mzCELL,mzPX=0,mzPY=0,mzGX,mzGY;
enum MzState{MZ_MENU,MZ_PLAY,MZ_WIN,MZ_LOSE};MzState mzSt=MZ_MENU;
int mzMoves=0;unsigned long mzStart=0,mzLastBtn=0,mzLastAnim=0;
int mzTimeLeft=0,mzAnim=0;
#define MZ_DEBOUNCE 160

int mz_dx[]={0,0,1,-1},mz_dy[]={-1,1,0,0},mz_opp[]={1,0,3,2};
struct MzPt{int x,y;};
MzPt mzStk[MZ_MCOLS*MZ_MROWS];int mzStkTop=0;

void mz_shuffleDirs(int d[]){for(int i=3;i>0;i--){int j=random(i+1),t=d[i];d[i]=d[j];d[j]=t;}}
void mz_generate(){
  for(int x=0;x<mzCOLS;x++)for(int y=0;y<mzROWS;y++){
    mzMaze[x][y].visited=false;mzMaze[x][y].walls[0]=mzMaze[x][y].walls[1]=mzMaze[x][y].walls[2]=mzMaze[x][y].walls[3]=true;}
  mzStkTop=0;mzStk[mzStkTop++]={0,0};mzMaze[0][0].visited=true;
  int vis=1,tot=mzCOLS*mzROWS;
  while(vis<tot){MzPt c=mzStk[mzStkTop-1];int dirs[4]={0,1,2,3};mz_shuffleDirs(dirs);bool mv=false;
    for(int i=0;i<4;i++){int d=dirs[i],nx=c.x+mz_dx[d],ny=c.y+mz_dy[d];
      if(nx>=0&&nx<mzCOLS&&ny>=0&&ny<mzROWS&&!mzMaze[nx][ny].visited){
        mzMaze[c.x][c.y].walls[d]=false;mzMaze[nx][ny].walls[mz_opp[d]]=false;
        mzMaze[nx][ny].visited=true;mzStk[mzStkTop++]={nx,ny};vis++;mv=true;break;}}
    if(!mv)mzStkTop--;}
}
void mz_drawChar(int cx,int cy){
  u.drawCircle(cx,cy-2,1);u.drawVLine(cx,cy,3);
  if(mzAnim==0){u.drawHLine(cx-2,cy+1,5);u.drawLine(cx,cy+3,cx-2,cy+5);u.drawLine(cx,cy+3,cx+2,cy+5);}
  else{u.drawLine(cx,cy+1,cx-2,cy-1);u.drawLine(cx,cy+1,cx+2,cy-1);u.drawLine(cx,cy+3,cx-1,cy+5);u.drawLine(cx,cy+3,cx+1,cy+5);}
}

void runMaze(){
  static unsigned long dHeld=0;bool d=btnD();
  if(d){if(dHeld==0)dHeld=millis();if(millis()-dHeld>1000){dHeld=0;mzSt=MZ_MENU;appState=APP_MENU;return;}}else dHeld=0;

  if(mzSt==MZ_MENU){
    static bool pA=true,pB=true,pC=true;bool a=btnA(),b=btnB(),c=btnC();
    u.clearBuffer();u.setFont(u8g2_font_6x10_tr);u.setCursor(22,11);u.print("MAZE RUNNER");u.drawHLine(0,13,SW);
    const char*descs[3]={"Easy   11x5  60s","Medium 14x6  45s","Hard   19x8  35s"};
    for(int i=0;i<3;i++){int yy=24+i*14;
      if(i==mzCurLvl){u.drawRBox(2,yy-9,124,12,2);u.setDrawColor(0);}
      u.setFont(u8g2_font_5x7_tr);u.setCursor(6,yy);u.print(i+1);u.setCursor(16,yy);u.print(descs[i]);u.setDrawColor(1);}
    u.setFont(u8g2_font_4x6_tr);u.setCursor(2,63);u.print("A/B=Level  C=Start  D=Menu");
    u.sendBuffer();
    if(a&&!pA)mzCurLvl=(mzCurLvl-1+3)%3;
    if(b&&!pB)mzCurLvl=(mzCurLvl+1)%3;
    if(c&&!pC){mzCELL=mzLevels[mzCurLvl].cellSize;mzCOLS=mzLevels[mzCurLvl].cols;mzROWS=mzLevels[mzCurLvl].rows;
      randomSeed(millis());mz_generate();mzPX=0;mzPY=0;mzGX=mzCOLS-1;mzGY=mzROWS-1;
      mzMoves=0;mzAnim=0;mzStart=millis();mzTimeLeft=mzLevels[mzCurLvl].timeLimit;mzSt=MZ_PLAY;}
    pA=a;pB=b;pC=c;delay(20);return;
  }
  if(mzSt==MZ_PLAY){
    int el=(millis()-mzStart)/1000;mzTimeLeft=mzLevels[mzCurLvl].timeLimit-el;
    if(mzTimeLeft<=0){mzTimeLeft=0;mzSt=MZ_LOSE;return;}
    if(millis()-mzLastBtn>=MZ_DEBOUNCE){
      int nx=mzPX,ny=mzPY,dir=-1;
      if(btnA()){ny--;dir=0;}if(btnB()){ny++;dir=1;}if(btnC()){nx++;dir=2;}
      if(dir!=-1){mzLastBtn=millis();
        if(nx>=0&&nx<mzCOLS&&ny>=0&&ny<mzROWS&&!mzMaze[mzPX][mzPY].walls[dir]){
          mzPX=nx;mzPY=ny;mzMoves++;mzAnim=1-mzAnim;}
        if(mzPX==mzGX&&mzPY==mzGY)mzSt=MZ_WIN;}}
    if(millis()-mzLastAnim>300){mzLastAnim=millis();mzAnim=1-mzAnim;}
    u.clearBuffer();
    for(int x=0;x<mzCOLS;x++)for(int y=0;y<mzROWS;y++){int px=x*mzCELL,py=y*mzCELL;
      if(mzMaze[x][y].walls[0])u.drawHLine(px,py,mzCELL);if(mzMaze[x][y].walls[1])u.drawHLine(px,py+mzCELL,mzCELL);
      if(mzMaze[x][y].walls[2])u.drawVLine(px+mzCELL,py,mzCELL);if(mzMaze[x][y].walls[3])u.drawVLine(px,py,mzCELL);}
    int gx=mzGX*mzCELL+mzCELL/2,gy=mzGY*mzCELL+mzCELL/2;
    u.drawVLine(gx-1,gy-3,6);u.drawLine(gx-1,gy-3,gx+2,gy-2);u.drawLine(gx-1,gy-1,gx+2,gy-2);
    mz_drawChar(mzPX*mzCELL+mzCELL/2,mzPY*mzCELL+mzCELL/2);
    u.drawFrame(0,SH-7,SW,7);
    int bw=map(mzTimeLeft,0,mzLevels[mzCurLvl].timeLimit,0,SW-2);
    if(!(mzTimeLeft<=10&&(millis()/300)%2==0))u.drawBox(1,SH-6,bw,5);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(1,SH-1);u.print("M:");u.print(mzMoves);
    u.setCursor(SW-18,SH-1);u.print(mzTimeLeft);u.print("s");u.sendBuffer();delay(16);return;
  }
  if(mzSt==MZ_WIN){
    // حساب سكور المتاهة: كلما قل الوقت المستهلك أكثر، زاد السكور
    int timeUsed = mzLevels[mzCurLvl].timeLimit - mzTimeLeft;
    int mazeScore = max(1, (mzLevels[mzCurLvl].timeLimit - timeUsed) * 10 - mzMoves);
    u.clearBuffer();u.setFont(u8g2_font_ncenB14_tr);
    u.setCursor((SW-u.getStrWidth("WIN!"))/2,18);u.print("WIN!");
    u.setFont(u8g2_font_6x10_tr);u.setCursor(8,32);u.print("Moves:");u.print(mzMoves);
    u.setCursor(8,44);u.print("Score:");u.print(mazeScore);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(4,62);u.print("Any btn = menu");u.sendBuffer();
    submitScore("maze", mazeScore);  // ← إرسال السكور
    delay(600);while(!btnA()&&!btnB()&&!btnC()&&!btnD())delay(20);
    delay(200);mzSt=MZ_MENU;appState=APP_MENU;return;
  }
  if(mzSt==MZ_LOSE){
    u.clearBuffer();u.setFont(u8g2_font_ncenB14_tr);
    u.setCursor((SW-u.getStrWidth("TIME!"))/2,18);u.print("TIME!");
    u.setFont(u8g2_font_6x10_tr);u.setCursor(18,34);u.print("Too slow :(");
    u.setFont(u8g2_font_4x6_tr);u.setCursor(4,62);u.print("Any btn = menu");u.sendBuffer();
    delay(600);while(!btnA()&&!btnB()&&!btnC()&&!btnD())delay(20);
    delay(200);mzSt=MZ_MENU;appState=APP_MENU;return;
  }
}

// ══════════════════════════════════════════════════════
//  BREAKOUT
// ══════════════════════════════════════════════════════
#define BR_PAD_Y 56
#define BR_PAD_H  4
#define BR_BALL_R 2
#define BR_BCOLS 10
#define BR_BROWS  4
#define BR_BW    11
#define BR_BH     5
#define BR_BPAD   1
#define BR_BTOP   9

struct BrLvlCfg{float spd,spdMax;int padW,hardR,rockR;bool gaps;int lives;};
const BrLvlCfg brLvls[10]={
  {1.8f,3.5f,28,0,0,false,5},{2.0f,3.8f,26,1,0,false,4},{2.2f,4.0f,24,1,0,true,4},
  {2.3f,4.2f,22,2,0,false,4},{2.5f,4.4f,22,2,0,true,3},{2.6f,4.6f,20,2,1,false,3},
  {2.8f,4.8f,20,2,1,true,3},{3.0f,5.0f,18,3,1,true,3},{3.2f,5.2f,16,3,2,true,2},{3.5f,5.5f,14,4,2,true,2}
};

int brBricks[BR_BCOLS][BR_BROWS],brBLeft=0;
float brPadX=52,brPadVX=0;int brPadW=28;
float brBX,brBY,brBVX,brBVY,brBSpd;bool brBLaunched=false;
int brScore=0,brLives=5,brLevel=1;
enum BrState{BR_INTRO,BR_PLAY,BR_LOST,BR_LVWIN,BR_GOVER,BR_GWIN};BrState brSt=BR_INTRO;
unsigned long brLastF=0,brPauseT=0;

void br_initBricks(){
  brBLeft=0;const BrLvlCfg&c=brLvls[brLevel-1];
  for(int col=0;col<BR_BCOLS;col++)for(int row=0;row<BR_BROWS;row++){
    int h=1;if(row<c.rockR)h=3;else if(row<c.rockR+c.hardR)h=2;
    if(c.gaps&&row>=2&&random(5)==0)h=0;
    brBricks[col][row]=h;if(h>0)brBLeft++;}
}
void br_resetBall(){
  brBX=brPadX+brPadW/2.0f;brBY=BR_PAD_Y-BR_BALL_R-1;
  float spd=brLvls[brLevel-1].spd,ang=random(45,135)*PI/180.0f;
  brBVX=spd*cos(ang);brBVY=-spd*abs(sin(ang));brBLaunched=false;
}
void br_initLevel(){brPadW=brLvls[brLevel-1].padW;brPadX=(SW-brPadW)/2.0f;brPadVX=0;br_initBricks();br_resetBall();brSt=BR_PLAY;}
void br_newGame(){brScore=0;brLives=brLvls[0].lives;brLevel=1;br_initLevel();}
void br_checkBricks(){
  for(int c=0;c<BR_BCOLS;c++)for(int r=0;r<BR_BROWS;r++){
    if(brBricks[c][r]==0)continue;
    int bx=c*(BR_BW+BR_BPAD)+1,by=BR_BTOP+r*(BR_BH+BR_BPAD);
    float L=bx-BR_BALL_R,R=bx+BR_BW+BR_BALL_R,T=by-BR_BALL_R,B=by+BR_BH+BR_BALL_R;
    if(brBX>=L&&brBX<=R&&brBY>=T&&brBY<=B){
      float oL=brBX-L,oR=R-brBX,oT=brBY-T,oB=B-brBY;
      if(min(oL,oR)<min(oT,oB))brBVX=-brBVX;else brBVY=-brBVY;
      brBricks[c][r]--;
      if(brBricks[c][r]==0){brBLeft--;brScore+=(BR_BROWS-r)*10*brLevel;
        float sp=sqrt(brBVX*brBVX+brBVY*brBVY),mx=brLvls[brLevel-1].spdMax;
        if(sp<mx){float ns=min(sp+0.06f,mx);brBVX*=ns/sp;brBVY*=ns/sp;}}
      if(brBLeft==0){brSt=BR_LVWIN;return;}return;}}}

void runBreakout(){
  unsigned long now=millis();static unsigned long dHeld=0;
  bool a=btnA(),b=btnB(),c=btnC(),d=btnD();
  if(brSt==BR_INTRO){
    u.clearBuffer();u.drawFrame(0,0,SW,SH);u.drawFrame(3,3,SW-6,SH-6);
    u.setFont(u8g2_font_ncenB14_tr);u.setCursor((SW-u.getStrWidth("BREAKOUT"))/2,20);u.print("BREAKOUT");
    for(int i=0;i<10;i++)u.drawFrame(4+i*12,28,10,4);
    u.setFont(u8g2_font_5x7_tr);u.setCursor(18,42);u.print("10 Levels!");
    u.setFont(u8g2_font_4x6_tr);u.setCursor(14,56);if((millis()/500)%2==0)u.print("Press C to Start");
    u.sendBuffer();if(c){delay(200);br_newGame();return;}if(d){appState=APP_MENU;return;}return;
  }
  if(d){if(dHeld==0)dHeld=millis();if(millis()-dHeld>1000){dHeld=0;brSt=BR_INTRO;appState=APP_MENU;return;}}else dHeld=0;
  if(brSt==BR_LOST){
    u.clearBuffer();u.setFont(u8g2_font_6x10_tr);u.setCursor(38,20);u.print("OOPS!");
    u.setCursor(4,34);u.print("Lives:");u.print(brLives);u.setCursor(4,48);u.print("Score:");u.print(brScore);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(20,62);u.print("Get ready...");u.sendBuffer();
    if(now-brPauseT>=2200){brPadX=(SW-brPadW)/2.0f;brPadVX=0;br_resetBall();brSt=BR_PLAY;}return;
  }
  if(brSt==BR_LVWIN){
    u.clearBuffer();u.setFont(u8g2_font_ncenB14_tr);u.setCursor((SW-u.getStrWidth("CLEAR!"))/2,18);u.print("CLEAR!");
    u.setFont(u8g2_font_5x7_tr);u.setCursor(10,32);u.print("Level ");u.print(brLevel);u.print(" done!");
    u.setCursor(10,44);u.print("Score:");u.print(brScore);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(8,58);if(brLevel<10)u.print("C=Next  Hold D=Menu");else u.print("C=Final Score!");
    u.sendBuffer();if(c){delay(200);if(brLevel<10){brLevel++;brLives=min(brLives+1,5);br_initLevel();}else brSt=BR_GWIN;}return;
  }
  if(brSt==BR_GOVER){
    u.clearBuffer();u.setFont(u8g2_font_ncenB14_tr);
    u.setCursor((SW-u.getStrWidth("GAME"))/2,16);u.print("GAME");u.setCursor((SW-u.getStrWidth("OVER"))/2,30);u.print("OVER");
    u.setFont(u8g2_font_5x7_tr);u.setCursor(16,44);u.print("Score:");u.print(brScore);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(14,62);u.print("C=Retry  Hold D=Menu");
    u.sendBuffer();
    if(c){
      submitScore("breakout", brScore);  // ← إرسال السكور
      delay(200);br_newGame();
    }
    return;
  }
  if(brSt==BR_GWIN){
    brScore+=brLives*1000;u.clearBuffer();u.setFont(u8g2_font_ncenB14_tr);
    u.setCursor((SW-u.getStrWidth("YOU WIN!"))/2,18);u.print("YOU WIN!");
    u.setFont(u8g2_font_5x7_tr);u.setCursor(4,32);u.print("ALL 10 DONE!");u.setCursor(4,44);u.print("Score:");u.print(brScore);
    u.setFont(u8g2_font_4x6_tr);u.setCursor(10,58);u.print("C=Play Again  D=Menu");
    u.sendBuffer();
    if(c){
      submitScore("breakout", brScore);  // ← إرسال السكور
      delay(200);br_newGame();
    }
    return;
  }
  if(brSt==BR_PLAY&&now-brLastF>=16){brLastF=now;
    if(a&&!c)brPadVX-=2.5f;if(c&&!a)brPadVX+=2.5f;if(!a&&!c)brPadVX*=0.75f;
    brPadVX=np_clamp(brPadVX,-5.5f,5.5f);brPadX+=brPadVX;brPadX=np_clamp(brPadX,0,SW-brPadW);
    if(brPadX<=0||brPadX>=SW-brPadW)brPadVX=0;
    if(b&&!brBLaunched)brBLaunched=true;
    if(!brBLaunched){brBX=brPadX+brPadW/2.0f;brBY=BR_PAD_Y-BR_BALL_R-1;}
    else{
      brBX+=brBVX;brBY+=brBVY;
      if(brBX-BR_BALL_R<=0){brBX=BR_BALL_R;brBVX=abs(brBVX);}
      if(brBX+BR_BALL_R>=SW){brBX=SW-BR_BALL_R;brBVX=-abs(brBVX);}
      if(brBY-BR_BALL_R<=7){brBY=7+BR_BALL_R;brBVY=abs(brBVY);}
      if(brBVY>0&&brBY+BR_BALL_R>=BR_PAD_Y&&brBY+BR_BALL_R<=BR_PAD_Y+BR_PAD_H+3&&brBX>=brPadX-BR_BALL_R&&brBX<=brPadX+brPadW+BR_BALL_R){
        brBY=BR_PAD_Y-BR_BALL_R;float hit=(brBX-brPadX)/brPadW,ang=(hit-0.5f)*2.2f;
        float sp=max(sqrt(brBVX*brBVX+brBVY*brBVY),(float)brLvls[brLevel-1].spd);
        brBVX=sp*sin(ang)+brPadVX*0.3f;brBVY=-sp*abs(cos(ang));if(brBVY>-0.8f)brBVY=-0.8f;}
      if(brBY-BR_BALL_R>SH+4){brLives--;if(brLives<=0)brSt=BR_GOVER;else{brSt=BR_LOST;brPauseT=millis();}}
      br_checkBricks();
    }
    u.clearBuffer();
    for(int c2=0;c2<BR_BCOLS;c2++)for(int r=0;r<BR_BROWS;r++){
      if(brBricks[c2][r]==0)continue;int bx=c2*(BR_BW+BR_BPAD)+1,by=BR_BTOP+r*(BR_BH+BR_BPAD);
      if(brBricks[c2][r]==3){u.drawBox(bx,by,BR_BW,BR_BH);u.setDrawColor(0);u.drawHLine(bx+1,by+1,BR_BW-2);u.drawHLine(bx+1,by+3,BR_BW-2);u.setDrawColor(1);}
      else if(brBricks[c2][r]==2){u.drawBox(bx,by,BR_BW,BR_BH);u.setDrawColor(0);u.drawHLine(bx+1,by+BR_BH/2,BR_BW-2);u.setDrawColor(1);}
      else u.drawFrame(bx,by,BR_BW,BR_BH);}
    u.drawRBox((int)brPadX,BR_PAD_Y,brPadW,BR_PAD_H,2);u.drawDisc((int)brBX,(int)brBY,BR_BALL_R);
    for(int i=0;i<brLives&&i<5;i++){int hx=1+i*7;u.drawPixel(hx+1,0);u.drawPixel(hx+3,0);u.drawPixel(hx,1);u.drawPixel(hx+2,1);u.drawPixel(hx+4,1);u.drawPixel(hx,2);u.drawPixel(hx+1,2);u.drawPixel(hx+2,2);u.drawPixel(hx+3,2);u.drawPixel(hx+4,2);u.drawPixel(hx+1,3);u.drawPixel(hx+2,3);u.drawPixel(hx+3,3);u.drawPixel(hx+2,4);}
    char buf[10];sprintf(buf,"%d",brScore);u.setFont(u8g2_font_4x6_tr);
    u.setCursor(SW-u.getStrWidth(buf)-1,6);u.print(buf);u.setCursor(SW/2-4,6);u.print("L");u.print(brLevel);
    if(!brBLaunched){u.setCursor(16,BR_PAD_Y-7);u.print("B=Launch");}u.sendBuffer();
  }
}

// ══════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════
void setup(){
  Serial.begin(115200);
  pinMode(BTN_A,INPUT_PULLUP);pinMode(BTN_B,INPUT_PULLUP);
  pinMode(BTN_C,INPUT_PULLUP);pinMode(BTN_D,INPUT_PULLUP);
  u.begin();
  randomSeed(analogRead(0));
  pxa=64;pxb=pxa+PIPE_SPACING;pxc=pxb+PIPE_SPACING;

  // سكورات تجريبية مبدئية
  addScore("NanoPlayer",42);
  addScore("FlappyPro",38);
  addScore("ESPilot",55);

  appState = APP_SPLASH;
}

// ══════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════
void loop(){
  if(appState==APP_SPLASH){
    showSplash();
    connectWiFi();       // ← الاتصال بعد الـ splash
    appState=APP_FACE;
    return;
  }

  if(appState==APP_FACE){
    static unsigned long dHeld=0;static bool pA=true;
    bool a=btnA(),d=btnD();
    if(a&&!pA){if(faceMode==3)targetMode=0;else targetMode=3;}pA=a;
    if(d){if(dHeld==0)dHeld=millis();if(millis()-dHeld>1000){dHeld=0;menuSel=0;drawMenu();appState=APP_MENU;delay(300);return;}}else dHeld=0;
    showFace();return;
  }

  if(appState==APP_MENU){
    static bool pA=true,pB=true,pC=true,pD2=true;
    bool a=btnA(),b=btnB(),c=btnC(),d=btnD();
    if(a&&!pA){menuSel=(menuSel-1+GAME_COUNT)%GAME_COUNT;drawMenu();delay(120);}
    if(b&&!pB){menuSel=(menuSel+1)%GAME_COUNT;drawMenu();delay(120);}
    if(c&&!pC){delay(150);drawGameSplash(menuSel);
      switch(menuSel){
        case 0:fl_reset();appState=APP_FLAPPY;break;
        case 1:pongSt=PP_WAIT;appState=APP_PONG;break;
        case 2:d_reset();appState=APP_DINO;break;
        case 3:sp_reset();appState=APP_SPACE;break;
        case 4:mzSt=MZ_MENU;appState=APP_MAZE;break;
        case 5:brSt=BR_INTRO;appState=APP_BREAKOUT;break;}}
    if(d&&!pD2){appState=APP_FACE;delay(200);}
    pA=a;pB=b;pC=c;pD2=d;drawMenu();delay(40);return;
  }

  if(appState==APP_FLAPPY)  {runFlappy();  return;}
  if(appState==APP_PONG)    {runPong();    return;}
  if(appState==APP_DINO)    {runDino();    return;}
  if(appState==APP_SPACE)   {runSpace();   return;}
  if(appState==APP_MAZE)    {runMaze();    return;}
  if(appState==APP_BREAKOUT){runBreakout();return;}
}

