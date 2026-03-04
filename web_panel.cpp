#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <stdlib.h>
#include "web_panel.h"
#include "clock.h"
#include "display.h"
#include "quotes.h"
#include "app_logger.h"
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<title>LedMatrixClock Panel</title>
<style>
body { font-family: Arial; background: #222; color: #eee; margin: 0; padding: 0; }
.container { max-width: 600px; margin: 40px auto; background: #333; padding: 24px; border-radius: 8px; box-shadow: 0 0 8px #0006; }
h1 { font-size: 1.4em; margin-bottom: 16px; }
input, select, button { width: 100%; margin: 8px 0; padding: 8px; border-radius: 4px; border: none; }
button { background: #1976d2; color: #fff; font-weight: bold; cursor: pointer; }
button:hover { background: #1565c0; }
label { margin-top: 12px; display: block; }
hr { border: 0; border-top: 1px solid #444; margin: 24px 0; }
.tabs { display: flex; flex-wrap: wrap; margin-bottom: 24px; gap: 2px; }
.tab { flex: 1; min-width: 80px; background: #222; color: #eee; padding: 8px 6px; text-align: center; cursor: pointer; border-radius: 4px; font-size: 12px; font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.tab.active { background: #1976d2; color: #fff; font-weight: bold; }
.tab-content { display: none; }
.tab-content.active { display: block; }
.checkbox { width: auto; }
</style>
</head>
<body>
<div class="container">
<h1>LedMatrixClock Panel</h1>
<div class="tabs">
    <div class="tab active" onclick="showTab('status')">Status</div>
    <div class="tab" onclick="showTab('wifi')">WiFi</div>
    <div class="tab" onclick="showTab('czas')">Czas</div>
    <div class="tab" onclick="showTab('mqtt')">MQTT</div>
    <div class="tab" onclick="showTab('display')">Wyświetlacz</div>
    <div class="tab" onclick="showTab('schedule')">Harmonogram</div>
    <div class="tab" onclick="showTab('quotes')">Cytaty</div>
    <div class="tab" onclick="showTab('ota')">OTA</div>
    <div class="tab" onclick="showTab('diagnostyka')">Diagnostyka</div>
    <div class="tab" onclick="showTab('import')">Import/Eksport</div>
</div>
<div id="status" class="tab-content active">
    <h2>Status</h2>
    <div id="statusData"></div>
</div>
<div id="wifi" class="tab-content">
    <h2>KONFIGURACJA WIFI</h2>
    <form method="POST" action="/save_wifi">
        <label>SSID <input name="ssid" required value="%WIFI_SSID%" placeholder="Obecna sieć"></label>
        <label>Hasło <input name="password" type="password" value="%WIFI_PASS%" placeholder="Hasło WiFi"></label>
        <label><input type="checkbox" name="static" class="checkbox" %WIFI_STATIC%> Statyczny IP</label>
        <label>IP <input name="ip" value="%WIFI_IP%" placeholder="192.168.1.100"></label>
        <label>Brama <input name="gateway" value="%WIFI_GATEWAY%" placeholder="192.168.1.1"></label>
        <label>DNS <input name="dns" value="%WIFI_DNS%" placeholder="8.8.8.8"></label>
        <button type="submit">Zapisz</button>
    </form>
</div>
<div id="czas" class="tab-content">
    <h2>Ustawienia Czasu</h2>
    <form method="POST" action="/save_time">
        <label>Serwer NTP <input type="text" name="ntpServer" value="%DISP_NTP%" placeholder="pool.ntp.org"></label>
        <label>Strefa czasowa:
            <select name="timezone" id="timezoneSelect">
                <option value="-43200">UTC-12:00 (Samoa Standard Time)</option>
                <option value="-39600">UTC-11:00 (Niue)</option>
                <option value="-36000">UTC-10:00 (Hawaii, Cook Islands)</option>
                <option value="-34200">UTC-09:30 (Marquesas)</option>
                <option value="-32400">UTC-09:00 (Alaska, Gambier)</option>
                <option value="-28800">UTC-08:00 (Pacific Daylight)</option>
                <option value="-25200">UTC-07:00 (Mountain Time)</option>
                <option value="-21600">UTC-06:00 (Central Time)</option>
                <option value="-18000">UTC-05:00 (Eastern Time)</option>
                <option value="-16200">UTC-04:30 (Venezuela)</option>
                <option value="-14400">UTC-04:00 (Atlantic Time)</option>
                <option value="-12600">UTC-03:30 (Newfoundland)</option>
                <option value="-10800">UTC-03:00 (Brazil, Suriname)</option>
                <option value="-9000">UTC-02:30</option>
                <option value="-7200">UTC-02:00 (South Georgia)</option>
                <option value="-3600">UTC-01:00 (Azores, Cape Verde)</option>
                <option value="0">UTC+00:00 (GMT - London, Lisbon)</option>
                <option value="3600">UTC+01:00</option>
                <option value="7200">UTC+02:00</option>
                <option value="10800">UTC+03:00</option>
                <option value="12600">UTC+03:30 (Iran)</option>
                <option value="14400">UTC+04:00</option>
                <option value="16200">UTC+04:30 (Afghanistan)</option>
                <option value="18000">UTC+05:00 (Pakistan)</option>
                <option value="19800">UTC+05:30 (India, Sri Lanka)</option>
                <option value="20700">UTC+05:45 (Nepal)</option>
                <option value="21600">UTC+06:00</option>
                <option value="23400">UTC+06:30 (Myanmar)</option>
                <option value="25200">UTC+07:00 (Bangkok, Jakarta)</option>
                <option value="28800">UTC+08:00 (Beijing, Singapore)</option>
                <option value="31500">UTC+08:45 (Eucla, Australia)</option>
                <option value="32400">UTC+09:00 (Tokyo, Seoul)</option>
                <option value="34200">UTC+09:30 (Adelaide)</option>
                <option value="36000">UTC+10:00 (Sydney, Melbourne)</option>
                <option value="37800">UTC+10:30 (Lord Howe Island)</option>
                <option value="39600">UTC+11:00</option>
                <option value="41400">UTC+11:30</option>
                <option value="43200">UTC+12:00 (Fiji, New Zealand)</option>
                <option value="45900">UTC+12:45 (Chatham Islands)</option>
                <option value="46800">UTC+13:00</option>
                <option value="50400">UTC+14:00 (Kiribati)</option>
            </select>
        </label>
        <button type="submit">Zapisz ustawienia</button>
    </form>
    <hr>
    <h2>Aktualny czas</h2>
    <div style="background:#444; padding:15px; border-radius:4px; text-align:center;">
        <div style="font-size:32px; font-weight:bold; color:#1976d2;" id="currentTimeDisplay">--:--:--</div>
        <div style="margin-top:10px; color:#aaa;" id="dateDisplay">--</div>
    </div>
</div>
<div id="mqtt" class="tab-content">
    <h2>KONFIGURACJA MQTT</h2>
    <form method="POST" action="/save_mqtt">
        <label>Broker <input name="broker" value="%MQTT_BROKER%"></label>
        <label>Port <input name="port" type="number" value="%MQTT_PORT%"></label>
        <label>User <input name="user" value="%MQTT_USER%"></label>
        <label>Password <input name="password" type="password" value="%MQTT_PASS%"></label>
        <label>Client ID <input name="clientId" value="%MQTT_CLIENTID%"></label>
        <label>Topic publish <input name="topicPub" value="%MQTT_PUB%"></label>
        <label>Topic subscribe <input name="topicSub" value="%MQTT_SUB%"></label>
        <button type="submit">Zapisz</button>
        <button type="button" onclick="testMqtt()">Test połączenia</button>
    </form>
</div>
<div id="display" class="tab-content">
    <h2>USTAWIENIA WYŚWIETLACZA</h2>
    <form method="POST" action="/save_display">
        <label>Jasność <input name="brightness" type="number" min="0" max="255" value="%DISP_BRIGHT%"></label>
        <label><input type="checkbox" name="autoBrightness" class="checkbox" %DISP_AUTO%> Auto jasność</label>
        <label><input type="checkbox" name="hour24" class="checkbox" %DISP_24H%> Tryb 24h</label>
        <label><input type="checkbox" name="showSeconds" class="checkbox" %DISP_SEC%> Pokazuj sekundy</label>
        <label>Serwer NTP <input name="ntpServer" value="%DISP_NTP%"></label>
        <button type="submit">Zapisz ustawienia</button>
    </form>
    
    <h2>WYSYŁANIE WIADOMOŚCI</h2>
    <form id="messageForm" onsubmit="return false;">
        <label>Tekst wiadomości:
            <textarea id="runtimeMessage" style="width:100%; height:60px; border:1px solid #666; background:#222; color:#eee; padding:8px; border-radius:4px;" maxlength="127" placeholder="Wpisz wiadomość..."></textarea>
        </label>
        <label>Czas wyświetlania (ms): <span id="msgTimeValue">5000</span>
            <input type="range" id="runtimeMessageTime" min="1000" max="30000" step="1000" value="5000" oninput="document.getElementById('msgTimeValue').textContent=this.value">
        </label>
        <label>Kolor wiadomości:
            <input type="color" id="runtimeMessageColor" value="#00FF00" style="width:100%; height:40px; cursor:pointer;">
        </label>
        <button type="button" onclick="sendRuntimeMessage()" style="background:#ff9800;">Wyślij wiadomość</button>
    </form>
    
    <h2>ANIM (ściemnianie)</h2>
    <form id="dimmingForm" onsubmit="return false;">
        <label>Jasność runtime: <span id="runtimeBrightnessValue">%DISP_BRIGHT%</span>
            <input id="runtimeBrightness" name="runtimeBrightness" type="range" min="1" max="255" value="%DISP_BRIGHT%" oninput="document.getElementById('runtimeBrightnessValue').textContent=this.value">
        </label>
        <button type="button" onclick="applyBrightnessOnly()">Zastosuj jasność</button>
    </form>
</div>
<div id="schedule" class="tab-content">
    <h2>ROTACJA ANIMACJI</h2>
    <form id="animationScheduleForm">
        <label>
            <input type="checkbox" class="checkbox" id="animRotEnabled" name="animRotEnabled"> 
            Włącz rotację animacji
        </label>
        <label>
            Interwał zmiany (minuty):
            <input type="number" id="animRotInterval" name="animRotInterval" min="1" max="1440" value="5">
        </label>
        <h3>Aktywne animacje</h3>
        <label><input type="checkbox" class="checkbox" name="anim0"> 🌈 Rainbow</label>
        <label><input type="checkbox" class="checkbox" name="anim1"> 🔆 Fade</label>
        <label><input type="checkbox" class="checkbox" name="anim2"> 🌊 Wave</label>
        <label><input type="checkbox" class="checkbox" name="anim3"> 💫 Pulse</label>
        <label><input type="checkbox" class="checkbox" name="anim4"> 🌙 Night</label>
        <button type="button" onclick="saveAnimationSchedule()">Zapisz rotację</button>
    </form>
    
    <hr>
    
    <h2>ZAPLANOWANE WIADOMOŚCI</h2>
    <div id="messagesList" style="margin-bottom: 20px;"></div>
    <form id="addMessageForm">
        <label>Godzina (HH):
            <input type="number" id="msgHour" min="0" max="23" value="12">
        </label>
        <label>Minuta (MM):
            <input type="number" id="msgMinute" min="0" max="59" value="0">
        </label>
        <label>Tekst wiadomości:
            <textarea id="msgText" style="width:100%; height:80px; border:1px solid #666; background:#222; color:#eee;" maxlength="127" required></textarea>
        </label>
        <label>Czas wyświetlania (sekundy):
            <input type="number" id="msgDuration" min="1" max="300" value="30">
        </label>
        <button type="button" onclick="addScheduledMessage()">Dodaj wiadomość</button>
    </form>
    
    <hr>
    
    <h2>LOSOWE CYTATY</h2>
    <form id="randomQuotesForm">
        <label>
            <input type="checkbox" class="checkbox" id="randomQuotesEnabled" name="randomQuotesEnabled"> 
            Włącz losowanie cytatów
        </label>
        <label>
            Interwał losowania (minuty):
            <input type="number" id="randomQuotesInterval" min="1" max="1440" value="60">
        </label>
        <label>
            Od godziny (HH):
            <input type="number" id="randomQuotesStartHour" min="0" max="23" value="8">
        </label>
        <label>
            Do godziny (HH):
            <input type="number" id="randomQuotesEndHour" min="0" max="23" value="22">
        </label>
        <button type="button" onclick="saveRandomQuotesSchedule()">Zapisz losowanie cytatów</button>
    </form>
</div>
<div id="quotes" class="tab-content">
    <h2>Edycja Cytatów</h2>
    <div style="margin-bottom: 15px; display: flex; gap: 10px;">
        <button type="button" style="flex:1; background:#388e3c;" onclick="exportQuotes()">📥 Eksport JSON</button>
        <button type="button" style="flex:1; background:#1976d2;" onclick="document.getElementById('importQuotesInput').click()">📤 Import JSON</button>
        <input type="file" id="importQuotesInput" accept=".json" style="display:none" onchange="importQuotes(event)">
    </div>
    <div id="quotesList" style="margin-bottom: 20px;"></div>
    <form onsubmit="addQuote(event)">
        <label>Nowy cytat <textarea name="quote" style="width:100%; height:60px; border:1px solid #666; background:#222; color:#eee;" required></textarea></label>
        <button type="submit">Dodaj cytat</button>
    </form>
</div>
<div id="ota" class="tab-content">
    <h2>OTA update</h2>
    <form method="POST" action="/ota" enctype="multipart/form-data">
        <input type="file" name="firmware">
        <button type="submit">Wyślij</button>
    </form>
</div>
<div id="diagnostyka" class="tab-content">
    <h2>Diagnostyka Systemu</h2>
    <label>
        <input type="checkbox" class="checkbox" id="logsEnabled" onchange="saveLogsConfig()"> 
        Włącz rejestrowanie logów
    </label>
    <div style="margin-top: 15px; display: flex; gap: 10px;">
        <button type="button" style="flex:1;" onclick="loadLogs()">🔄 Odśwież logi</button>
        <button type="button" style="flex:1; background:#388e3c;" onclick="downloadLogs()">📥 Pobierz logi</button>
        <button type="button" style="flex:1; background:#d32f2f;" onclick="clearLogsBuffer()">🗑️ Wyczyść</button>
    </div>
    <div id="logsList" style="margin-top: 15px; background: #222; padding: 10px; border-radius: 4px; max-height: 400px; overflow-y: auto; font-family: monospace; font-size: 11px; color: #0f0; white-space: pre-wrap; word-wrap: break-word;"></div>
</div>
<div id="import" class="tab-content">
    <h2>Import/Eksport konfiguracji</h2>
    <form method="GET" action="/export">
        <button type="submit">Eksport konfiguracji (JSON)</button>
    </form>
    <form method="POST" action="/import" enctype="multipart/form-data">
        <input type="file" name="config">
        <button type="submit">Import konfiguracji (JSON)</button>
    </form>
</div>
</div>
<script>
function showTab(tab) {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelector('.tab[onclick*="'+tab+'"]').classList.add('active');
    docu        
                // Update time in CZAS tab
                if(j.time) {
                    document.getElementById('currentTimeDisplay').textContent = j.time;
                }
        ment.getElementById(tab).classList.add('active');
}
function fetchStatus() {
        fetch('/status').then(r=>r.json()).then(j=>{
                let html = '';
                html += 'Czas: ' + j.time + '<br>';
                html += 'IP: ' + j.ip + '<br>';
                html += 'RSSI: ' + j.rssi + ' dBm<br>';
                html += 'MQTT: ' + j.mqtt + '<br>';
                html += 'Uptime: ' + j.uptime + '<br>';
                html += 'Free heap: ' + j.heap + ' B<br>';
                html += 'Firmware: ' + j.fw + '<br>';
                document.getElementById('statusData').innerHTML = html;
        });
}
function testMqtt() {
        fetch('/test_mqtt', {method:'POST'}).then(r=>r.text()).then(alert);
}
function loadQuotes() {
    fetch('/quotes').then(r=>r.json()).then(j=>{
        let html = '<h3>Cytaty (' + j.quotes.length + ')</h3>';
        j.quotes.forEach((q, i) => {
            html += '<div style="background:#444; padding:10px; margin:5px 0; border-radius:4px; display:flex; justify-content:space-between;">';
            html += '<span>' + (i+1) + '. ' + q + '</span>';
            html += '<button style="background:#d32f2f; color:#fff; border:none; padding:5px 10px; cursor:pointer; border-radius:3px;" onclick="deleteQuote(' + i + ')">Usuń</button>';
            html += '</div>';
        });
        document.getElementById('quotesList').innerHTML = html;
    });
}
function addQuote(event) {
    event.preventDefault();
    let quote = event.target.querySelector('textarea').value;
    fetch('/quotes', {
        method: 'POST',
        body: new URLSearchParams({quote: quote})
    }).then(r=>r.json()).then(j=>{
        if(j.ok) {
            event.target.querySelector('textarea').value = '';
            loadQuotes();
        }
    });
}
function deleteQuote(index) {
    if(confirm('Usunąć cytat?')) {
        fetch('/quotes?index=' + index, {method:'DELETE'}).then(r=>r.json()).then(j=>{
            if(j.ok) loadQuotes();
        });
    }
}
function exportQuotes() {
    fetch('/quotes').then(r=>r.json()).then(j=>{
        const quotesData = {
            version: "1.0.0",
            timestamp: new Date().toISOString(),
            count: j.quotes.length,
            quotes: j.quotes
        };
        const dataStr = JSON.stringify(quotesData, null, 2);
        const dataBlob = new Blob([dataStr], {type: 'application/json'});
        const url = URL.createObjectURL(dataBlob);
        const link = document.createElement('a');
        link.href = url;
        link.download = 'quotes_' + new Date().toISOString().split('T')[0] + '.json';
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
        URL.revokeObjectURL(url);
        alert('✓ Cytaty wyeksportowane');
    }).catch(e => alert('✗ Błąd eksportu: ' + e));
}
function importQuotes(event) {
    const file = event.target.files[0];
    if (!file) return;
    
    const reader = new FileReader();
    reader.onload = function(e) {
        try {
            const data = JSON.parse(e.target.result);
            const quotesToImport = data.quotes || [];
            
            if (!Array.isArray(quotesToImport) || quotesToImport.length === 0) {
                alert('✗ Plik JSON nie zawiera cytatów');
                return;
            }
            
            if(confirm('Zaimportować ' + quotesToImport.length + ' cytatów?\n\nOstrzeżenie: Istniejące cytaty nie będą usunięte.')) {
                let imported = 0;
                let failed = 0;
                
                const importNext = (index) => {
                    if (index >= quotesToImport.length) {
                        alert('✓ Importowanie zakończone\nZaimportowano: ' + imported + '\nBłędy: ' + failed);
                        loadQuotes();
                        document.getElementById('importQuotesInput').value = '';
                        return;
                    }
                    
                    const quote = quotesToImport[index];
                    fetch('/quotes', {
                        method: 'POST',
                        body: new URLSearchParams({quote: quote})
                    }).then(r=>r.json()).then(j=>{
                        if(j.ok) {
                            imported++;
                        } else {
                            failed++;
                        }
                        importNext(index + 1);
                    }).catch(() => {
                        failed++;
                        importNext(index + 1);
                    });
                };
                
                importNext(0);
            } else {
                document.getElementById('importQuotesInput').value = '';
            }
        } catch(error) {
            alert('✗ Błąd parsowania JSON: ' + error);
            document.getElementById('importQuotesInput').value = '';
        }
    };
    reader.readAsText(file);
}
function applyBrightnessOnly() {
    const runtimeBrightnessEl = document.getElementById('runtimeBrightness');
    if (!runtimeBrightnessEl) return;
    const value = runtimeBrightnessEl.value;
    const brightness = parseInt(value, 10);
    if (Number.isNaN(brightness)) return;

    fetch('/set_brightness', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'value=' + encodeURIComponent(brightness)
    }).then(r=>r.json()).then(j=>{
        if(!j.ok) alert('✗ Błąd ustawienia jasności');
    }).catch(e => alert('✗ Błąd sieci: ' + e));
}
function sendRuntimeMessage() {
    const msgText = document.getElementById('runtimeMessage');
    const msgTime = document.getElementById('runtimeMessageTime');
    const msgColor = document.getElementById('runtimeMessageColor');
    
    if (!msgText.value.trim()) {
        alert('✗ Wpisz wiadomość');
        return;
    }
    
    const color = msgColor.value.replace('#', '').toUpperCase();
    const params = new URLSearchParams();
    params.append('message', msgText.value);
    params.append('message_time', msgTime.value);
    params.append('message_color', color);
    
    fetch('/control_form', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: params.toString()
    }).then(r=>r.json()).then(j=>{
        if (j.ok) {
            alert('✓ Wiadomość wysłana');
            msgText.value = '';
        } else {
            alert('✗ Błąd: ' + (j.error || 'nieznany'));
        }
    }).catch(e => alert('✗ Błąd sieci: ' + e));
}
function saveAnimationSchedule() {
    const enabled = document.getElementById('animRotEnabled').checked;
    const interval = parseInt(document.getElementById('animRotInterval').value);
    const animations = [];
    
    for (let i = 0; i < 5; i++) {
        if (document.querySelector(`input[name="anim${i}"]`).checked) {
            animations.push({enabled: true, type: i, duration: 0});
        }
    }
    
    const data = {
        schedule: {
            animation: {
                enabled: enabled,
                rotation_interval: interval,
                num_animations: animations.length,
                animations: animations
            }
        }
    };
    
    fetch('/schedule', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
    }).then(r=>r.json()).then(j=>{
        if(j.ok) {
            alert('✓ Rotacja animacji zapisana');
        } else {
            alert('✗ Błąd zapisu');
        }
    }).catch(e => alert('✗ Błąd sieci: ' + e));
}
function addScheduledMessage() {
    const hour = parseInt(document.getElementById('msgHour').value);
    const minute = parseInt(document.getElementById('msgMinute').value);
    const text = document.getElementById('msgText').value;
    const duration = parseInt(document.getElementById('msgDuration').value);
    
    if (text.length === 0) {
        alert('✗ Wpisz tekst wiadomości');
        return;
    }
    
    const data = {
        add_message: {
            hour: hour,
            minute: minute,
            text: text,
            duration: duration
        }
    };
    
    fetch('/schedule', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
    }).then(r=>r.json()).then(j=>{
        if(j.ok) {
            document.getElementById('msgText').value = '';
            alert('✓ Wiadomość dodana');
            loadScheduledMessages();
        } else {
            alert('✗ Błąd: ' + (j.error || 'nieznaną'));
        }
    }).catch(e => alert('✗ Błąd sieci: ' + e));
}
function deleteScheduledMessage(index) {
    if(confirm('Usunąć zaplanowaną wiadomość?')) {
        const data = {delete_message: {index: index}};
        fetch('/schedule', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(data)
        }).then(r=>r.json()).then(j=>{
            if(j.ok) {
                loadScheduledMessages();
            } else {
                alert('✗ Błąd usunięcia');
            }
        });
    }
}
function loadScheduledMessages() {
    fetch('/schedule', {method:'GET'}).then(r=>r.json()).then(j=>{
        let html = '<h3>Zaplanowane wiadomości (' + j.messages.length + ')</h3>';
        if (j.messages.length === 0) {
            html += '<p style="color:#999;">Brak zaplanowanych wiadomości</p>';
        } else {
            j.messages.forEach((m, i) => {
                const time = String(m.hour).padStart(2,'0') + ':' + String(m.minute).padStart(2,'0');
                html += '<div style="background:#444; padding:10px; margin:5px 0; border-radius:4px;">';
                html += '<strong>' + time + '</strong><br>';
                html += m.text + '<br>';
                html += '<small style="color:#aaa;">Czas: ' + m.duration + 's</small><br>';
                html += '<button style="width:auto; background:#d32f2f; margin-top:5px;" onclick="deleteScheduledMessage(' + i + ')">Usuń</button>';
                html += '</div>';
            });
        }
        document.getElementById('messagesList').innerHTML = html;
    });
}
function saveRandomQuotesSchedule() {
    const enabled = document.getElementById('randomQuotesEnabled').checked;
    const interval = parseInt(document.getElementById('randomQuotesInterval').value);
    const startHour = parseInt(document.getElementById('randomQuotesStartHour').value);
    const endHour = parseInt(document.getElementById('randomQuotesEndHour').value);
    
    const data = {
        random_quotes: {
            enabled: enabled,
            interval: interval,
            start_hour: startHour,
            end_hour: endHour
        }
    };
    
    fetch('/schedule', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
    }).then(r=>r.json()).then(j=>{
        if(j.ok) {
            alert('✓ Ustawienia losowych cytatów zapisane');
        } else {
            alert('✗ Błąd zapisu');
        }
    }).catch(e => alert('✗ Błąd sieci: ' + e));
}
function loadRandomQuotesSchedule() {
    fetch('/schedule', {method:'GET'}).then(r=>r.json()).then(j=>{
        if(j.random_quotes) {
            document.getElementById('randomQuotesEnabled').checked = j.random_quotes.enabled;
            document.getElementById('randomQuotesInterval').value = j.random_quotes.interval || 60;
            document.getElementById('randomQuotesStartHour').value = j.random_quotes.start_hour || 8;
            document.getElementById('randomQuotesEndHour').value = j.random_quotes.end_hour || 22;
        }
    });
}
function loadTimezone() {
    fetch('/status').then(r=>r.json()).then(j=>{
        if(j.timezone !== undefined) {
            document.getElementById('timezoneSelect').value = j.timezone;
        }
    });
}
setInterval(fetchStatus, 3000);
loadQuotes();
loadScheduledMessages();
loadRandomQuotesSchedule();
loadTimezone();
fetchStatus();

// === Logs functions ===
let lastLogSeq = 0;
async function loadLogs() {
    try {
        const resp = await fetch(`/api/logs?since=${lastLogSeq}&limit=100`);
        if (!resp.ok) { console.error('Logs API error'); return; }
        const data = await resp.json();
        if (!data.success) return;
        
        const logsDiv = document.getElementById('logsList');
        if (!data.logs || data.logs.length === 0) {
            logsDiv.textContent = '[Brak nowych logów]';
            return;
        }
        
        let text = '';
        for (const log of data.logs) {
            text += `${new Date(log.ms).toLocaleTimeString()} ${log.msg}\n`;
            lastLogSeq = Math.max(lastLogSeq, log.seq);
        }
        logsDiv.textContent = text;
        logsDiv.scrollTop = logsDiv.scrollHeight;
    } catch(e) {
        console.error('Load logs error:', e);
    }
}

async function saveLogsConfig() {
    const enabled = document.getElementById('logsEnabled').checked;
    try {
        const resp = await fetch('/save-logs-config', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: `logsEnabled=${enabled ? 'true' : 'false'}`
        });
        if (resp.ok) console.log('Logs config saved');
    } catch(e) {
        console.error('Save logs config error:', e);
    }
}

function downloadLogs() {
    const logsDiv = document.getElementById('logsList');
    const text = logsDiv.textContent;
    const blob = new Blob([text], {type: 'text/plain'});
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `logs_${new Date().toISOString().slice(0,19)}.txt`;
    a.click();
    URL.revokeObjectURL(url);
}

function clearLogsBuffer() {
    if (!confirm('Wyczyścić bufor logów?')) return;
    document.getElementById('logsList').textContent = '';
    lastLogSeq = 0;
}

// Load logs config on startup
fetch('/api/logs?since=0&limit=5').then(r=>r.json()).then(d=>{
    document.getElementById('logsEnabled').checked = d.enabled || false;
    if(d.latestSeq) lastLogSeq = d.latestSeq;
});
setInterval(loadLogs, 2000);

</script>
</body>
</html>
)rawliteral";

static bool parseHexColor(const JsonVariantConst& value, CRGB& outColor) {
    if (!value.is<const char*>()) return false;
    const char* raw = value.as<const char*>();
    if (!raw) return false;

    const char* hex = raw;
    if (hex[0] == '#') hex++;
    if (strlen(hex) != 6) return false;

    char* endPtr = nullptr;
    unsigned long rgb = strtoul(hex, &endPtr, 16);
    if (!endPtr || *endPtr != '\0') return false;

    outColor = CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

static bool parseHexColor(const char* raw, CRGB& outColor) {
    if (!raw) return false;

    const char* hex = raw;
    if (hex[0] == '#') hex++;
    if (strlen(hex) != 6) return false;

    char* endPtr = nullptr;
    unsigned long rgb = strtoul(hex, &endPtr, 16);
    if (!endPtr || *endPtr != '\0') return false;

    outColor = CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

// --- Funkcje pomocnicze do zamiany znaczników w HTML ---
String htmlReplace(const String& html) {
    String out = html;
    
    // WiFi settings - show current WiFi values + config
    out.replace("%WIFI_SSID%", WiFi.SSID().length() > 0 ? WiFi.SSID().c_str() : mainConfig.wifi.ssid);
    out.replace("%WIFI_PASS%", "");  // Never show password
    out.replace("%WIFI_STATIC%", !mainConfig.wifi.dhcp ? "checked" : "");
    out.replace("%WIFI_IP%", WiFi.localIP().toString());
    out.replace("%WIFI_GATEWAY%", WiFi.gatewayIP().toString());
    out.replace("%WIFI_DNS%", WiFi.dnsIP().toString());
    
    // MQTT settings
    out.replace("%MQTT_BROKER%", mainConfig.mqtt.broker);
    out.replace("%MQTT_PORT%", String(mainConfig.mqtt.port));
    out.replace("%MQTT_USER%", mainConfig.mqtt.user);
    out.replace("%MQTT_PASS%", mainConfig.mqtt.password);
    out.replace("%MQTT_CLIENTID%", mainConfig.mqtt.clientId);
    out.replace("%MQTT_PUB%", mainConfig.mqtt.topicPub);
    out.replace("%MQTT_SUB%", mainConfig.mqtt.topicSub);
    
    // Display settings
    out.replace("%DISP_BRIGHT%", String(mainConfig.display.brightness));
    out.replace("%DISP_AUTO%", mainConfig.display.autoBrightness ? "checked" : "");
    out.replace("%DISP_24H%", mainConfig.display.hour24 ? "checked" : "");
    out.replace("%DISP_SEC%", mainConfig.display.showSeconds ? "checked" : "");
    out.replace("%DISP_NTP%", mainConfig.display.ntpServer);
    return out;
}

// --- Funkcje do obsługi konfiguracji ---
bool loadConfig() {
    if (!LittleFS.begin()) {
        Serial.println("[LittleFS] Błąd montowania, próba formatowania...");
        LittleFS.format();
        if (!LittleFS.begin()) {
            Serial.println("[LittleFS] Montowanie nieudane po formacie!");
            return false;
        }
    }
    if (!LittleFS.exists(CONFIG_FILE)) return false;
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return false;
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;
    // WiFi
        strlcpy(mainConfig.wifi.ssid, doc["wifi"]["ssid"] | "", sizeof(mainConfig.wifi.ssid));
        strlcpy(mainConfig.wifi.password, doc["wifi"]["password"] | "", sizeof(mainConfig.wifi.password));
        mainConfig.wifi.dhcp = doc["wifi"]["dhcp"] | true;
        strlcpy(mainConfig.wifi.ip, doc["wifi"]["ip"] | "", sizeof(mainConfig.wifi.ip));
        strlcpy(mainConfig.wifi.gateway, doc["wifi"]["gateway"] | "", sizeof(mainConfig.wifi.gateway));
        strlcpy(mainConfig.wifi.dns, doc["wifi"]["dns"] | "", sizeof(mainConfig.wifi.dns));
    // MQTT
    strlcpy(mainConfig.mqtt.broker, doc["mqtt"]["broker"] | "", sizeof(mainConfig.mqtt.broker));
    mainConfig.mqtt.port = doc["mqtt"]["port"] | 1883;
    strlcpy(mainConfig.mqtt.user, doc["mqtt"]["user"] | "", sizeof(mainConfig.mqtt.user));
    strlcpy(mainConfig.mqtt.password, doc["mqtt"]["password"] | "", sizeof(mainConfig.mqtt.password));
    strlcpy(mainConfig.mqtt.clientId, doc["mqtt"]["clientId"] | "", sizeof(mainConfig.mqtt.clientId));
    strlcpy(mainConfig.mqtt.topicPub, doc["mqtt"]["topicPub"] | "", sizeof(mainConfig.mqtt.topicPub));
    strlcpy(mainConfig.mqtt.topicSub, doc["mqtt"]["topicSub"] | "", sizeof(mainConfig.mqtt.topicSub));
    // Display
    mainConfig.display.brightness = doc["display"]["brightness"] | 128;
    mainConfig.display.autoBrightness = doc["display"]["autoBrightness"] | false;
    mainConfig.display.hour24 = doc["display"]["hour24"] | true;
    mainConfig.display.showSeconds = doc["display"]["showSeconds"] | true;
    strlcpy(mainConfig.display.ntpServer, doc["display"]["ntpServer"] | "pool.ntp.org", sizeof(mainConfig.display.ntpServer));
    mainConfig.display.timezone = doc["display"]["timezone"] | 3600;
    // Schedule
    mainConfig.schedule.animation.enabled = doc["schedule"]["animation"]["enabled"] | false;
    mainConfig.schedule.animation.rotation_interval = doc["schedule"]["animation"]["rotation_interval"] | 5;
    mainConfig.schedule.animation.num_animations = doc["schedule"]["animation"]["num_animations"] | 0;
    for (uint8_t i = 0; i < mainConfig.schedule.animation.num_animations && i < 5; i++) {
        mainConfig.schedule.animation.animations[i].enabled = doc["schedule"]["animation"]["animations"][i]["enabled"] | false;
        mainConfig.schedule.animation.animations[i].type = doc["schedule"]["animation"]["animations"][i]["type"] | i;
    }
    mainConfig.schedule.num_messages = doc["schedule"]["num_messages"] | 0;
    for (uint8_t i = 0; i < mainConfig.schedule.num_messages && i < 10; i++) {
        mainConfig.schedule.messages[i].enabled = doc["schedule"]["messages"][i]["enabled"] | true;
        mainConfig.schedule.messages[i].hour = doc["schedule"]["messages"][i]["hour"] | 12;
        mainConfig.schedule.messages[i].minute = doc["schedule"]["messages"][i]["minute"] | 0;
        strlcpy(mainConfig.schedule.messages[i].text, doc["schedule"]["messages"][i]["text"] | "", sizeof(mainConfig.schedule.messages[i].text));
        mainConfig.schedule.messages[i].duration = doc["schedule"]["messages"][i]["duration"] | 30;
    }
    // Random quotes
    mainConfig.schedule.random_quotes_enabled = doc["schedule"]["random_quotes_enabled"] | false;
    mainConfig.schedule.random_quotes_interval = doc["schedule"]["random_quotes_interval"] | 60;
    mainConfig.schedule.random_quotes_start_hour = doc["schedule"]["random_quotes_start_hour"] | 8;
    mainConfig.schedule.random_quotes_end_hour = doc["schedule"]["random_quotes_end_hour"] | 22;
    return true;
}

bool saveConfig() {
    if (!LittleFS.begin()) return false;
    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) return false;
    StaticJsonDocument<1024> doc;
    // WiFi
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["ssid"] = mainConfig.wifi.ssid;
    wifi["password"] = mainConfig.wifi.password;
    wifi["dhcp"] = mainConfig.wifi.dhcp;
    wifi["ip"] = mainConfig.wifi.ip;
    wifi["gateway"] = mainConfig.wifi.gateway;
    wifi["dns"] = mainConfig.wifi.dns;
    // MQTT
    JsonObject mqtt = doc.createNestedObject("mqtt");
    mqtt["broker"] = mainConfig.mqtt.broker;
    mqtt["port"] = mainConfig.mqtt.port;
    mqtt["user"] = mainConfig.mqtt.user;
    mqtt["password"] = mainConfig.mqtt.password;
    mqtt["clientId"] = mainConfig.mqtt.clientId;
    mqtt["topicPub"] = mainConfig.mqtt.topicPub;
    mqtt["topicSub"] = mainConfig.mqtt.topicSub;
    // Display
    JsonObject display = doc.createNestedObject("display");
    display["brightness"] = mainConfig.display.brightness;
    display["autoBrightness"] = mainConfig.display.autoBrightness;
    display["hour24"] = mainConfig.display.hour24;
    display["showSeconds"] = mainConfig.display.showSeconds;
    display["ntpServer"] = mainConfig.display.ntpServer;
    display["timezone"] = mainConfig.display.timezone;
    // Schedule
    JsonObject schedule = doc.createNestedObject("schedule");
    JsonObject anim = schedule.createNestedObject("animation");
    anim["enabled"] = mainConfig.schedule.animation.enabled;
    anim["rotation_interval"] = mainConfig.schedule.animation.rotation_interval;
    anim["num_animations"] = mainConfig.schedule.animation.num_animations;
    JsonArray animations = anim.createNestedArray("animations");
    for (uint8_t i = 0; i < mainConfig.schedule.animation.num_animations && i < 5; i++) {
        JsonObject a = animations.createNestedObject();
        a["enabled"] = mainConfig.schedule.animation.animations[i].enabled;
        a["type"] = mainConfig.schedule.animation.animations[i].type;
    }
    schedule["num_messages"] = mainConfig.schedule.num_messages;
    JsonArray messages = schedule.createNestedArray("messages");
    for (uint8_t i = 0; i < mainConfig.schedule.num_messages && i < 10; i++) {
        JsonObject m = messages.createNestedObject();
        m["enabled"] = mainConfig.schedule.messages[i].enabled;
        m["hour"] = mainConfig.schedule.messages[i].hour;
        m["minute"] = mainConfig.schedule.messages[i].minute;
        m["text"] = mainConfig.schedule.messages[i].text;
        m["duration"] = mainConfig.schedule.messages[i].duration;
    }
    // Random quotes
    schedule["random_quotes_enabled"] = mainConfig.schedule.random_quotes_enabled;
    schedule["random_quotes_interval"] = mainConfig.schedule.random_quotes_interval;
    schedule["random_quotes_start_hour"] = mainConfig.schedule.random_quotes_start_hour;
    schedule["random_quotes_end_hour"] = mainConfig.schedule.random_quotes_end_hour;
    serializeJsonPretty(doc, f);
    f.close();
    return true;
}

void resetConfig() {
    LittleFS.remove(CONFIG_FILE);
}

void exportConfig() {
    if (!LittleFS.begin()) return;
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) {
        webServer.send(404, "text/plain", "Config not found");
        return;
    }
    webServer.streamFile(f, "application/json");
    f.close();
}

void importConfig() {
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        File f = LittleFS.open(CONFIG_FILE, "w");
        if (f) f.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        File f = LittleFS.open(CONFIG_FILE, "a");
        if (f) {
            f.write(upload.buf, upload.currentSize);
            f.close();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        webServer.send(200, "text/plain", "Config imported, restarting...");
        delay(1000);
        ESP.restart();
    }
}

// --- Funkcje statusu ---
String getStatusJson() {
    StaticJsonDocument<512> doc;
    doc["time"] = getCurrentTime();
    doc["ip"] = getIp();
    doc["rssi"] = getRssi();
    doc["mqtt"] = getMqttStatus();
    doc["uptime"] = getUptime();
    doc["heap"] = getFreeHeap();
    doc["fw"] = FIRMWARE_VERSION;
    doc["timezone"] = mainConfig.display.timezone;
    String out;
    serializeJson(doc, out);
    return out;
}

String getCurrentTime() {
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", 
        timeClient.getHours(), 
        timeClient.getMinutes(), 
        timeClient.getSeconds());
    return String(buf);
}

String getIp() {
    return WiFi.localIP().toString();
}

String getRssi() {
    return String(WiFi.RSSI());
}

String getMqttStatus() {
    // Zaimplementuj wg swojego kodu MQTT
    // Przykład:
    // return mqttClient.connected() ? "Połączony" : "Brak połączenia";
    return "Brak implementacji";
}

String getUptime() {
    unsigned long s = (millis() - bootMillis) / 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", s/3600, (s/60)%60, s%60);
    return String(buf);
}

String getFreeHeap() {
    return String(ESP.getFreeHeap());
}

// --- Funkcje serwisowe ---
void restartDevice() {
    webServer.send(200, "text/plain", "Restart...");
    delay(1000);
    ESP.restart();
}

void factoryReset() {
    resetConfig();
    webServer.send(200, "text/plain", "Factory reset, restarting...");
    delay(1000);
    ESP.restart();
}

void handleOtaUpdate() {
    // Prosty placeholder – pełna obsługa OTA wymaga dodatkowego kodu
    webServer.send(200, "text/plain", "OTA update not implemented in this example.");
}

// --- Ustawianie hosta ---
void webPanel_setHostname(const char* hostname) {
    WiFi.setHostname(hostname);
    MDNS.begin(hostname);
}

// --- Obsługa żądań HTTP ---
void webPanel_handle() {
    webServer.handleClient();
}

void webPanel_setup() {
    bootMillis = millis();
    if (!LittleFS.begin()) {
        Serial.println("[WebPanel] LittleFS init failed");
    }
    loadConfig();
    uint8_t startupBrightness = mainConfig.display.brightness;
    if (startupBrightness == 0) {
        startupBrightness = 80;
    }

    Preferences startupPrefs;
    if (startupPrefs.begin("wifi", true)) {
        String animBrightness = startupPrefs.getString("animBrightness", "");
        startupPrefs.end();
        if (animBrightness.length() > 0) {
            startupBrightness = constrain(animBrightness.toInt(), 1, 255);
        }
    }

    mainConfig.display.brightness = startupBrightness;
    display_setBrightness(startupBrightness);

    webPanel_setHostname("LedMatrixClock");

    webServer.on("/", HTTP_GET, []() {
        String html = htmlReplace(FPSTR(HTML_PAGE));
        webServer.send(200, "text/html", html);
    });

    webServer.on("/status", HTTP_GET, []() {
        webServer.send(200, "application/json", getStatusJson());
    });

    webServer.on("/save_wifi", HTTP_POST, []() {
        strlcpy(mainConfig.wifi.ssid, webServer.arg("ssid").c_str(), sizeof(mainConfig.wifi.ssid));
        strlcpy(mainConfig.wifi.password, webServer.arg("password").c_str(), sizeof(mainConfig.wifi.password));
        mainConfig.wifi.dhcp = !webServer.hasArg("static");
        strlcpy(mainConfig.wifi.ip, webServer.arg("ip").c_str(), sizeof(mainConfig.wifi.ip));
        strlcpy(mainConfig.wifi.gateway, webServer.arg("gateway").c_str(), sizeof(mainConfig.wifi.gateway));
        strlcpy(mainConfig.wifi.dns, webServer.arg("dns").c_str(), sizeof(mainConfig.wifi.dns));
        saveConfig();
        webServer.send(200, "text/plain", "WiFi config saved, restarting...");
        delay(1000);
        ESP.restart();
    });

    webServer.on("/save_mqtt", HTTP_POST, []() {
        strlcpy(mainConfig.mqtt.broker, webServer.arg("broker").c_str(), sizeof(mainConfig.mqtt.broker));
        mainConfig.mqtt.port = webServer.arg("port").toInt();
        strlcpy(mainConfig.mqtt.user, webServer.arg("user").c_str(), sizeof(mainConfig.mqtt.user));
        strlcpy(mainConfig.mqtt.password, webServer.arg("password").c_str(), sizeof(mainConfig.mqtt.password));
        strlcpy(mainConfig.mqtt.clientId, webServer.arg("clientId").c_str(), sizeof(mainConfig.mqtt.clientId));
        strlcpy(mainConfig.mqtt.topicPub, webServer.arg("topicPub").c_str(), sizeof(mainConfig.mqtt.topicPub));
        strlcpy(mainConfig.mqtt.topicSub, webServer.arg("topicSub").c_str(), sizeof(mainConfig.mqtt.topicSub));
        saveConfig();
        webServer.send(200, "text/plain", "MQTT config saved, restarting...");
        delay(1000);
        ESP.restart();
    });

    webServer.on("/save_display", HTTP_POST, []() {
        mainConfig.display.brightness = constrain(webServer.arg("brightness").toInt(), 0, 255);
        mainConfig.display.autoBrightness = webServer.hasArg("autoBrightness");
        mainConfig.display.hour24 = webServer.hasArg("hour24");
        mainConfig.display.showSeconds = webServer.hasArg("showSeconds");
        strlcpy(mainConfig.display.ntpServer, webServer.arg("ntpServer").c_str(), sizeof(mainConfig.display.ntpServer));
        display_setBrightness(mainConfig.display.brightness);
        saveConfig();
        webServer.send(200, "text/plain", "Display config saved, restarting...");
        delay(1000);
        ESP.restart();
    });

    webServer.on("/set_brightness", HTTP_POST, []() {
        int requested = webServer.arg("value").toInt();
        mainConfig.display.brightness = constrain(requested, 1, 255);
        display_setBrightness(mainConfig.display.brightness);
        webServer.send(200, "application/json", "{\"ok\":true}");
    });

    webServer.on("/save_time", HTTP_POST, []() {
        strlcpy(mainConfig.display.ntpServer, webServer.arg("ntpServer").c_str(), sizeof(mainConfig.display.ntpServer));
        mainConfig.display.timezone = webServer.arg("timezone").toInt();
        saveConfig();
        webServer.send(200, "text/plain", "Time config saved, restarting...");
        delay(1000);
        ESP.restart();
    });

    webServer.on("/restart", HTTP_POST, restartDevice);
    webServer.on("/factory_reset", HTTP_POST, factoryReset);

    webServer.on("/export", HTTP_GET, exportConfig);

    webServer.on("/import", HTTP_POST, []() {
        importConfig();
    });

    webServer.on("/ota", HTTP_POST, handleOtaUpdate);

    webServer.on("/test_mqtt", HTTP_POST, []() {
        webServer.send(200, "text/plain", "Test MQTT niezaimplementowany.");
    });

    // === API Logs endpoint ===
    webServer.on("/api/logs", HTTP_GET, []() {
        uint32_t sinceSeq = 0;
        uint16_t limit = 50;
        
        if (webServer.hasArg("since")) {
            sinceSeq = (uint32_t)atol(webServer.arg("since").c_str());
        }
        if (webServer.hasArg("limit")) {
            limit = (uint16_t)atoi(webServer.arg("limit").c_str());
        }
        
        String jsonResponse;
        app_logger_build_json(sinceSeq, limit, jsonResponse);
        webServer.send(200, "application/json", jsonResponse);
    });

    // === Logs control endpoints ===
    webServer.on("/logs_enabled", HTTP_POST, []() {
        bool enabled = (webServer.arg("enabled") == "true" || webServer.arg("enabled") == "1");
        app_logger_set_enabled(enabled);
        webServer.send(200, "application/json", "{\"ok\":true}");
    });

    webServer.on("/clear_logs", HTTP_POST, []() {
        app_logger_clear();
        webServer.send(200, "application/json", "{\"ok\":true}");
    });

    webServer.on("/download_logs", HTTP_GET, []() {
        uint32_t sinceSeq = 0;
        uint16_t limit = 5000;
        
        if (webServer.hasArg("since")) {
            sinceSeq = (uint32_t)atol(webServer.arg("since").c_str());
        }
        
        String csv = "seq,ms,message\n";
        
        // We'll do a quick export as CSV
        String jsonResponse;
        app_logger_build_json(sinceSeq, limit, jsonResponse);
        
        webServer.send(200, "text/csv", csv);
    });

    // === RGB/Animation control endpoint ===
    webServer.on("/control", HTTP_POST, []() {
        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, webServer.arg("plain"));
        
        if (!err) {
            if (doc.containsKey("display_mode")) {
                display_mode = doc["display_mode"];
            }
            if (doc.containsKey("animation_mode")) {
                animation_mode = doc["animation_mode"];
            }
            if (doc.containsKey("animation_speed")) {
                animation_speed = constrain(doc["animation_speed"], 1, 10);
                Preferences prefs;
                if (prefs.begin("wifi", false)) {
                    prefs.putUChar("animSpeed", animation_speed);
                    prefs.end();
                }
            }
            if (doc.containsKey("brightness") && !doc["brightness"].isNull()) {
                int requestedBrightness = (int)doc["brightness"];
                mainConfig.display.brightness = constrain(requestedBrightness, 1, 255);
                display_setBrightness(mainConfig.display.brightness);
            }
            CRGB parsedColor;
            if (doc.containsKey("clockColor") && doc["clockColor"].is<const char*>() && parseHexColor(doc["clockColor"].as<const char*>(), parsedColor)) {
                clock_color = parsedColor;
            }
            if (doc.containsKey("animColor") && doc["animColor"].is<const char*>() && parseHexColor(doc["animColor"].as<const char*>(), parsedColor)) {
                animation_color = parsedColor;
            }
            if (doc.containsKey("quoteColor") && doc["quoteColor"].is<const char*>() && parseHexColor(doc["quoteColor"].as<const char*>(), parsedColor)) {
                quote_color = parsedColor;
            }
            if (doc.containsKey("msgColor") && doc["msgColor"].is<const char*>() && parseHexColor(doc["msgColor"].as<const char*>(), parsedColor)) {
                message_color = parsedColor;
            }
            if (doc.containsKey("display_enabled")) {
                display_enabled = doc["display_enabled"];
            }
            if (doc.containsKey("message") && doc["message"].is<const char*>()) {
                const char* msg = doc["message"].as<const char*>();
                if (msg && msg[0] != '\0') {
                    strncpy(message_text, msg, sizeof(message_text)-1);
                    message_text[sizeof(message_text)-1] = '\0';
                    message_active = true;
                    message_offset = 64;
                    message_time_left = doc.containsKey("message_time") ? doc["message_time"] : 5000;
                    message_start_time = millis();
                }
            }
            webServer.send(200, "application/json", "{\"ok\":true}");
        } else {
            webServer.send(400, "application/json", "{\"ok\":false}");
        }
    });

    webServer.on("/control_form", HTTP_POST, []() {
        if (webServer.hasArg("display_mode")) {
            display_mode = constrain(webServer.arg("display_mode").toInt(), 0, 3);
        }
        if (webServer.hasArg("animation_mode")) {
            animation_mode = constrain(webServer.arg("animation_mode").toInt(), 0, 4);
            if (display_mode == DISPLAY_MODE_CLOCK) {
                display_mode = DISPLAY_MODE_ANIMATION;
            }
        }
        if (webServer.hasArg("animation_speed")) {
            animation_speed = constrain(webServer.arg("animation_speed").toInt(), 1, 10);
            Preferences prefs;
            if (prefs.begin("wifi", false)) {
                prefs.putUChar("animSpeed", animation_speed);
                prefs.end();
            }
            if (display_mode == DISPLAY_MODE_CLOCK) {
                display_mode = DISPLAY_MODE_ANIMATION;
            }
        }
        if (webServer.hasArg("brightness")) {
            mainConfig.display.brightness = constrain(webServer.arg("brightness").toInt(), 1, 255);
            display_setBrightness(mainConfig.display.brightness);
        }

        CRGB parsedColor;
        if (webServer.hasArg("clockColor") && parseHexColor(webServer.arg("clockColor").c_str(), parsedColor)) {
            clock_color = parsedColor;
        }
        if (webServer.hasArg("animColor") && parseHexColor(webServer.arg("animColor").c_str(), parsedColor)) {
            animation_color = parsedColor;
        }
        if (webServer.hasArg("quoteColor") && parseHexColor(webServer.arg("quoteColor").c_str(), parsedColor)) {
            quote_color = parsedColor;
        }
        if (webServer.hasArg("msgColor") && parseHexColor(webServer.arg("msgColor").c_str(), parsedColor)) {
            message_color = parsedColor;
        }

        if (webServer.hasArg("display_enabled")) {
            display_enabled = webServer.arg("display_enabled").toInt() != 0;
        }

        if (webServer.hasArg("message")) {
            String msg = webServer.arg("message");
            if (msg.length() > 0) {
                strncpy(message_text, msg.c_str(), sizeof(message_text) - 1);
                message_text[sizeof(message_text) - 1] = '\0';
                message_active = true;
                message_offset = LED_WIDTH;
                message_time_left = webServer.hasArg("message_time") ? webServer.arg("message_time").toInt() : 5000;
                message_start_time = millis();
            }
        }

        webServer.send(200, "application/json", "{\"ok\":true}");
    });

    webServer.on("/anim_test", HTTP_POST, []() {
        String cmd = webServer.arg("cmd");
        cmd.toLowerCase();

        if (cmd == "off") {
            display_enabled = false;
            display_clear();
            display_show();
            webServer.send(200, "application/json", "{\"ok\":true}");
            return;
        }

        display_enabled = true;
        message_active = false;

        if (cmd == "red") {
            globalColor = CRGB::Red;
            display_mode = DISPLAY_MODE_MESSAGE;
            display_clear();
            display_drawLamp();
            display_show();
            webServer.send(200, "application/json", "{\"ok\":true}");
            return;
        }
        if (cmd == "green") {
            globalColor = CRGB::Green;
            display_mode = DISPLAY_MODE_MESSAGE;
            display_clear();
            display_drawLamp();
            display_show();
            webServer.send(200, "application/json", "{\"ok\":true}");
            return;
        }
        if (cmd == "blue") {
            globalColor = CRGB::Blue;
            display_mode = DISPLAY_MODE_MESSAGE;
            display_clear();
            display_drawLamp();
            display_show();
            webServer.send(200, "application/json", "{\"ok\":true}");
            return;
        }

        display_mode = DISPLAY_MODE_ANIMATION;
        webServer.send(200, "application/json", "{\"ok\":true}");
    });

    // === Quotes API endpoint ===
    webServer.on("/quotes", HTTP_GET, []() {
        webServer.send(200, "application/json", quotes_getJson());
    });

    webServer.on("/quotes", HTTP_POST, []() {
        String quote = webServer.arg("quote");
        if (quote.length() > 0 && quotes_add(quote.c_str())) {
            webServer.send(200, "application/json", "{\"ok\":true}");
        } else {
            webServer.send(400, "application/json", "{\"ok\":false}");
        }
    });

    webServer.on("/quotes", HTTP_DELETE, []() {
        uint8_t index = webServer.arg("index").toInt();
        if (quotes_remove(index)) {
            webServer.send(200, "application/json", "{\"ok\":true}");
        } else {
            webServer.send(400, "application/json", "{\"ok\":false}");
        }
    });

    // === Schedule API endpoint ===
    webServer.on("/schedule", HTTP_GET, []() {
        StaticJsonDocument<1024> doc;
        doc["rotation_enabled"] = mainConfig.schedule.animation.enabled;
        doc["rotation_interval"] = mainConfig.schedule.animation.rotation_interval;
        
        JsonArray messages = doc.createNestedArray("messages");
        for (uint8_t i = 0; i < mainConfig.schedule.num_messages && i < 10; i++) {
            JsonObject msg = messages.createNestedObject();
            msg["hour"] = mainConfig.schedule.messages[i].hour;
            msg["minute"] = mainConfig.schedule.messages[i].minute;
            msg["text"] = mainConfig.schedule.messages[i].text;
            msg["duration"] = mainConfig.schedule.messages[i].duration;
        }
        
        // Random quotes
        JsonObject randomQuotes = doc.createNestedObject("random_quotes");
        randomQuotes["enabled"] = mainConfig.schedule.random_quotes_enabled;
        randomQuotes["interval"] = mainConfig.schedule.random_quotes_interval;
        randomQuotes["start_hour"] = mainConfig.schedule.random_quotes_start_hour;
        randomQuotes["end_hour"] = mainConfig.schedule.random_quotes_end_hour;
        
        String response;
        serializeJson(doc, response);
        webServer.send(200, "application/json", response);
    });

    webServer.on("/schedule", HTTP_POST, []() {
        StaticJsonDocument<1024> doc;
        DeserializationError err = deserializeJson(doc, webServer.arg("plain"));
        
        if (!err) {
            // Rotation animation
            if (doc.containsKey("schedule") && doc["schedule"].containsKey("animation")) {
                JsonObject anim = doc["schedule"]["animation"];
                mainConfig.schedule.animation.enabled = anim["enabled"];
                mainConfig.schedule.animation.rotation_interval = anim["rotation_interval"];
                mainConfig.schedule.animation.num_animations = anim["num_animations"];
                
                for (uint8_t i = 0; i < anim["num_animations"] && i < 5; i++) {
                    mainConfig.schedule.animation.animations[i].enabled = anim["animations"][i]["enabled"];
                    mainConfig.schedule.animation.animations[i].type = anim["animations"][i]["type"];
                }
                
                saveConfig();
                webServer.send(200, "application/json", "{\"ok\":true}");
                return;
            }
            
            // Add scheduled message
            if (doc.containsKey("add_message")) {
                if (mainConfig.schedule.num_messages >= 10) {
                    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"Max messages\"}");
                    return;
                }
                
                uint8_t idx = mainConfig.schedule.num_messages;
                mainConfig.schedule.messages[idx].enabled = true;
                mainConfig.schedule.messages[idx].hour = doc["add_message"]["hour"];
                mainConfig.schedule.messages[idx].minute = doc["add_message"]["minute"];
                strlcpy(mainConfig.schedule.messages[idx].text, 
                        doc["add_message"]["text"], 
                        sizeof(mainConfig.schedule.messages[idx].text)-1);
                mainConfig.schedule.messages[idx].duration = doc["add_message"]["duration"];
                mainConfig.schedule.num_messages++;
                
                saveConfig();
                webServer.send(200, "application/json", "{\"ok\":true}");
                return;
            }
            
            // Delete scheduled message
            if (doc.containsKey("delete_message")) {
                uint8_t idx = doc["delete_message"]["index"];
                if (idx >= mainConfig.schedule.num_messages) {
                    webServer.send(400, "application/json", "{\"ok\":false}");
                    return;
                }
                
                for (uint8_t i = idx; i < mainConfig.schedule.num_messages - 1; i++) {
                    mainConfig.schedule.messages[i] = mainConfig.schedule.messages[i+1];
                }
                mainConfig.schedule.num_messages--;
                
                saveConfig();
                webServer.send(200, "application/json", "{\"ok\":true}");
                return;
            }
            
            // Random quotes
            if (doc.containsKey("random_quotes")) {
                JsonObject rq = doc["random_quotes"];
                mainConfig.schedule.random_quotes_enabled = rq["enabled"];
                mainConfig.schedule.random_quotes_interval = rq["interval"];
                mainConfig.schedule.random_quotes_start_hour = rq["start_hour"];
                mainConfig.schedule.random_quotes_end_hour = rq["end_hour"];
                
                saveConfig();
                webServer.send(200, "application/json", "{\"ok\":true}");
                return;
            }
            
            webServer.send(400, "application/json", "{\"ok\":false}");
        } else {
            webServer.send(400, "application/json", "{\"ok\":false}");
        }
    });

    webServer.begin();
}

void webPanel_loop() {
    webPanel_handle();
}
