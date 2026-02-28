#ifndef CLOCK_H
#define CLOCK_H

#include <NTPClient.h>
#include <WiFiUdp.h>

extern NTPClient timeClient;
extern uint8_t currentHour, currentMinute, currentSecond;
extern bool colonState;

void clock_init();
void clock_update();
int32_t clock_normalizeTimezoneOffset(int32_t rawOffset);

#endif // CLOCK_H
