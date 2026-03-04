# LED Matrix Clock (ESP32-S3)

Projekt zegara LED na matrycę 32x8 (WS2812B) z panelem WWW, OTA i integracją MQTT/Home Assistant.

## 1. Wymagany sprzęt

- ESP32-S3 (w projekcie testowane jako ESP32-S3 N16R8)
- Matryca LED WS2812B 32x8 (256 LED)
- Stabilny zasilacz 5V (dla 256 LED zalecane minimum 8A, lepiej 10A)
- Przewody połączeniowe
- Opcjonalnie buzzer na GPIO18

## 2. Podłączenie

Domyślna konfiguracja z projektu:

- Data matrycy LED -> GPIO2 (LED_PIN = 2)
- GND matrycy -> GND ESP32
- 5V matrycy -> zasilacz 5V
- ESP32 zasilaj stabilnie (USB lub osobno), ale masa (GND) musi być wspólna z matrycą

Parametry matrycy są w pliku config.h:

- LED_WIDTH = 32
- LED_HEIGHT = 8
- MATRIX_SERPENTINE = 1
- MATRIX_COLUMN_MAJOR = 1

Jeśli tekst jest odwrócony lub źle mapowany, zmień MATRIX_FLIP_X / MATRIX_FLIP_Y / MATRIX_SERPENTINE / MATRIX_COLUMN_MAJOR.

## 3. Środowisko i biblioteki

Arduino IDE lub VS Code + PlatformIO/Arduino.

Wymagane biblioteki:

- FastLED
- ArduinoJson
- PubSubClient
- NTPClient

Biblioteki WiFi/WebServer/Preferences/ArduinoOTA są częścią pakietu ESP32.

## 4. Konfiguracja przed wgraniem

W pliku config.h ustaw (opcjonalnie, bo i tak możesz skonfigurować z panelu WWW):

- WIFI_SSID, WIFI_PASSWORD
- MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD
- OTA_HOSTNAME, OTA_PASSWORD

## 5. Wgranie firmware

1. Wybierz płytkę ESP32-S3 i poprawny port COM.
2. Ustaw opcje płytki (Arduino IDE):
	- PSRAM: OPI PSRAM
	- Flash Size: 16MB (128Mb)
	- Partition Scheme: Custom
3. Skompiluj i wgraj projekt.
4. Po starcie urządzenie uruchamia panel konfiguracyjny WiFi/AP, jeśli nie ma połączenia.

## 6. Panel WWW

Po połączeniu z siecią wejdź na adres IP urządzenia (sprawdzisz w logu portu szeregowego).

Panel WWW jest zabezpieczony logowaniem HTTP Basic:

- użytkownik: `admin`
- hasło: takie samo jak aktualne hasło AP

W zakładce WiFi możesz zmienić:

- hasło sieci WiFi (STA)
- hasło AP / logowania WWW (8-63 znaki)

Aktualne hasło AP/WWW jest wypisywane w `Serial Monitor` po starcie i po zmianie.

W panelu skonfigurujesz:

- **Czas i NTP** - synchronizacja czasu z serwerem (np. domyślnie co 30 sekund)
- **Kolory/jasność/efekty** - personalizacja wyświetlacza
- **Lampę** - konfiguracja dodatkowych oświetleniach
- **MQTT** - integracja z Home Assistant
- **Harmonogram i cytaty** - zaplanowane zmiany i odpowiedzi
- **Diagnostykę** - podgląd logów systemowych i podejmowania działań

W zakładce **Diagnostyka** możesz:

- Włączyć/wyłączyć rejestrowanie logów
- Wyświetlić logi w real-time z aktualizacją co 2 sekundy
- Pobrać logi jako plik .txt
- Wyczyścić buffor logów

## 7. MQTT / Home Assistant

Urządzenie publikuje auto-discovery do Home Assistant (domyślnie prefix homeassistant).

Dostępne są m.in. encje:

- light: główne światło zegara
- light: lampa
- select: tryb (clock/lamp/animation)
- switch: negatyw (losowanie)
- switch: pokaż hasło AP
- text: hasło AP (pole edytowalne)
- sensory status/rssi

Zasada działania haseł w HA:

- `Pokaż hasło AP` domyślnie jest `OFF`.
- Gdy `OFF`, encja tekstowa hasła pokazuje `ukryte`.
- Gdy `ON`, encja tekstowa pokazuje aktualne hasło AP.
- Hasło AP można też zmienić z encji `text` (zmiana zapisywana, aktywna po restarcie urządzenia).

## 7a. System logowania

Urządzenie zapisuje logi zdarzeń w ring bufforze (200 linii, ~4KB pamięci):

- Synchronizacja NTP
- Zmiany trybu wyświetlania
- Wykonywane animacje
- Błędy połączeń

Logi dostępne są:

- W **Serial Monitor** w czasie rzeczywistym
- W panelu WWW zakładka **Diagnostyka** (podgląd live co 2 sekundy)
- Przez **API endpoint** `/api/logs` (JSON format)

## 8. OTA

OTA jest aktywne po podłączeniu WiFi.

Domyślnie:

- host: LedMatrixClock
- port: 3232
- hasło: 12345678

## 8a. Optymalizacja performansu i NTP

Urządzenie zoptymalizowane jest dla płynnego renderowania (docelowo ~30 FPS):

- **NTP**: Synchronizacja czasu odbywa się domyślnie **co 30 sekund** (nie co 1 sekundę), aby uniknąć blokowania pętli renderowania na timeout'ach sieciowych
- **Pierwsza synchronizacja**: Przy starcie urządzenie natychmiast próbuje zsynchronizować czas
- **Fallback serwera NTP**: Jeśli serwer podstawowy nie odpowiada przez 3 próby, urządzenie automatycznie przechodzi do `pool.ntp.org`

Jeśli czas nie synchronizuje się prawidłowo:

1. Sprawdź w **Diagnostyce** logi NTP
2. W panelu **Czas/NTP** spróbuj zmienić serwer na `pool.ntp.org` lub `time.nist.gov`
3. Upewnij się, że urządzenie ma dostęp do internetu

## 9. Szybkie problemy

- Migotanie/artefakty: sprawdź zasilanie 5V i wspólną masę.
- Zły kierunek tekstu: popraw mapowanie matrycy w config.h.
- Brak MQTT: sprawdź host/port/login i czy broker jest dostępny w tej samej sieci.
- Czas się nie synchronizuje: sprawdź logi w zakładce **Diagnostyka**, spróbuj zmienić serwer NTP.

## 10. Ostatnie aktualizacje

### Cytaty i teksty
- **2026-03-03**: Naprawiono efekt clipping'u cyfr w cytatach (problem z desynchronizacją timingu scroll'u)
  - Scroll zawsze przesuwał o -1 pixel, nawet jeśli timer przeskoczył okresy
  - Nowa logika: obliczanie liczby step'ów i stosowanie wszystkich naraz
  - Przywrócono pełne wsparcie dla polskich znaków UTF-8 w cytatach (ą, ć, ę, ł, ń, ó, ś, ź, ż)

### Interfejs użytkownika
- **2026-03-03**: Zmiana labelu z "Prędkość animacji:" na "Prędkość wyświetlania:"
  - Parametr `animation_speed` steruje zarówno prędkością animacji tła jak i szybkością scrollowania tekstu/cytatów

### Animacje
- Usunięto animacje: "Połówki cyfr: prawa w dół, lewa w górę" (MOVE i SPLIT_HALVES)
- Dostępne animacje: Lustro, Tęcza, Godziny, Predator GLYPH, Spirala Down, Do góry nogami, Obrót 180°, Obrót 360°, Zamiana środka, Tetris, Stos, Negatyw

### Nowe funkcje (2026-03-04)
- Dodano efekt **Tęczowe tło z cyframi**:
  - Zegar (cyfry i dwukropki) pozostaje widoczny na pierwszym planie
  - Profil czasowy: 2s fade-in + 4s pełna jasność + 2s fade-out
  - Efekt testowy uruchamiany z WWW kończy się automatycznie (nie działa bez końca)
- Efekt **Tęczowe tło z cyframi** został podpięty do losowania jak pozostałe efekty:
  - checkbox `fxRainbowBackground` w panelu WWW realnie steruje udziałem w randomizacji
  - stan checkboxa jest zapisywany i odczytywany z ustawień urządzenia
- W panelu WWW (AP) dodano szybkie wysyłanie wiadomości runtime z formularza.
