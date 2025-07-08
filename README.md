# ESP32-STOCKSCROLLER
Stock Ticker Scroller for ESP-WROOM-32

* connects to Wi-Fi  
* pulls live quotes for eight symbols from **Finnhub** every 60 s  
* scrolls the prices across a 0 .96″ I²C SSD1306 OLED  
* shows **MARKET CLOSED** outside US trading hours  
* lets you cycle through five scroll-speeds with the *BOOT* button  
* reboots instantly with the *EN* button

<p align="center">
  <img src="docs/ticker_demo.gif" width="420">
</p>

---

## Bill of materials

| Qty | Part | Notes |
|-----|------|-------|
| 1 | **ESP-WROOM-32 dev board** | Any 30-pin or 38-pin module with the BOOT/EN buttons exposed |
| 1 | 0 .96″ SSD1306 I²C OLED | 128 × 64, 3-pin GND/VCC/SCL/SDA |
| – | USB-C or micro-USB cable | Must carry data, not just power |

> **Wiring** (default pins):  
> `OLED SDA → GPIO-21`, `OLED SCL → GPIO-22`, `VCC → 3.3 V`, `GND → GND`

---

## 1 · Arduino IDE setup

1. **Install Arduino IDE 2.x**  
2. **Add ESP32 core**  
   * File → *Preferences* → **Additional Board URLs**  
   * Paste:  
     ```
     https://espressif.github.io/arduino-esp32/package_esp32_index.json
     ```  
3. Tools → *Board Manager* → search **esp32** → **Install**  
4. Tools → *Board* → choose **ESP32 Dev Module**  
5. Tools → *Port* → pick your `/dev/cu.usbserial-…` (macOS) or COM x (Win)  
6. Install libraries *(Sketch → Include Library → Manage Libraries)*  
   * **Adafruit SSD1306**  
   * **Adafruit GFX**  
   * **ArduinoJson**  
   *(WiFi.h and HTTPClient.h come with the ESP32 core)*

---

## 2 · Finnhub API key

1. Sign up for a free account at **[finnhub.io](https://finnhub.io)**  
2. Copy your *API token* from → Dashboard → API Key  
3. In `ticker.ino`, replace:

```cpp
const char* FINNHUB = "YOUR_TOKEN_HERE";

Free tier allows 60 requests /min — perfect for one call every 60 s.

3 · Configure Wi-Fi & symbols
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASS";

const char* symbols[] = { "QQQ", "SPY", "VIX", "AMZN",
                          "SOFI", "PYPL", "PLTR", "NVDA" };

4 · Compile & upload
	•	Click ✔ Verify → → Upload
	•	If the IDE hangs on “Connecting…”, hold BOOT, tap EN, release BOOT
	•	The OLED should light up within five seconds

Runtime controls
Button
Function
BOOT / GPIO 0
Tap to cycle scroll speed (40 ms → 30 → 20 → 12 → 6 ms per frame)
EN / RST
Reboot the board

Status bar legend (top yellow line):
IP:192.168.0.24 W/D
         │        │
         │        └─ Data OK / API error
         └────────── Wi-Fi OK / E〈code〉
	•	MARKET CLOSED appears on the cyan line below the ticker whenever Finnhub reports the US market is shut.

Troubleshooting
Symptom
Fix
OLED stays black
Check SDA/SCL wiring, use a data-capable USB cable
“Failed to connect” during upload
Hold BOOT, tap EN, or lower Upload Speed to 115 200
Only yellow/cyan noise
Ensure the panel is SSD1306; some clones are SH1106 ← need different library
No Wi-Fi, code shows E1
Wrong SSID case or 5 GHz-only network; enable 2.4 GHz channel 1-11


Customisation tips
	•	Change FETCH_MS to pull quotes faster/slower (respect Finnhub limits).
	•	Set TICKER_SIZE to 1 for small 5 × 8 font and two ticker rows.
	•	Add more symbols by enlarging symbols[] and NSYM, but watch the 256-byte JSON buffer.

License

MIT — do whatever you like; attribution appreciated.
Quotes via Finnhub.io are subject to their Terms of Service.
