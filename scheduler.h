#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include "web_panel.h"
#include <ArduinoJson.h>

// Zmienne globalne dla scheduler'a
extern unsigned long last_animation_change;      // ostatnia zmiana animacji (ms)
extern unsigned long last_quote_check;           // ostatnia próba wylosowania cytatu (ms)
extern uint8_t current_animation_index;          // obecna animacja w rotacji (0-4)
extern char current_scheduled_message[128];      // tekst aktualnej zaplanowanej wiadomości

extern unsigned long last_scheduler_minute_check; // ostatnia minuta (do sprawdzania godzinowych zdarzeń)

// New: zmienne dla zaplanowanych animacji i cytatów
extern uint8_t scheduled_animation_type;         // typ animacji do wygranego zdarzenia
extern bool scheduled_animation_active;          // czy zaplanowana animacja powinna się wyświetlić
extern bool scheduled_quote_active;              // czy zaplanowany cytat powinien się wyświetlić

// Funkcje scheduler'a
void scheduler_init();
void scheduler_loop();  // Wywoływane z main display loop

// Helper functions
bool shouldRotateAnimation();
bool shouldShowScheduledMessage();
bool shouldShowRandomQuote();

#endif // SCHEDULER_H
