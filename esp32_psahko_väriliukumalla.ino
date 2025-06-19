#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <time.h>
#include <vector>

// 🛜 WiFi-tiedot
const char* ssid = "ssid";
const char* password = "password";

// 💡 LED-matriisin asetukset
#define LED_PIN     2
#define LED_WIDTH   5
#define LED_HEIGHT  5
#define NUM_LEDS    (LED_WIDTH * LED_HEIGHT)
#define BRIGHTNESS  51
CRGB leds[NUM_LEDS];

/**
 * @brief Vilkuttaa LED-matriisia yhdellä värillä virhetilanteessa.
 * 
 * @param color Väri, jolla vilkutetaan (esim. CRGB::Blue).
 * @param message Sarjaporttiin tulostettava selitysteksti.
 */
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

/**
 * @brief Vilkuttaa LED-matriisia satunnaisilla väreillä määrätyn ajan.
 *        Käytetään esimerkiksi, kun hintadataa ei saada näkyviin.
 * 
 * @param durationMs Aika millisekunteina, jonka ajan vilkutetaan.
 */
void blinkRandomLeds(unsigned long durationMs) {
  Serial.println("🎲 Vilkutetaan LED-matriisia satunnaisilla väreillä...");

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

/**
 * @brief Tarkistaa, onko annetussa ajassa voimassa kesäaika.
 * 
 * @param t Aikaleima rakenteena (struct tm).
 * @return true, jos kesäaika on voimassa. Muuten false.
 */
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

/**
 * @brief Laskee paikallisen ajan UTC-ajasta huomioiden manuaalisen kesäajan.
 * 
 * @param utcNow UTC-aikaleima.
 * @return Paikallinen aika time_t-muodossa.
 */
time_t getLocalTimeWithOffset(time_t utcNow) {
  struct tm tmUTC;
  gmtime_r(&utcNow, &tmUTC);
  int offset = isSummerTime(&tmUTC) ? 3 * 3600 : 2 * 3600;
  return utcNow + offset;
}

/**
 * @brief Ajanmuunnos funktio ilman aikavyöhykettä (kuten timegm).
 * 
 * @param tm struct tm, joka sisältää UTC-aikatiedot.
 * @return time_t UTC-ajassa.
 */
time_t my_timegm(struct tm* tm) {
  char *tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzset();
  time_t t = mktime(tm);
  if (tz) setenv("TZ", tz, 1); else unsetenv("TZ");
  tzset();
  return t;
}

/**
 * @brief Parsii ISO 8601 -muotoisen ajan (esim. "2025-06-19T12:00:00").
 * 
 * @param iso Merkkijonoaika.
 * @return time_t UTC-ajassa.
 */
time_t parseISOTime(const char* iso) {
  struct tm t = {};
  strptime(iso, "%Y-%m-%dT%H:%M:%S", &t);
  return my_timegm(&t);
}

/**
 * @brief Palauttaa hintatasoa vastaavan luokitustason (0–4).
 * 
 * @param price Sähkön hinta €/kWh.
 * @return Kokonaisluku tasolta 0 (halpa) tasolle 4 (kallis).
 */
int getLevelForPrice(float price) {
  if (price <= 2.05) return 0;
  else if (price <= 4.10) return 1;
  else if (price <= 6.15) return 2;
  else if (price <= 10.20) return 3;
  else return 4;
}

/**
 * @brief Muuntaa 2D-koordinaatin LED-matriisin lineaariseksi indeksiksi.
 * 
 * @param x X-koordinaatti (leveys).
 * @param y Y-koordinaatti (korkeus).
 * @return LED-indeksi (0–NUM_LEDS-1).
 */
int xyToIndex(int x, int y) {
  return (y % 2 == 0) ? y * LED_WIDTH + x : y * LED_WIDTH + (LED_WIDTH - 1 - x);
}

/**
 * @brief Synkronoi ajan NTP-palvelimelta ja tulostaa paikallisen ajan.
 *        Jos NTP ei onnistu, vilkuttaa violeteilla ledeillä ja yrittää uudelleen.
 */
void syncTime() {
  Serial.println("🕒 Synkronoidaan aika NTP:llä...");
  configTime(0, 0, "pool.ntp.org");

  struct tm t;
  int attempts = 0;
  while (!getLocalTime(&t) && attempts < 10) {
    Serial.print(".");
    delay(500);
    attempts++;
  }

  if (!getLocalTime(&t)) {
    blinkError(CRGB(128, 0, 255), "\n❌ NTP-aika ei saatu. Yritetään uudelleen 15 s kuluttua.");
    delay(15000);
    syncTime();  // uusi yritys
    return;
  }

  Serial.println("\n✅ Aika haettu onnistuneesti");
  time_t now = time(nullptr);
  time_t localNow = getLocalTimeWithOffset(now);
  struct tm lt;
  localtime_r(&localNow, &lt);
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
  Serial.print("🕒 Paikallinen aika: ");
  Serial.println(buf);
  Serial.print("🌍 UTC-aika: ");
  Serial.println(ctime(&now));
}

/**
 * @brief Hakee sähkön hintatiedot HTTPS:n kautta API:sta.
 * 
 * @return JSON-data merkkijonona. Tyhjä merkkijono, jos haku epäonnistui.
 */
String fetchPriceJson() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  const char* url = "https://api.porssisahko.net/v1/latest-prices.json";
  Serial.println("🌐 Alustetaan HTTPS-yhteys...");
  if (!https.begin(client, url)) {
    Serial.println("❌ HTTPS-yhteyden alustus epäonnistui.");
    return "";
  }

  https.setConnectTimeout(5000);
  https.useHTTP10(true);
  Serial.println("📡 Lähetetään GET-pyyntö...");
  int httpCode = https.GET();
  Serial.printf("📶 HTTP-vastauskoodi: %d\n", httpCode);

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("❌ HTTP-pyyntö epäonnistui. Koodi: %d\n", httpCode);
    https.end();
    return "";
  }

  String payload = https.getString();
  Serial.printf("✅ JSON vastaanotettu (%d merkkiä)\n", payload.length());

  https.end();
  return payload;
}

/**
 * @brief Hakee hintatiedot API:sta ja näyttää meneillään olevan sekä seuraavat 4 tuntia LED-matriisilla.
 *        Jos JSON ei ole saatavilla minuutissa, ESP32 käynnistetään uudelleen.
 *        Epäonnistumisten aikana LED-matriisi vilkkuu satunnaisilla väreillä.
 */
void showLivePrices() {
  Serial.println("📥 Haetaan hintatietoja API:sta...");
  unsigned long startAttempt = millis();

  while (true) {
    String payload = fetchPriceJson();

    if (payload.length() == 0) {
      Serial.println("⚠️ Ei saatu hintadataa. Uusi yritys hetken kuluttua...");
      blinkRandomLeds(3000);
      if (millis() - startAttempt > 60000) {
        Serial.println("⏱️ 60 sekuntia ilman vastausta – suoritetaan uudelleenkäynnistys.");
        ESP.restart();
      }
      continue;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.print("❌ JSON-virhe: ");
      Serial.println(error.f_str());
      blinkRandomLeds(3000);
      if (millis() - startAttempt > 60000) {
        Serial.println("⏱️ 60 sekuntia epäkelvolla datalla – suoritetaan uudelleenkäynnistys.");
        ESP.restart();
      }
      continue;
    }

    if (!doc.containsKey("prices")) {
      Serial.println("❌ JSON ei sisällä 'prices'-avainta.");
      blinkRandomLeds(3000);
      if (millis() - startAttempt > 60000) {
        Serial.println("⏱️ 60 sekuntia ilman hintatietoja – suoritetaan uudelleenkäynnistys.");
        ESP.restart();
      }
      continue;
    }

    // ✅ Data OK – jatketaan LED-näyttöön
    JsonArray prices = doc["prices"];
    std::vector<JsonObject> future;

    time_t nowUTC = time(nullptr);
    for (JsonObject p : prices) {
      time_t st = parseISOTime(p["startDate"]);
      time_t et = parseISOTime(p["endDate"]);
      if (et > nowUTC) {
        future.push_back(p);
      }
    }

    std::sort(future.begin(), future.end(), [](const JsonObject& a, const JsonObject& b) {
      return parseISOTime(a["startDate"]) < parseISOTime(b["startDate"]);
    });

    fill_solid(leds, NUM_LEDS, CRGB::Black);
    Serial.println("📊 Näytetään tuntihinnat LED-matriisilla:");

    for (int x = 0; x < 5 && x < future.size(); x++) {
      JsonObject p = future[x];
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
        leds[xyToIndex(x, 0)] = CRGB::Green;
        leds[xyToIndex(x, LED_HEIGHT - 1)] = CRGB::Green;
        for (int y = 1; y < LED_HEIGHT - 1; y++) {
          leds[xyToIndex(x, y)] = CRGB::Black;
        }
      } else {
        for (int y = 0; y < LED_HEIGHT; y++) {
          int idx = xyToIndex(x, y);
          if (y <= level) {
            float ratio = (float)y / (LED_HEIGHT - 1);
            CRGB color = blend(CRGB::Green, CRGB::Red, (uint8_t)(ratio * 255));
            leds[idx] = color;
          } else {
            leds[idx] = CRGB::Black;
          }
        }
      }
    }

    FastLED.show();
    Serial.println("💡 LED-näyttö päivitetty.");
    break;
  }
}

/**
 * @brief Käynnistää ESP32:n, yhdistää WiFiin, synkronoi ajan ja hakee hintatiedot.
 */
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🚀 Käynnistetään ESP32-pörssisähkö");

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  // WiFi-yhteyden muodostus
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("🔌 Yhdistetään WiFiin: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);

    int countdown = 0;
    while (WiFi.status() != WL_CONNECTED && countdown < 20) {
      Serial.print(".");
      delay(500);
      countdown++;
    }

    if (WiFi.status() != WL_CONNECTED) {
      blinkError(CRGB::Blue, "\n❌ WiFi-yhteys epäonnistui. Uusi yritys 15 s kuluttua.");
      delay(15000);
    }
  }

  Serial.println("\n✅ WiFi-yhteys muodostettu!");

  syncTime();
  showLivePrices();
}

/**
 * @brief Päivittää hintanäytön säännöllisesti 30 minuutin välein.
 */
void loop() {
  delay(30 * 60 * 1000);
  showLivePrices();
}

