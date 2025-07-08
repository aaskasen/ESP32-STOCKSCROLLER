#include <WiFi.h>
#include <esp_wifi.h>                 // ← new
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_W     128
#define OLED_H      64
#define OLED_ADDR 0x3C
#define SDA_PIN     21
#define SCL_PIN     22
#define TOPBAR_H     8
#define FETCH_MS 60000

#define TICKER_SIZE   2          // 1 = 5×8 font, 2 = 10×16
#define SCROLL_DY     8          // vertical offset from topbar

#define BUTTON_PIN   0            // BOOT button on most ESP-WROOM-32 dev boards
const uint8_t SPEED_STEPS = 5;
const uint16_t FRAME_DELAY[SPEED_STEPS] = { 40, 30, 20, 12, 6 };  // ms per frame

const char* WIFI_SSID = "";
const char* WIFI_PASS = "";
const char* FINNHUB   = "";
const char* symbols[] = {"QQQ","SPY","VIX","AMZN","SOFI","PYPL","PLTR","NVDA"};
const uint8_t NSYM    = sizeof(symbols)/sizeof(symbols[0]);

/* market open indicator globals */
bool marketOpen = true;                // refreshed once per fetch cycle
const int16_t STATUS_Y = TOPBAR_H + SCROLL_DY + (TICKER_SIZE * 8) + 2; // row 34

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

/* ---------- globals ---------- */
String  tickerLine;
int16_t scrollX = OLED_W;
bool    wifiOK  = false;
bool    dataOK  = false;
int8_t  wifiErr = 0;
bool    needsScan  = false;
int     lastScanCnt = 0;
String  scanList[10];
int8_t  scanRSSI[10];
unsigned long lastFetch = 0;
uint8_t   speedIdx    = 0;        // index into FRAME_DELAY
unsigned long lastBtn = 0;        // debounce timer

/* ---------- helpers ---------- */
bool isMarketOpen()
{
  HTTPClient http;
  http.begin("https://finnhub.io/api/v1/stock/market/status?exchange=US&token=" + String(FINNHUB));
  int code = http.GET();
  if (code != 200) { http.end(); return true; }      // assume open on error

  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, http.getString())) { http.end(); return true; }
  http.end();

  const char* s = doc["market"];
  return (s && strcmp(s, "open") == 0);
}

void drawStatusBar()
{
  oled.fillRect(0,0,OLED_W,TOPBAR_H,SSD1306_BLACK);
  oled.setCursor(0,0);  oled.setTextSize(1);  oled.setTextColor(SSD1306_WHITE);
  oled.print("IP:");
  oled.print(wifiOK ? WiFi.localIP() : "0.0.0.0");
  oled.print(" ");
  oled.print(wifiOK ? "W" : String("E")+wifiErr);   // Wi-Fi state
  oled.print("/");
  oled.print(dataOK ? "D" : "API");
}

void drawMarquee()
{
  const int16_t cw     = 6 * TICKER_SIZE;
  const int16_t textW  = tickerLine.length() * cw;
  const int16_t y      = TOPBAR_H + SCROLL_DY;       // ticker row

  oled.fillRect(0, TOPBAR_H, OLED_W, OLED_H - TOPBAR_H, SSD1306_BLACK);
  oled.setTextWrap(false);

  /* ticker (size-2) */
  oled.setTextSize(TICKER_SIZE);
  oled.setCursor(scrollX, y);
  oled.print(tickerLine);
  oled.setCursor(scrollX + textW, y);
  oled.print(tickerLine);

  /* market-status line (size-1) */
  oled.setTextSize(1);
  if (!marketOpen) {
      oled.setCursor(0, STATUS_Y);
      oled.print("MARKET CLOSED");
  }

  oled.display();

  /* advance ticker */
  scrollX--;
  if (scrollX <= -textW) scrollX += textW;
}

void drawScanResults()
{
  oled.fillRect(0,TOPBAR_H,OLED_W,OLED_H-TOPBAR_H,SSD1306_BLACK);
  oled.setTextSize(1);

  if (lastScanCnt == 0) {                   // nothing found
      oled.setCursor(0, TOPBAR_H+10);
      oled.print("(no 2.4 GHz APs)");
      oled.display();
      return;
  }

  for (int i=0; i<lastScanCnt && i<6; i++) {
      int y = TOPBAR_H + 2 + i*9;
      oled.setCursor(0, y);
      oled.print(scanList[i].substring(0,12));        // clip to fit
      int bars = map(scanRSSI[i], -90, -30, 1, 5);
      for (int b=0; b<bars; b++) oled.fillRect(70+b*6, y+2, 4, 4, SSD1306_WHITE);
  }
  oled.display();
}

bool fetchQuotes()
{
  tickerLine = "";
  StaticJsonDocument<256> doc;
  HTTPClient http;

  for (uint8_t i=0;i<NSYM;i++) {
    String url = "https://finnhub.io/api/v1/quote?symbol="+String(symbols[i])+"&token="+FINNHUB;
    http.begin(url); http.setConnectTimeout(4000); http.setReuse(true);
    int code = http.GET();
    if (code!=200){ http.end(); return false; }
    if (deserializeJson(doc,http.getString())){ http.end(); return false; }
    http.end();
    float p = doc["c"] | 0.0, dp = doc["dp"] | 0.0;
    tickerLine += String(symbols[i]) + " "
                + String(p,2) + " "
                + (dp>=0?"+":"") + String(dp,2) + "%   ";
    delay(150);
  }
scrollX = OLED_W;        // start the new marquee just off-screen right
marketOpen = isMarketOpen();           // refresh open/closed flag
return true;
}

/* ---------- Wi-Fi ---------- */
void doWifiScan()
{
  WiFi.mode(WIFI_STA);                 // ensure we are in STA
  WiFi.disconnect(true);               // stop any pending join & clear DHCP
  delay(200);                          // give the PHY time to settle

  oled.fillRect(0,TOPBAR_H,OLED_W,OLED_H-TOPBAR_H,SSD1306_BLACK);
  oled.setCursor(0,TOPBAR_H+2); oled.setTextSize(1); oled.print("Scanning…");
  oled.display();

  /* passive scan, show hidden SSIDs */
  lastScanCnt = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true, /*max_ms_per_chan=*/150);

  if (lastScanCnt < 0) lastScanCnt = 0;
  if (lastScanCnt > 10) lastScanCnt = 10;

  for (int i = 0; i < lastScanCnt; i++) {
      scanList[i] = WiFi.SSID(i);
      scanRSSI[i] = WiFi.RSSI(i);
      Serial.printf("%2d  %-32s  RSSI %d dBm\n",
                    i, scanList[i].c_str(), scanRSSI[i]);
  }
  WiFi.scanDelete();                   // free RAM
}

void connectWiFi()
{
  /* Country cfg: cc, start-chan, nchan, policy */
  wifi_country_t se = { "SE", 1, 13, WIFI_COUNTRY_POLICY_AUTO };
  esp_wifi_set_country(&se);           // enable channels 1-13

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000)
      delay(300);

  wifiErr   = WiFi.status();
  wifiOK    = (wifiErr == WL_CONNECTED);
  needsScan = (wifiErr == WL_NO_SSID_AVAIL);   // E1

  if (needsScan) doWifiScan();
}

/* ---------- Arduino entry points ---------- */
void setup()
{
  Wire.begin(SDA_PIN, SCL_PIN);

  pinMode(BUTTON_PIN, INPUT_PULLUP);      // active-LOW pushbutton
  
  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.clearDisplay();
  Serial.begin(115200);

  connectWiFi();
  dataOK = wifiOK && fetchQuotes();

  drawStatusBar();
  oled.display();
}

bool buttonPressed()
{
  /* sample twice, 5 ms apart */
  if (digitalRead(BUTTON_PIN) == LOW) {
      delay(5);
      return digitalRead(BUTTON_PIN) == LOW;   // confirms sustained LOW
  }
  return false;
}

void loop()
{
  static unsigned long tScan = 0;
  unsigned long now = millis();

  /* -------- speed-button handler -------- */
if (buttonPressed() && now - lastBtn > 250) {
    speedIdx = (speedIdx + 1) % SPEED_STEPS;                 // cycle 0-4
    lastBtn  = now;
}

  if (needsScan) {                     // show SSID list instead of ticker
      if (now - tScan > 30000) {       // rescan every 30 s
          doWifiScan();
          tScan = now;
      }
      drawStatusBar();
      drawScanResults();
      delay(1000);
      return;
  }

  if (WiFi.status() != WL_CONNECTED) { // lost Wi-Fi → attempt reconnect
      wifiOK = false;
      connectWiFi();
      oled.clearDisplay();
  }

  if (wifiOK && now - lastFetch > FETCH_MS) {
      dataOK = fetchQuotes();
      lastFetch = now;
  }

  drawStatusBar();
  drawMarquee();
delay(FRAME_DELAY[speedIdx]);                // user-controlled speed                                // ~40 fps, smoother scroll
}
