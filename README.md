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
2. Skompiluj i wgraj projekt.
3. Po starcie urządzenie uruchamia panel konfiguracyjny WiFi/AP, jeśli nie ma połączenia.

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

- czas i NTP
- kolory/jasność/efekty
- lampę
- MQTT
- harmonogram i cytaty

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

## 8. OTA

OTA jest aktywne po podłączeniu WiFi.

Domyślnie:

- host: LedMatrixClock
- port: 3232
- hasło: 12345678

## 9. Szybkie problemy

- Migotanie/artefakty: sprawdź zasilanie 5V i wspólną masę.
- Zły kierunek tekstu: popraw mapowanie matrycy w config.h.
- Brak MQTT: sprawdź host/port/login i czy broker jest dostępny w tej samej sieci.
