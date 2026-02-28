#include "quotes.h"

char quotes[MAX_QUOTES][MAX_QUOTE_LENGTH];
uint8_t numQuotes = 0;

void quotes_normalizeForMatrix(const char* src, char* dst, size_t dstSize) {
    if (!src || !dst || dstSize == 0) return;

    size_t dstIndex = 0;
    for (size_t srcIndex = 0; src[srcIndex] != '\0' && dstIndex < dstSize - 1; ) {
        unsigned char byte = static_cast<unsigned char>(src[srcIndex]);

        if (byte >= 32 && byte <= 126) {
            dst[dstIndex++] = static_cast<char>(byte);
            srcIndex++;
            continue;
        }

        if (byte == 0xC4 || byte == 0xC5 || byte == 0xC3) {
            unsigned char second = static_cast<unsigned char>(src[srcIndex + 1]);
            char replacement = 0;

            if (byte == 0xC4 && second == 0x84) replacement = 'A';
            else if (byte == 0xC4 && second == 0x85) replacement = 'a';
            else if (byte == 0xC4 && second == 0x86) replacement = 'C';
            else if (byte == 0xC4 && second == 0x87) replacement = 'c';
            else if (byte == 0xC4 && second == 0x98) replacement = 'E';
            else if (byte == 0xC4 && second == 0x99) replacement = 'e';
            else if (byte == 0xC5 && second == 0x81) replacement = 'L';
            else if (byte == 0xC5 && second == 0x82) replacement = 'l';
            else if (byte == 0xC5 && second == 0x83) replacement = 'N';
            else if (byte == 0xC5 && second == 0x84) replacement = 'n';
            else if (byte == 0xC3 && second == 0x93) replacement = 'O';
            else if (byte == 0xC3 && second == 0xB3) replacement = 'o';
            else if (byte == 0xC5 && second == 0x9A) replacement = 'S';
            else if (byte == 0xC5 && second == 0x9B) replacement = 's';
            else if (byte == 0xC5 && second == 0xB9) replacement = 'Z';
            else if (byte == 0xC5 && second == 0xBA) replacement = 'z';
            else if (byte == 0xC5 && second == 0xBB) replacement = 'Z';
            else if (byte == 0xC5 && second == 0xBC) replacement = 'z';

            if (replacement != 0 && dstIndex < dstSize - 1) {
                dst[dstIndex++] = replacement;
            }

            srcIndex += (src[srcIndex + 1] != '\0') ? 2 : 1;
            continue;
        }

        if (byte == '\n' || byte == '\r' || byte == '\t') {
            dst[dstIndex++] = ' ';
        }

        srcIndex++;
    }

    dst[dstIndex] = '\0';
}

bool quotes_init() {
    if (!LittleFS.begin()) {
        Serial.println("[Quotes] LittleFS init failed, attempting format...");
        // Spróbuj sformatować system plików
        if (!LittleFS.format()) {
            Serial.println("[Quotes] Format failed, quotes disabled");
            return false;
        }
        Serial.println("[Quotes] Format successful, retrying...");
        // Spróbuj jeszcze raz
        if (!LittleFS.begin()) {
            Serial.println("[Quotes] LittleFS still failed after format");
            return false;
        }
    }
    
    if (!LittleFS.exists(QUOTES_FILE)) {
        // Tworzenie pliku z domyślnymi cytatami
        StaticJsonDocument<4096> doc;
        JsonArray arr = doc.createNestedArray("quotes");
        arr.add("Każdy dzień to nowa szansa");
        arr.add("Wiele rzeczy możemy osiągnąć");
        arr.add("Nigdy się nie poddawaj");
        arr.add("Motywacja to klucz do sukcesu");
        arr.add("Bądź sobą, reszta jest już zajęta");
        
        File f = LittleFS.open(QUOTES_FILE, "w");
        if (f) {
            serializeJson(doc, f);
            f.close();
            Serial.println("[Quotes] Default quotes created");
        }
    }
    
    return quotes_load();
}

bool quotes_load() {
    if (!LittleFS.exists(QUOTES_FILE)) {
        Serial.println("[Quotes] File not found");
        return false;
    }
    
    File f = LittleFS.open(QUOTES_FILE, "r");
    if (!f) return false;
    
    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    
    if (err) {
        Serial.printf("[Quotes] JSON parse error: %s\n", err.c_str());
        return false;
    }
    
    JsonArray arr = doc["quotes"];
    numQuotes = 0;
    
    for (JsonVariant v : arr) {
        if (numQuotes >= MAX_QUOTES) break;
        strlcpy(quotes[numQuotes], v.as<const char*>(), MAX_QUOTE_LENGTH);
        numQuotes++;
    }
    
    Serial.printf("[Quotes] Loaded %d quotes\n", numQuotes);
    return numQuotes > 0;
}

bool quotes_save() {
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.createNestedArray("quotes");
    
    for (uint8_t i = 0; i < numQuotes; i++) {
        arr.add(quotes[i]);
    }
    
    File f = LittleFS.open(QUOTES_FILE, "w");
    if (!f) return false;
    
    serializeJson(doc, f);
    f.close();
    
    Serial.println("[Quotes] Saved");
    return true;
}

bool quotes_add(const char* quote) {
    if (numQuotes >= MAX_QUOTES) return false;
    if (strlen(quote) > MAX_QUOTE_LENGTH - 1) return false;

    strlcpy(quotes[numQuotes], quote, MAX_QUOTE_LENGTH);
    numQuotes++;
    
    return quotes_save();
}

bool quotes_remove(uint8_t index) {
    if (index >= numQuotes) return false;
    
    for (uint8_t i = index; i < numQuotes - 1; i++) {
        strcpy(quotes[i], quotes[i + 1]);
    }
    numQuotes--;
    
    return quotes_save();
}

char* quotes_getRandom() {
    if (numQuotes == 0) return (char*)"Brak cytatów";
    
    uint8_t idx = random(0, numQuotes);
    return quotes[idx];
}

char* quotes_get(uint8_t index) {
    if (index >= numQuotes) return (char*)"";
    return quotes[index];
}

String quotes_getJson() {
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.createNestedArray("quotes");
    
    for (uint8_t i = 0; i < numQuotes; i++) {
        arr.add(quotes[i]);
    }
    
    String out;
    serializeJson(doc, out);
    return out;
}
