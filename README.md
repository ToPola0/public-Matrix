# LED Matrix Clock (ESP32-S3)

Projekt zegara LED na matrycę WS2812B 32x8 (256 LED) z panelem WWW, OTA, harmonogramem, cytatami, funkcją **Urodziny** i integracją MQTT/Home Assistant.

## Funkcje

- Zegar (HH:MM:SS) z animacjami cyfr (losowanie efektów i testy ręczne z panelu)
- Tryb **Lampa** (pełne oświetlenie jednolitym kolorem)
- Wiadomości runtime i wiadomości zaplanowane
- Cytaty (losowe i testowe)
- Urodziny (lista imion + dat, losowe życzenia w losowych kolorach)
- Harmonogram jasności i koloru
- MQTT + auto-discovery dla Home Assistant
- OTA update
- Logi diagnostyczne (WWW + API + Serial)

## Wymagany sprzęt

- ESP32-S3 (testowane na ESP32-S3 N16R8)
- Matryca LED WS2812B 32x8
- Stabilny zasilacz 5V (zalecane min. 8A, najlepiej 10A)
- Przewody
- (Opcjonalnie) buzzer na GPIO18

## Podłączenie (domyślne)

- Data matrycy: GPIO2 (`LED_PIN = 2`)
- GND matrycy: wspólna masa z ESP32
- 5V matrycy: osobny stabilny zasilacz 5V

Parametry mapowania matrycy w `config.h`:

- `LED_WIDTH = 32`
- `LED_HEIGHT = 8`
- `MATRIX_SERPENTINE = 1`
- `MATRIX_COLUMN_MAJOR = 1`

Jeśli tekst jest odwrócony lub „rozsypany”, skoryguj:
`MATRIX_FLIP_X`, `MATRIX_FLIP_Y`, `MATRIX_SERPENTINE`, `MATRIX_COLUMN_MAJOR`.

## Środowisko i biblioteki

Projekt działa w Arduino IDE oraz VS Code (Arduino/PlatformIO).

Wymagane biblioteki:

- FastLED
- ArduinoJson
- PubSubClient
- NTPClient

Biblioteki `WiFi`, `WebServer`, `Preferences`, `ArduinoOTA` pochodzą z core ESP32.

## Konfiguracja przed wgraniem

W `config.h` można ustawić:

- WiFi: `WIFI_SSID`, `WIFI_PASSWORD`
- MQTT: `MQTT_BROKER`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASSWORD`
- OTA: `OTA_HOSTNAME`, `OTA_PASSWORD`

Większość ustawień można później zmienić z panelu WWW.

## Wgranie firmware

1. Wybierz płytkę ESP32-S3 i port COM.
2. Ustaw opcje płytki (Arduino IDE):
   - PSRAM: OPI PSRAM
   - Flash Size: 16MB (128Mb)
   - Partition Scheme: Custom
3. Skompiluj i wgraj.
4. Jeśli urządzenie nie ma zapisanej sieci WiFi, uruchamia tryb AP z panelem konfiguracji.

## Panel WWW

Po połączeniu z siecią otwórz adres IP urządzenia (widać w logach Serial).

Logowanie (HTTP Basic):

- użytkownik: `admin`
- hasło: aktualne hasło AP

Zakładki panelu (STA):

- Info
- Czas
- Anim
- Lampa
- Cytaty
- Urodziny
- Plan (harmonogram jasności)
- MQTT
- WiFi
- Logi

### Animacje cyfr

Dostępne efekty zegara (Fun Clock):

- Lustro
- Tęcza
- Godziny: wyjazd/wjazd
- Predator GLYPH
- Matrix bokiem (2 rzędy)
- Do góry nogami
- Obrót 180°
- Pełny obrót
- Przejazd wszystkich cyfr
- Tetris
- Karambol cyfr
- Negatyw
- Tęczowe tło z cyframi

W zakładce **Anim** możesz:

- włączać/wyłączać każdy efekt checkboxem
- uruchamiać testy ręczne (osobny przycisk testu dla efektu)
- uruchomić „Test animacji cyfr” (losuje jeden z aktywnych efektów)

Rotacja animacji używa tylko efektów aktywnych w ustawieniach.

### Urodziny

W zakładce **Urodziny**:

- dodajesz wpisy: imię + data (`YYYY-MM-DD`)
- usuwasz wpisy z listy
- uruchamiasz **Test życzeń** (jak inne testy)

Automatyczne życzenia:

- przy dopasowaniu daty (miesiąc + dzień) są wyświetlane co losowy czas
- używają losowych szablonów i losowych kolorów
- rok w dacie nie blokuje cykliczności — wpis działa co roku

### Tryb Lampa

W trybie `DISPLAY_MODE_LAMP` logika zegara i scheduler są zatrzymane, żeby uniknąć lagów podczas świecenia lampy.
To oznacza, że podczas lampy nie uruchamiają się animacje zegara ani automatyczne zdarzenia scheduler.

## MQTT / Home Assistant

Urządzenie publikuje auto-discovery (domyślnie prefix `homeassistant`).

Przykładowe encje:

- `light` (zegar)
- `light` (lampa)
- `select` trybu (`clock/lamp/animation`)
- `switch` negatywu
- encje statusowe (m.in. WiFi/MQTT/RSSI)

## Logi i diagnostyka

Logi są trzymane w buforze pierścieniowym i dostępne przez:

- Serial Monitor
- zakładkę Logi w panelu WWW
- endpoint API: `/api/logs`

Z poziomu WWW można logi odświeżać, pobrać i wyczyścić.

## OTA

OTA aktywuje się po połączeniu WiFi.

Domyślnie:

- host: `LedMatrixClock`
- port: `3232`
- hasło: `12345678`

## API (wybrane endpointy)

- `POST /trigger-quote`
- `POST /trigger-clock-anim`
- `POST /trigger-clock-mirror`
- `POST /trigger-birthday-test`
- `GET /api/birthdays`
- `POST /save-birthday`
- `POST /delete-birthday?index=N`
- `GET /api/logs`

## Najczęstsze problemy

- Migotanie/artefakty: sprawdź wydajność zasilacza i wspólną masę.
- Złe mapowanie tekstu: popraw ustawienia mapowania w `config.h`.
- Brak MQTT: sprawdź host/port/dane logowania i sieć brokera.
- Brak synchronizacji czasu: sprawdź NTP w zakładce Czas i logi diagnostyczne.

## Rozwój projektu

- Aktualna historia zmian: `CHANGELOG.md`
- Główna konfiguracja projektu: `config.h`
