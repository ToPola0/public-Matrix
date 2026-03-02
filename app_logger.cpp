#include "app_logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const uint16_t kLogCapacity = 200;
static const uint16_t kLogMessageMaxLen = 180;

struct AppLogEntry {
    uint32_t seq;
    uint32_t ms;
    char msg[kLogMessageMaxLen + 1];
};

static AppLogEntry s_logBuffer[kLogCapacity];
static uint16_t s_start = 0;
static uint16_t s_count = 0;
static uint32_t s_nextSeq = 1;
static bool s_enabled = true;

static void appendEscapedJsonString(String& out, const char* text) {
    if (!text) return;
    for (const char* p = text; *p; ++p) {
        char c = *p;
        if (c == '\\' || c == '"') {
            out += '\\';
            out += c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            out += ' ';
        } else {
            out += c;
        }
    }
}

static void storeLogLine(const char* line) {
    if (!line || line[0] == '\0') return;

    uint16_t index = (uint16_t)((s_start + s_count) % kLogCapacity);
    if (s_count == kLogCapacity) {
        s_start = (uint16_t)((s_start + 1) % kLogCapacity);
        index = (uint16_t)((s_start + s_count - 1) % kLogCapacity);
    } else {
        s_count++;
    }

    AppLogEntry& entry = s_logBuffer[index];
    entry.seq = s_nextSeq++;
    entry.ms = millis();
    strncpy(entry.msg, line, kLogMessageMaxLen);
    entry.msg[kLogMessageMaxLen] = '\0';
}

void app_logger_set_enabled(bool enabled) {
    s_enabled = enabled;
}

bool app_logger_is_enabled() {
    return s_enabled;
}

void app_logger_clear() {
    s_start = 0;
    s_count = 0;
}

uint32_t app_logger_latest_seq() {
    return (s_nextSeq == 0) ? 0 : (s_nextSeq - 1);
}

void app_log(const String& message) {
    if (!s_enabled) return;
    storeLogLine(message.c_str());
}

void app_logf(const char* fmt, ...) {
    if (!s_enabled || !fmt) return;

    char buffer[kLogMessageMaxLen + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    storeLogLine(buffer);
}

void app_logger_build_json(uint32_t sinceSeq, uint16_t limit, String& outJson) {
    if (limit == 0) limit = 50;
    if (limit > kLogCapacity) limit = kLogCapacity;

    uint16_t matched = 0;
    for (uint16_t i = 0; i < s_count; i++) {
        uint16_t idx = (uint16_t)((s_start + i) % kLogCapacity);
        if (s_logBuffer[idx].seq > sinceSeq) {
            matched++;
        }
    }

    uint16_t skip = 0;
    if (matched > limit) {
        skip = (uint16_t)(matched - limit);
    }

    outJson = "{\"success\":true,\"enabled\":";
    outJson += (s_enabled ? "true" : "false");
    outJson += ",\"latestSeq\":";
    outJson += String(app_logger_latest_seq());
    outJson += ",\"logs\":[";

    bool first = true;
    for (uint16_t i = 0; i < s_count; i++) {
        uint16_t idx = (uint16_t)((s_start + i) % kLogCapacity);
        const AppLogEntry& entry = s_logBuffer[idx];
        if (entry.seq <= sinceSeq) continue;
        if (skip > 0) {
            skip--;
            continue;
        }

        if (!first) outJson += ',';
        first = false;

        outJson += "{\"seq\":";
        outJson += String(entry.seq);
        outJson += ",\"ms\":";
        outJson += String(entry.ms);
        outJson += ",\"msg\":\"";
        appendEscapedJsonString(outJson, entry.msg);
        outJson += "\"}";
    }

    outJson += "]}";
}
