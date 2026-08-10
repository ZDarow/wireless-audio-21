// logger.h — лёгкий логгер поверх Arduino Serial.
// Header-only, не зависит от внешних библиотек.
#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace audio21 {

enum class LogLevel : uint8_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    None = 4,
};

class Logger {
public:
    static LogLevel level;

    static void setLevel(LogLevel lvl) { level = lvl; }

    template <typename... Args>
    static void log(LogLevel lvl, const char* tag, Args&&... args) {
        if (static_cast<uint8_t>(lvl) < static_cast<uint8_t>(level)) return;
        Serial.print('[');
        Serial.print(tag);
        Serial.print("] ");
        print(std::forward<Args>(args)...);
        Serial.println();
    }

    template <typename... Args>
    static void debug(const char* tag, Args&&... args) {
        log(LogLevel::Debug, tag, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static void info(const char* tag, Args&&... args) {
        log(LogLevel::Info, tag, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static void warn(const char* tag, Args&&... args) {
        log(LogLevel::Warn, tag, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static void error(const char* tag, Args&&... args) {
        log(LogLevel::Error, tag, std::forward<Args>(args)...);
    }

    // printf-вариант: форматирует fmt (vsnprintf) и печатает с префиксом [tag].
    static void logf(LogLevel lvl, const char* tag, const char* fmt, ...) {
        if (static_cast<uint8_t>(lvl) < static_cast<uint8_t>(level)) return;
        char buf[192];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        Serial.print('[');
        Serial.print(tag);
        Serial.print("] ");
        Serial.print(buf);
        Serial.println();
    }
    static void infof(const char* tag, const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        logfArgs(LogLevel::Info, tag, fmt, args);
        va_end(args);
    }
    static void warnf(const char* tag, const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        logfArgs(LogLevel::Warn, tag, fmt, args);
        va_end(args);
    }
    static void errorf(const char* tag, const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        logfArgs(LogLevel::Error, tag, fmt, args);
        va_end(args);
    }

private:
    static void logfArgs(LogLevel lvl, const char* tag, const char* fmt, va_list args) {
        if (static_cast<uint8_t>(lvl) < static_cast<uint8_t>(level)) return;
        char buf[192];
        vsnprintf(buf, sizeof(buf), fmt, args);
        Serial.print('[');
        Serial.print(tag);
        Serial.print("] ");
        Serial.print(buf);
        Serial.println();
    }

private:
    static void print() {}
    template <typename T, typename... Args>
    static void print(T&& first, Args&&... rest) {
        Serial.print(first);
        print(std::forward<Args>(rest)...);
    }
};

inline LogLevel Logger::level = LogLevel::Info;

} // namespace audio21