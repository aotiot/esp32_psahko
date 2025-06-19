#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <time.h>
#include <vector>

// WiFi-tiedot
const char* ssid = "ssid";
const char* password = "password";

// LED-asetukset
#define LED_PIN 2
#define LED_WIDTH 5
#define LED_HEIGHT 5
#define NUM_LEDS (LED_WIDTH * LED_HEIGHT)
#define BRIGHTNESS 51
CRGB leds[NUM_LEDS];

// ⏰ Korvaava timegm()
time_t my_timegm(struct tm* tm) {
  char* tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzset();
  time_t t = mktime(tm);
  if (tz) setenv("TZ", tz, 1);
  else unsetenv("TZ");
  tzset();
  return t;
}

// Kesäajan tarkistus
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

int getLevelForPrice(float price) {
  if (price <= 2.05) return 0;
  else if (price <= 4.10) return 1;
  else if (price <= 6.15) return 2;
  else if (price <= 10.20) return 3;
  else return 4;
}

CRGB getColorForLevel(int level) {
  switch (level) {
    case 0: return CRGB::Green;
    case 1: return CRGB(100, 255, 0);
    case 2: return CRGB::Yellow;
    case 3: return CRGB::Orange;
    case 4: return CRGB::Red;
    default: return CRGB::Black;
  }
}

int xyToIndex(int x, int y) {
  return (y % 2 == 0) ? y * LED_WIDTH + x : y * LED_WIDTH + (LED_WIDTH - 1 - x);
}

time_t parseISOTime(const char* iso) {
  struct tm t = {};
  strptime(iso, "%Y-%m-%dT%H:%M:%S", &t);
  return my_timegm(&t);
}

void syncTime() {
  configTime(0, 0, "pool.ntp.org");
  Serial.print("🕒 Synkronoidaan aika");
  struct tm t;
  while (!getLocalTime(&t)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  time_t now = time(nullptr);
  time_t localNow = getLocalTimeWithOffset(now);
  struct tm lt;
  localtime_r(&localNow, &lt);

  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
  Serial.print("✅ Paikallinen aika: ");
  Serial.println(buf);
  Serial.print("🌍 UTC-aika: ");
  Serial.println(ctime(&now));
}

String fetchPriceJson() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  Serial.println("🌐 Alustetaan HTTPS-yhteys...");
  if (!https.begin(client, "https://api.porssisahko.net/v1/latest-prices.json")) {
    Serial.println("❌ HTTPS-yhteyden alustus epäonnistui.");
    return "";
  }

  https.setConnectTimeout(5000);
  https.useHTTP10(true);
  Serial.println("📡 Lähetetään GET-pyyntö...");
  int httpCode = https.GET();
  Serial.printf("📶 HTTP-vastauskoodi: %d\n", httpCode);

  String payload = "";
  if (httpCode == HTTP_CODE_OK) {
    payload = https.getString();
  } else {
    Serial.printf("❌ HTTP-pyyntö epäonnistui: %d\n", httpCode);
  }

  https.end();
  return payload;
}

void showLivePrices() {
  String payload = fetchPriceJson();
  if (payload.length() == 0) {
    Serial.println("⚠️ Ei saatu dataa.");
    return;
  }

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, payload)) {
    Serial.println("⚠️ JSON-virhe");
    return;
  }

  time_t nowUTC = time(nullptr);
  time_t localNow = getLocalTimeWithOffset(nowUTC);

  JsonArray prices = doc["prices"];
  std::vector<JsonObject> relevant;

  for (JsonObject p : prices) {
    time_t st = parseISOTime(p["startDate"]);
    time_t et = parseISOTime(p["endDate"]);
    if (et > nowUTC) { // Tunti on meneillään tai tuleva
      relevant.push_back(p);
    }
  }

  std::sort(relevant.begin(), relevant.end(), [](const JsonObject& a, const JsonObject& b) {
    return parseISOTime(a["startDate"]) < parseISOTime(b["startDate"]);
  });

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  Serial.println("📊 Meneillään oleva ja seuraavat 4 tuntia:");

  for (int x = 0; x < 5 && x < relevant.size(); x++) {
    JsonObject p = relevant[x];
    float price = p["price"];
    time_t ts = parseISOTime(p["startDate"]);
    time_t localTs = getLocalTimeWithOffset(ts);

    struct tm lt;
    localtime_r(&localTs, &lt);
    char tstr[25];
    strftime(tstr, sizeof(tstr), "%Y-%m-%d %H:%M", &lt);
    int level = getLevelForPrice(price);

    Serial.printf("⏰ %s | %.3f €/kWh | Taso %d\n", tstr, price, level);

    if (price < 0) {
      // Vihreä ylä- ja alarivi
      leds[xyToIndex(x, 0)] = CRGB::Green;
      leds[xyToIndex(x, LED_HEIGHT - 1)] = CRGB::Green;
      for (int y = 1; y < LED_HEIGHT - 1; y++) {
        leds[xyToIndex(x, y)] = CRGB::Black;
      }
    } else {
      // Värjätään tasokorkeuden mukaan
      for (int y = 0; y < LED_HEIGHT; y++) {
        int idx = xyToIndex(x, y);
        leds[idx] = (y <= level) ? getColorForLevel(level) : CRGB::Black;
      }
    }
  }

  FastLED.show();
  Serial.println("💡 LED-näyttö päivitetty.\n");
}


void setup() {
  Serial.begin(115200);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  Serial.print("🔌 Yhdistetään WiFiin...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println(" ✅");

  syncTime();
  showLivePrices();
}

void loop() {
  delay(30 * 60 * 1000);  // päivitä kerran tunnissa
  showLivePrices();
}
