#include "instrukcja.h"

const char* getInstrukcjaHtml() {
    return R"(
<div style='line-height:1.6;font-size:0.95em;color:#ddd'>
<h4 style='color:#fff;margin-top:1em;margin-bottom:0.5em'>[INSTRUKCJA] Obsługa Zegara LED</h4>
Witaj! Ta instrukcja wyjaśnia jak korzystać z panelu web twojego zegara LED.

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[UPDATE] Aktualizacja oprogramowania</h4>
<p><strong>Automatyczna aktualizacja</strong></p>
<p>Twoje urządzenie <strong>automatycznie pobiera i instaluje</strong> nowe wersje oprogramowania. Nie musisz nic robić!</p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Każdego dnia urządzenie sprawdza czy jest nowa wersja</li>
<li>Jeśli jest dostępna, urządzenie ją pobiera i instaluje</li>
<li>Jeśli coś pójdzie nie tak, powraca do poprzedniej wersji (bezpieczne A/B)</li>
</ul>

<p><strong>Ręczna aktualizacja</strong></p>
<ol style='margin:0.5em 0;padding-left:1.5em'>
<li>Przejdź do <strong>Inne</strong> > zakładka <strong>OTA</strong></li>
<li>Kliknij na pole &quot;Plik firmware (.bin)&quot;</li>
<li>Wybierz plik .bin ze swojego komputera</li>
<li>Kliknij <strong>&quot;Wyślij firmware&quot;</strong></li>
<li>Czekaj aż postęp osiągnie 100% - urządzenie automatycznie się zrestartuje</li>
</ol>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[MAIN] Główne zakładki</h4>
<p><strong>Zegar</strong> - Ustawienie czasu, efektów, koloru i jasności</p>
<p><strong>Efekty</strong> - Rozmaite efekty wizualne dla matrycy LED</p>
<p><strong>Cytaty</strong> - Cytaty, testowanie, edycja listy</p>
<p><strong>Urodziny</strong> - Lista imion i dat urodzin z losowymi życzeniami</p>
<p><strong>Harmonogram</strong> - Jasność i kolory w zależności od pory dnia</p>
<p><strong>Logi</strong> - Historia działania, diagnostyka</p>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[MENU] Sekcja Inne</h4>
<p><strong>MQTT:</strong> Integracja z Home Assistant. Wpisz host, port i hasło brokera MQTT.</p>
<p><strong>WiFi:</strong> Zmiana sieci WiFi. Hasło musi mieć min. 8 znaków. Opcjonalnie wprowadź statyczne IP.</p>
<p><strong>OTA:</strong> Aktualizacja oprogramowania. Pokaż status, wersję, i przyciski do aktualizacji.</p>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[AP] Tryb AP (Punkt dostępu WiFi)</h4>
<p><strong>Kiedy aktywuje się tryb AP?</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Gdy urządzenie <strong>pierwszy raz się uruchamia</strong> - nie ma zapisanej sieci WiFi</li>
<li>Gdy urządzenie <strong>nie może połączyć się z zapisaną siecią</strong> - WiFi wymaga nowego hasła lub sieci nie ma</li>
<li>Gdy wciśniesz przycisk <strong>resetowania AP</strong> (jeśli urządzenie go ma)</li>
</ul>

<p><strong>Jak działa tryb AP?</strong></p>
<p>Urządzenie <strong>sam tworzy sieć WiFi</strong> do której możesz się podłączyć. Sieć ta:</p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li><strong>Nazwa sieci:</strong> LedMatrixClock lub LedMatrixClock-AP</li>
<li><strong>Hasło:</strong> 12345678</li>
<li><strong>Nie ma internetu!</strong> - to tylko komunikacja między Tobą a urządzeniem</li>
<li><strong>Mały zasięg</strong> - działa na odległość kilka metrów</li>
</ul>

<p><strong>Jak się podłączyć i zmienić sieć WiFi?</strong></p>
<ol style='margin:0.5em 0;padding-left:1.5em'>
<li><strong>Na telefonie lub komputerze:</strong> Szukaj dostępnych sieci WiFi</li>
<li>Połącz się z <strong>LedMatrixClock</strong> lub <strong>LedMatrixClock-AP</strong></li>
<li>Wpisz hasło: <strong>12345678</strong></li>
<li>Otwórz przeglądarkę i wejdź na: <strong>http://192.168.4.1</strong></li>
<li>Zobaczysz panel konfiguracyjny zegara LED</li>
<li>Przejdź na zakładkę <strong>Inne &gt; WiFi</strong></li>
<li>Wybierz sieć WiFi do której chcesz się podłączyć</li>
<li>Wpisz hasło do tej sieci (min. 8 znaków)</li>
<li>Opcjonalnie ustaw <strong>statyczne IP</strong> (jeśli znasz co to)</li>
<li>Kliknij <strong>Zapisz WiFi</strong></li>
</ol>

<p><strong>Co się dzieje po zapisaniu nowej sieci?</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Urządzenie <strong>się zrestartuje</strong> (zgaśnie LED na chwilę)</li>
<li>Spróbuje się <strong>podłączyć do nowej sieci WiFi</strong></li>
<li>Gdy się podłączy, <strong>tryb AP wyłączy się</strong> - nie będzie już widoczna sieć LedMatrixClock</li>
<li>Będziesz mógł wejść na panel web używając <strong>hlavnej IP adresu urządzenia</strong></li>
</ul>

<p><strong>Jak znaleźć adres IP urządzenia w sieci głównej?</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li><strong>Metoda 1:</strong> W routerze WiFi - szukaj &quot;LedMatrixClock&quot; w liście urządzeń</li>
<li><strong>Metoda 2:</strong> W logach zegara - przejdź do <strong>Logi</strong> i poszukaj &quot;IP:&quot;</li>
<li><strong>Metoda 3:</strong> Jeśli masz MQTT - urządzenie wyśle swój adres IP do Home Assistant</li>
<li><strong>Metoda 4:</strong> Spróbuj: <strong>http://LedMatrixClock.local</strong> (jeśli router to obsługuje)</li>
</ul>

<p><strong>Problemy z połączeniem?</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li><strong>Sieć LedMatrixClock-AP nie pojawia się:</strong> Poczekaj - urządzenie potrzebuje kilka sekund aby się uruchomić</li>
<li><strong>Hasło nie działa:</strong> Spróbuj <strong>12345678</strong> (osiem cyfr)</li>
<li><strong>Strona 192.168.4.1 nie ładuje się:</strong> Upewnij się że jesteś podłączony do sieci AP, czekaj kilka sekund</li>
<li><strong>Po zapisaniu WiFi urządzenie się nie łączy:</strong> Sprawdzisz czy hasło jest poprawne i czy ta sieć istnieje</li>
</ul>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>Tryb AP (Punkt dostępu)</h4>
<p>Jeśli urządzenie nie łączy się z WiFi:</p>
<ol style='margin:0.5em 0;padding-left:1.5em'>
<li>Szukaj sieci <strong>LedMatrixClock</strong> lub <strong>LedMatrixClock-AP</strong></li>
<li>Połącz się z hasłem <strong>12345678</strong></li>
<li>Wejdź na <strong>http://192.168.4.1</strong></li>
<li>Zmień sieć WiFi na zakładce <strong>Inne &gt; WiFi</strong></li>
</ol>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[HOME] MQTT (Home Assistant)</h4>
<p>Pozwala na sterowanie urządzeniem z Home Assistant.</p>
<ol style='margin:0.5em 0;padding-left:1.5em'>
<li>Wejdź na <strong>Inne &gt; MQTT</strong></li>
<li>Wpisz dane twojego brokera MQTT</li>
<li>Kliknij <strong>Zapisz MQTT</strong></li>
<li>W Home Assistant urządzenie pojawi się automatycznie (auto-discovery)</li>
</ol>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[CLOCK] Zegar - główna zakładka</h4>
<p><strong>Co tutaj można robić?</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li><strong>Zmieniaj godzinę:</strong> Możesz ustawić godzinę ręcznie (przydatne gdy WiFi nie działa)</li>
<li><strong>Efekt wyświetlania:</strong> Wybierz jak ma wyglądać zegar (zwykły, migający, animowany)</li>
<li><strong>Kolor zegara:</strong> Kliknij na kolor aby zmienić barwę wyświetlanego tekstu</li>
<li><strong>Jasność:</strong> Reguluj od 0% (ciemno) do 100% (maksimum)</li>
<li><strong>Automatyczne wyłączanie:</strong> Ustaw o której godzinie LED ma się wyłączyć</li>
<li><strong>Czcionka:</strong> Wybierz rozmiar liter (mała, normalna, duża)</li>
</ul>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[EFFECTS] Efekty - animacje LED</h4>
<p><strong>Co to są efekty?</strong></p>
<p>Efekty to piękne animacje wyświetlane na macierzy LED zamiast lub obok zegara.</p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li><strong>Wyłączony (OFF):</strong> Tylko zegar, bez animacji</li>
<li><strong>Spadające piksele:</strong> Kolorowe piksele spadają z góry na dół</li>
<li><strong>Pulsujące światło:</strong> LED pulsuje jak tętnienie serca</li>
<li><strong>Rainbow (Tęcza):</strong> Kolory zmieniają się w cykl tęczy</li>
<li><strong>Fale:</strong> Animacja fal przebiegających przez całą matrycę</li>
<li><strong>Błyski:</strong> Losowe błyski światła</li>
</ul>
<p><strong>Jak zmienić efekt?</strong></p>
<ol style='margin:0.5em 0;padding-left:1.5em'>
<li>Przejdź na zakładkę <strong>Efekty</strong></li>
<li>Wybierz efekt z listy</li>
<li>Dostosuj prędkość (szybko/wolno)</li>
<li>Kliknij <strong>Zapisz</strong></li>
</ol>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[QUOTES] Cytaty - inspirująca wiadomość</h4>
<p><strong>Co to są cytaty?</strong></p>
<p>Zamiast zegara urządzenie może wyświetlić losowy cytat lub wiadomość inspirującą.</p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li><strong>Włącz cytaty:</strong> Zaznacz opcję aby cytaty były pokazywane</li>
<li><strong>Co ile czasu:</strong> Cytaty wyświetlają się co kilka minut (domyślnie co 5 minut)</li>
<li><strong>Jak długo:</strong> Cytat wyświetla się przez kilka sekund, potem wraca zegar</li>
</ul>
<p><strong>Jak dodać własne cytaty?</strong></p>
<ol style='margin:0.5em 0;padding-left:1.5em'>
<li>Przejdź na zakładkę <strong>Cytaty</strong></li>
<li>W polu tekstowym wpisz nowy cytat (max 255 znaków)</li>
<li>Kliknij <strong>Dodaj cytat</strong></li>
<li>Kliknij <strong>Testuj</strong> aby zobaczyć jak będzie wyglądać</li>
<li>Kliknij <strong>Wyślij listę</strong> aby zapisać wszystkie cytaty</li>
</ol>
<p><strong>Zarządzanie cytatami:</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Możesz usunąć cytat klikając X obok niego</li>
<li>Lista cytatów jest zapisana w pamięci urządzenia</li>
<li>Cytaty pozostają nawet po zrestartowaniu urządzenia</li>
</ul>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[BIRTHDAY] Urodziny - życzenia na specjalny dzień</h4>
<p><strong>Co to są urodziny?</strong></p>
<p>Możesz dodać imiona i daty urodzin swoich bliskich. W dniu urodzin urządzenie wyświetli losowe życzenia!</p>
<p><strong>Jak dodać urodziny?</strong></p>
<ol style='margin:0.5em 0;padding-left:1.5em'>
<li>Przejdź na zakładkę <strong>Urodziny</strong></li>
<li>Wpisz <strong>imię</strong> osoby</li>
<li>Wybierz <strong>datę urodzin</strong> (dzień i miesiąc)</li>
<li>Kliknij <strong>Dodaj osobę</strong></li>
<li>Kliknij <strong>Wyślij listę</strong> aby zapisać</li>
</ol>
<p><strong>Co się dzieje w dniu urodzin?</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Zamiast zegara wyświetli się &quot;Wszystkiego najlepszego [IMI&Ę]!&quot;</li>
<li>Urządzenie pokaże losowe życzenia urodzinowe</li>
<li>Możesz wyłączyć tę funkcję w ustawieniach</li>
</ul>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[SCHEDULER] Harmonogram - jasność o różnych porach dnia</h4>
<p><strong>Co to jest harmonogram?</strong></p>
<p>Pozwala na automatyczną zmianę jasności i koloru urządzenia w zależności od pory dnia.</p>
<p><strong>Przykład:</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>6:00 - 7:30 - LED bardzo jasne (budzik)</li>
<li>7:30 - 23:00 - LED normalne (dzień)</li>
<li>23:00 - 6:00 - LED ciemne (noc, nie rozprasza)</li>
</ul>
<p><strong>Jak ustawić harmonogram?</strong></p>
<ol style='margin:0.5em 0;padding-left:1.5em'>
<li>Przejdź na zakładkę <strong>Harmonogram</strong></li>
<li>Ustaw godzinę rozpoczęcia i zakończenia dla każdej strefy</li>
<li>Ustaw jasność dla każdej strefy (0% = ciemno, 100% = maksimum)</li>
<li>Opcjonalnie zmień kolor dla strefy</li>
<li>Kliknij <strong>Zapisz harmonogram</strong></li>
</ol>
<p><strong>Spostrzeżenia:</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Harmonogram zmienia się automatycznie zgodnie z systemowym zegarkiem urządzenia</li>
<li>Jeśli ręcznie zmienisz jasność, harmonogram wciąż będzie aktywny</li>
<li>Możesz wyłączyć harmonogram w ustawieniach</li>
</ul>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[LOGS] Logi i diagnostyka</h4>
<ol style='margin:0.5em 0;padding-left:1.5em'>
<li>Wejdź na <strong>Logi</strong></li>
<li>Zaznacz <strong>&quot;Włącz logi&quot;</strong></li>
<li>Zaznacz <strong>&quot;Auto&quot;</strong> dla odświeżania automatycznego</li>
<li>Logi pokażą co robi urządzenie - połączenia, aktualizacje, błędy</li>
</ol>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[HELP] Rozwiązywanie problemów</h4>

<p><strong>Urządzenie nie łączy się z WiFi:</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Sprawdź czy hasło jest poprawne</li>
<li>Zmień na WiFi 2.4GHz (ESP32-S3 nie obsługuje 5GHz)</li>
<li>Przybliż urządzenie do routera</li>
<li>Użyj trybu AP (LedMatrixClock-AP) aby zmienić sieć</li>
</ul>

<p><strong>Aktualizacja nie przychodzi:</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Sprawdź czy jest internet</li>
<li>Wejdź na <strong>Inne &gt; OTA</strong> i kliknij <strong>Odśwież status</strong></li>
<li>Sprawdź czy &quot;Wersja zdalna&quot; się zaktualizowała</li>
</ul>

<p><strong>LED nie świecą:</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Sprawdź podłączenie matrycy LED</li>
<li>Zasilacz musi być min. 8A</li>
<li>Przejdź do <strong>Zegar</strong> i zwiększ jasność</li>
</ul>

<p><strong>Panel web nie odpowiada:</strong></p>
<ul style='margin:0.5em 0;padding-left:1.5em'>
<li>Zrestartuj urządzenie (wyłącz zasilanie)</li>
<li>Czekaj 30 sekund aż się uruchomi</li>
<li>Ponownie otwórz panel web</li>
</ul>

<h4 style='color:#fff;margin-top:1.5em;margin-bottom:0.5em'>[SETTINGS] Przykładowe ustawienia</h4>
<p><strong>WiFi:</strong> Wpisz SSID i hasło do twojej sieci WiFi</p>
<p><strong>MQTT:</strong> Host: 192.168.1.100, Port: 1883 (bez hasła jeśli nie wymagane)</p>
<p><strong>Jasność:</strong> 0-255 (0 = ciemno, 255 = maksimum)</p>

<hr style='border:1px solid #555;margin:1.5em 0'>
<p style='font-size:0.85em;color:#999'>Wersja instrukcji: 1.0 | Marzec 2026 | Ostatnia aktualizacja: 2026-03-09</p>
</div>
    )";
}
