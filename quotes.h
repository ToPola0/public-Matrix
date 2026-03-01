#ifndef QUOTES_H
#define QUOTES_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define QUOTES_FILE "/quotes.json"
#define MAX_QUOTES 200
#define MAX_QUOTE_LENGTH 128
#define QUOTES_JSON_DOC_SIZE ((MAX_QUOTES * (MAX_QUOTE_LENGTH + 32)) + 4096)

extern char quotes[MAX_QUOTES][MAX_QUOTE_LENGTH];
extern uint8_t numQuotes;

bool quotes_init();
bool quotes_load();
bool quotes_save();
bool quotes_add(const char* quote);
bool quotes_remove(uint8_t index);
char* quotes_getRandom();
char* quotes_get(uint8_t index);
String quotes_getJson();
void quotes_normalizeForMatrix(const char* src, char* dst, size_t dstSize);

#endif // QUOTES_H
