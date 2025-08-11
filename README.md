# esp32_psahko

5x5 matriisi näyttää pörssisähkön, datalähde https://api.porssisahko.net/v1/latest-prices.json


esp32 c3 . 3,3voltin 5x5 rgb matriisi, datapinni 2

BOM

https://www.aliexpress.com/w/wholesale-esp32.html?spm=a2g0o.productlist.auto_suggest.1.6907RzMERzMEO0

(mikä tahansa esp-piiri)

https://www.aliexpress.com/item/32965670423.html <- näyttö

---
Ohjelma on ESP32-mikrokontrollerille kirjoitettu Arduino-sovellus, joka näyttää pörssisähkön tuntihinnat 5×5 LED-matriisilla väriliu’un avulla.

Päätoiminnot

WiFi-yhteys: Yhdistää kovakoodattuun langattomaan verkkoon.

Ajan synkronointi: Hakee UTC-ajan NTP-palvelimelta ja muuntaa sen Suomen aikaan (sisältäen kesäajan laskennan).

Datan haku: Lataa sähkön hinnat api.porssisahko.net -palvelusta JSON-muodossa TLS-yhteydellä.

JSON-parsaus: Etsii kuluvan ja seuraavat tunnit hinnastosta.

Värilogiikka: Määrittää LED-matriisin sarakkeiden värin hinnan mukaan (vihreä = halpa, punainen = kallis, erityismerkki negatiiviselle hinnalle).

Virheenkäsittely: Epäonnistuessa näyttää satunnaisvärit ja yrittää hakea datan uudelleen; jatkuvassa virhetilassa laite käynnistyy uudelleen.

Päivityssykli: Päivittää näytön tiedot puolen tunnin välein.
