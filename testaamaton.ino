#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <time.h>
#include <vector>
#include <WebServer.h>
#include <Preferences.h>

// 💡 LED-matriisin asetukset
#define LED_PIN     2
#define LED_WIDTH   5
#define LED_HEIGHT  5
#define NUM_LEDS    (LED_WIDTH * LED_HEIGHT)
#define BRIGHTNESS  51
CRGB leds[NUM_LEDS];

Preferences preferences;
WebServer server(80);

/** LED-hälytysvilkutus */
void blinkError(CRGB color, const char* message) {
  Serial.println(message);
  for (int i = 0; i < 15; i++) {
    fill_solid(leds, NUM_LEDS, color);
    FastLED.show();
    delay(250);
    FastLED.clear();
    FastLED.show();
    delay(250);
  }
}

/** Satunnainen vilkutus */
void blinkRandomLeds(unsigned long durationMs) {
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB(random(0, 256), random(0, 256), random(0, 256));
    }
    FastLED.show();
    delay(200);
    FastLED.clear();
    FastLED.show();
    delay(200);
  }
}

bool isSummerTime(struct tm* t) {
  int month = t->tm_mon + 1;
  int day = t->tm_mday;
  int wday = t->tm_wday;
  int hour = t->tm_hour;
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;
  int lastSunday = 31 - ((wday + (31 - day)) % 7);
  if (month == 3) return day > lastSunday || (day == lastSunday && hour >= 3);
  if (month == 10) return day < lastSunday || (day == lastSunday && hour < 4);
  return false;
}

time_t getLocalTimeWithOffset(time_t utcNow) {
  struct tm tmUTC;
  gmtime_r(&utcNow, &tmUTC);
  int offset = isSummerTime(&tmUTC) ? 3 * 3600 : 2 * 3600;
  return utcNow + offset;
}

time_t my_timegm(struct tm* tm) {
  char *tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzset();
  time_t t = mktime(tm);
  if (tz) setenv("TZ", tz, 1); else unsetenv("TZ");
  tzset();
  return t;
}

time_t parseISOTime(const char* iso) {
  struct tm t = {};
  strptime(iso, "%Y-%m-%dT%H:%M:%S", &t);
  return my_timegm(&t);
}

int getLevelForPrice(float price) {
  if (price <= 2.05) return 0;
  else if (price <= 4.10) return 1;
  else if (price <= 6.15) return 2;
  else if (price <= 10.20) return 3;
  else return 4;
}

int xyToIndex(int x, int y) {
  return (y % 2 == 0) ? y * LED_WIDTH + x : y * LED_WIDTH + (LED_WIDTH - 1 - x);
}

void syncTime() {
  configTime(0, 0, "pool.ntp.org");
  struct tm t;
  int attempts = 0;
  while (!getLocalTime(&t) && attempts < 10) {
    delay(500);
    attempts++;
  }
  if (!getLocalTime(&t)) {
    blinkError(CRGB(128, 0, 255), "❌ NTP-aika ei saatu");
    delay(15000);
    syncTime();
    return;
  }
  time_t now = time(nullptr);
  time_t localNow = getLocalTimeWithOffset(now);
  struct tm lt;
  localtime_r(&localNow, &lt);
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
  Serial.print("🕒 Paikallinen aika: ");
  Serial.println(buf);
}

String fetchPriceJson() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  const char* url = "https://api.porssisahko.net/v1/latest-prices.json";
  if (!https.begin(client, url)) return "";
  https.setConnectTimeout(5000);
  https.useHTTP10(true);
  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    https.end();
    return "";
  }
  String payload = https.getString();
  https.end();
  return payload;
}

void showLivePrices() {
  unsigned long startAttempt = millis();
  while (true) {
    String payload = fetchPriceJson();
    if (payload.length() == 0) {
      blinkRandomLeds(3000);
      if (millis() - startAttempt > 60000) ESP.restart();
      continue;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, payload);
    if (error || !doc.containsKey("prices")) {
      blinkRandomLeds(3000);
      if (millis() - startAttempt > 60000) ESP.restart();
      continue;
    }

    JsonArray prices = doc["prices"];
    std::vector<JsonObject> future;
    time_t nowUTC = time(nullptr);
    for (JsonObject p : prices) {
      time_t st = parseISOTime(p["startDate"]);
      time_t et = parseISOTime(p["endDate"]);
      if (et > nowUTC) future.push_back(p);
    }

    std::sort(future.begin(), future.end(), [](const JsonObject& a, const JsonObject& b) {
      return parseISOTime(a["startDate"]) < parseISOTime(b["startDate"]);
    });

    fill_solid(leds, NUM_LEDS, CRGB::Black);
    for (int x = 0; x < 5 && x < future.size(); x++) {
      JsonObject p = future[x];
      float price = p["price"];
      int level = getLevelForPrice(price);
      if (price < 0) {
        leds[xyToIndex(x, 0)] = CRGB::Green;
        leds[xyToIndex(x, LED_HEIGHT - 1)] = CRGB::Green;
      } else {
        for (int y = 0; y < LED_HEIGHT; y++) {
          int idx = xyToIndex(x, y);
          if (y <= level) {
            float ratio = (float)y / (LED_HEIGHT - 1);
            leds[idx] = blend(CRGB::Green, CRGB::Red, (uint8_t)(ratio * 255));
          } else {
            leds[idx] = CRGB::Black;
          }
        }
      }
    }
    FastLED.show();
    break;
  }
}

/** Käynnistää AP-tilan ja lomakepalvelimen */
void startConfigPortal() {
  WiFi.softAP("Konffis");
  IPAddress IP = WiFi.softAPIP();
  Serial.print("🛠️ AP IP: ");
  Serial.println(IP);

  server.on("/", HTTP_GET, []() {
    String html = R"rawliteral(
      <html><head><title>WiFi-asetukset</title></head><body>
      <h2>Anna WiFi-tiedot</h2>
      <form action="/save" method="POST">
        SSID: <input type="text" name="ssid"><br>
        Salasana: <input type="password" name="pass"><br>
        <input type="submit" value="Tallenna">
      </form></body></html>
    )rawliteral";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    String newSSID = server.arg("ssid");
    String newPASS = server.arg("pass");
    if (newSSID.length() > 0 && newPASS.length() > 0) {
      preferences.putString("ssid", newSSID);
      preferences.putString("pass", newPASS);
      server.send(200, "text/html", "<html><body><h2>Tallennettu. Käynnistetään uudelleen...</h2></body></html>");
      delay(2000);
      ESP.restart();
    } else {
      server.send(200, "text/html", "<html><body><h2>⚠️ Molemmat kentät vaaditaan.</h2></body></html>");
    }
  });

  server.begin();

  unsigned long lastToggle = 0;
  bool ledOn = false;

  while (true) {
    server.handleClient();
    if (millis() - lastToggle > 500) {
      fill_solid(leds, NUM_LEDS, ledOn ? CRGB::Yellow : CRGB::Black);
      FastLED.show();
      ledOn = !ledOn;
      lastToggle = millis();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  preferences.begin("wifi-creds", false);
  String storedSSID = preferences.getString("ssid", "");
  String storedPASS = preferences.getString("pass", "");

  if (storedSSID == "") {
    startConfigPortal();
    return;
  }

  WiFi.begin(storedSSID.c_str(), storedPASS.c_str());
  Serial.printf("🔌 Yhdistetään WiFiin: %s\n", storedSSID.c_str());

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    retries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    blinkError(CRGB::Blue, "❌ WiFi-yhteys epäonnistui");
    startConfigPortal();
    return;
  }

  syncTime();
  showLivePrices();
}

void loop() {
  server.handleClient();
  delay(30 * 60 * 1000);
  showLivePrices();
}
