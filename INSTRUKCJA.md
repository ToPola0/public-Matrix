# 📱 Instrukcja obsługi - Zegar LED

Witaj! Ta instrukcja wyjaśnia jak korzystać z panelu web twojego zegara LED.

## Spis treści

1. [Aktualizacja oprogramowania](#aktualizacja-oprogramowania)
2. [Panel sterowania](#panel-sterowania)
3. [Konfiguracja WiFi](#konfiguracja-wifi)
4. [Integracja MQTT (Home Assistant)](#integracja-mqtt-home-assistant)
5. [Logi i diagnostyka](#logi-i-diagnostyka)
6. [Rozwiązywanie problemów](#rozwiązywanie-problemów)

---

## Aktualizacja oprogramowania

### Automatyczna aktualizacja

Twoje urządzenie **automatycznie pobiera i instaluje** nowe wersje oprogramowania z GitHuba. Nie musisz nic robić!

**Co się dzieje:**
- Każdego dnia o **03:10** urządzenie sprawdza czy jest nowa wersja
- Jeśli jest dostępna nowa wersja, urządzenie ją pobiera
- Nowa wersja jest automatycznie instalowana i uruchomiana
- Jeśli coś pójdzie nie tak, urządzenie **powraca do poprzedniej wersji** (bezpieczne A/B partycje)

**W panelu zobaczysz:**
- "**Jest nowa wersja, wkrótce będzie na twoim urządzeniu**" - oznacza że czeka instalacji
- "**Wersja zdalna**" - pokazuje jaka wersja jest dostępna na GitHubie

### Ręczna aktualizacja firmware

Jeśli chcesz ręcznie wysłać plik oprogramowania:

1. Przejdź do sekcji **Inne** → zakładka **OTA**
2. Kliknij na pole "Plik firmware (.bin)"
3. Wybierz plik `.bin` ze swojego komputera
4. Kliknij **"Wyślij firmware"**
5. Czekaj aż postęp osiągnie 100% - urządzenie **automatycznie się zrestartuje**

---

## Panel sterowania

Panel web znajduje się pod adresem IP twojego urządzenia (domyślnie dostępny w sieci WiFi).

### Główne zakładki

#### 📟 Zegar
- Ustawienie czasu i strefy czasowej
- Wybór efektów animacji cyfr
- Ustawienie koloru i jasności

#### 🎨 Efekty
- Rozmaite efekty wizualne dla matrycy LED
- Możliwość testowania efektów

#### 💬 Cytaty
- Włączenie/wyłączenie cytatów
- Testowanie cytatów
- Edycja listy cytatów

#### 🎂 Urodziny
- Lista imion i dat urodzin
- Losowe życzenia w dniu imienin/urodzin

#### 📅 Harmonogram
- Ustawienie jasności LED w zależności od pory dnia
- Zmiana kolorów o określonych godzinach

#### 📊 Logi
- Historia działania urządzenia
- Pomocne w diagnostyce problemów

#### ⚙️ Inne
Zawiera podsekcje:

<details>
<summary><strong>MQTT (Home Assistant)</strong></summary>

Połączenie z domową automatyką.

**Jak podłączyć:**
1. Wejdź na zakładkę **Inne** → **MQTT**
2. Wpisz dane twojego brokera MQTT:
   - **Host** - IP lub nazwa hosta brokera (np. 192.168.1.100)
   - **Port** - port brokera (domyślnie 1883)
   - **Użytkownik** - opcjonalnie, jeśli wymagany
   - **Hasło** - opcjonalnie, jeśli wymagane
   - **Prefix discovery** - zazwyczaj zostaw "homeassistant"
3. Kliknij **Zapisz MQTT**

W Home Assistant urządzenie pojawi się automatycznie (auto-discovery).

</details>

<details>
<summary><strong>WiFi</strong></summary>

Zmiana sieci WiFi i hasła.

**Jak zmienić sieć WiFi:**
1. Wejdź na **Inne** → **WiFi**
2. Wpisz:
   - **SSID** - nazwa twojej sieci WiFi
   - **Hasło** - hasło do WiFi
   - **Hasło AP** - hasło do panelu web (min. 8 znaków)
3. Opcjonalnie zmień statyczne IP (jeśli chcesz stały adres)
4. Kliknij **Zapisz**

Urządzenie połączy się z nową siecią.

**Przywróć domyślną sieć:**
- Kliknij **Reset WiFi (całość)** - urządzenie aktywuje tryb AP z hasłem "12345678"

</details>

<details>
<summary><strong>OTA</strong></summary>

Aktualizacja oprogramowania.

**Informacje:**
- **Status** - co aktualnie robi urządzenie
- **Wersja** - aktualna wersja oprogramowania
- **Wersja zdalna** - wersja dostępna na GitHubie
- **Dostępna aktualizacja** - czy jest nowa wersja

**Przyciski:**
- **Wyślij firmware** - ręczna aktualizacja z pliku .bin
- **Przełącz slot i restart** - przełączenie się do drugiej partycji (jeśli test nie powiedzie się)
- **🧪 TEST** - ręczne sprawdzenie GitHub (widoczny tylko jeśli włączony w konfiguracji)

</details>

<details>
<summary><strong>Instrukcja</strong></summary>

Ta instrukcja w formie interaktywnej. Czytasz ją właśnie teraz! 📖

</details>

---

## Konfiguracja WiFi

### Tryb AP (Punkt dostępu)

Jeśli urządzenie nie może połączyć się z siecią WiFi, lub chcesz zmienić sieć:

1. Szukaj sieci WiFi o nazwie `LedMatrixClock` (lub `LedMatrixClock-AP`)
2. Połącz się z hasłem `12345678`
3. Wejdź na `http://192.168.4.1`
4. Przejdź do **Inne** → **WiFi**
5. Zmień sieć i hasło
6. Po połączeniu urządzenie wyłączy tryb AP

### Statyczne IP

Aby urządzenie miało zawsze ten sam adres IP:

1. Wejdź na **Inne** → **WiFi**
2. Wyłącz **DHCP** (odznacz checkbox)
3. Wpisz:
   - **IP** - adres statyczny (np. 192.168.1.100)
   - **GW** - brama (zwykle 192.168.1.1)
   - **Subnet** - maska podsieci (zwykle 255.255.255.0)
4. Kliknij **Zapisz**

---

## Integracja MQTT (Home Assistant)

### Co to jest MQTT?

MQTT to protokół komunikacji dla domowych urządzeń inteligentnych. Pozwala na automatyzację i sterowanie urządzeniami z Home Assistant.

### Wymagania

- Działający serwer Home Assistant
- Zainstalowany broker MQTT (np. Mosquitto)

### Konfiguracja

1. Wejdź do panelu web na **Inne** → **MQTT**
2. Wpisz dane twojego brokera:
   ```
   Host: 192.168.1.YOUR_MQTT_IP
   Port: 1883
   Użytkownik: (jeśli wymagany)
   Hasło: (jeśli wymagane)
   ```
3. Kliknij **Zapisz MQTT**
4. W Home Assistant powinna pojawić się automatycznie nowa integracja

### Sterowanie z Home Assistant

Po podłączeniu urządzenie udostępni następujące byty:
- Jasność LED
- Kolor LED
- Tryb wyświetlania
- Status połączenia
- i inne...

---

## Logi i diagnostyka

### Włączenie logów

1. Wejdź na **Logi**
2. Zaznacz **"Włącz logi"**
3. Zaznacz **"Auto"** aby logi odświeżały się automatycznie
4. Kliknij **Odśwież** aby ręcznie odświeżyć

### Czytanie logów

Logi pokazują co robi urządzenie:
- Połączenia WiFi
- Aktualizacje z GitHub
- Błędy i ostrzeżenia
- Działanie MQTT

**Typowe wpisy:**
```
[WiFi] Connected to: MyNetwork (IP: 192.168.1.100)
[GitHub OTA] Checking for new version...
[GitHub OTA] New version available: 1.0.3
[GitHub OTA] Downloaded and installed successfully
```

### Eksport logów

Możesz skopiować logi i wysłać je w celu diagnozy.

---

## Rozwiązywanie problemów

### Urządzenie nie łączy się z WiFi

**Przyczyny i rozwiązania:**
1. **Błędne hasło** - sprawdź czy hasło do WiFi jest poprawne
2. **Sieć 5GHz** - ESP32-S3 obsługuje tylko 2.4GHz, zmień na 2.4GHz
3. **Zbyt daleko** - przybliż urządzenie do routera
4. **Router overload** - zrestartuj router

**Jak naprawić:**
- Przejdź w tryb AP (szukaj `LedMatrixClock-AP`)
- Zmień sieć WiFi na zakładce **WiFi**

### Aktualizacja nie przychodzi

**Przyczyny:**
1. **Brak internetu** - urządzenie musi mieć dostęp do GitHuba
2. **Problemy z DNS** - router może blokować GitHub
3. **Tryb offline** - sprawdź czy WiFi jest włączony

**Jak sprawdzić:**
- Wejdź na **Inne** → **OTA**
- Kliknij przycisk **Odśwież status**
- Sprawdź czy "Wersja zdalna" się zaktualizowała

### LED nie świecą lub świecą słabo

**Przyczyny:**
1. **Mało zasilania** - zasilacz musi być minimum 8A
2. **Rozaląd przewodu** - sprawdź podłączenie matrycy
3. **Maksymalna jasność ustawiona na 0** - przejdź do **Zegar** i zwiększ jasność

### Panel web nie odpowiada

**Co robić:**
1. Zrestartuj urządzenie (wyłącz i włącz zasilanie)
2. Czekaj 30 sekund aż się uruchomi
3. Ponownie otwórz panel web
4. Jeśli to nie pomoże, sprawdź logi WiFi

### MQTT nie łączy się

**Sprawdzenia:**
1. Czy broker MQTT jest włączony?
2. Czy poprawnie wpisałeś IP/host brokera?
3. Czy hasło jest poprawne?
4. Czy port 1883 jest dostępny?

**Podpowiedź:** Sprawdź logi na zakładce **Logi** - tam zobaczysz błąd MQTT.

---

## 🆘 Potrzebujesz pomocy?

Jeśli coś nie działa:
1. Wejdź na **Logi** i włącz logi diagnostyczne
2. Zrób screenshot/zapis błędu
3. Sprawdź czy połączenie WiFi jest stabilne
4. Zrestartuj urządzenie

---

**Wersja instrukcji:** 1.0  
**Data:** Marzec 2026  
**Ostatnia aktualizacja:** 09.03.2026
