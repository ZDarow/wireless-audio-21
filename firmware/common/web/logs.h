// logs.h — кольцевой буфер логов для Web UI (ТЗ_Веб §13).
// Header-only. Не зависит от Arduino-железа (host-совместимый).
//
// Формат записи (ТЗ §13.3):
//   [timestamp] [level] [module] message
// Категории (ТЗ §13.2): BOOT, WIFI, INTERNET, AUDIO, TRANSPORT, SATELLITE,
// CONFIG, ERROR. Уровень — число severity (0=DEBUG..3=ERROR).
//
// Ограничения (ТЗ §13.4): логи не хранят пароли и полные HTTP-ответы;
// буфер ограничен, старые записи вытесняются.
#pragma once

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace audio21 {

enum class LogCat : uint8_t {
    Boot = 0,
    Wifi = 1,
    Internet = 2,
    Audio = 3,
    Transport = 4,
    Satellite = 5,
    Config = 6,
    Error = 7,
};

inline const char* logCatName(LogCat c) {
    switch (c) {
        case LogCat::Boot: return "BOOT";
        case LogCat::Wifi: return "WIFI";
        case LogCat::Internet: return "INTERNET";
        case LogCat::Audio: return "AUDIO";
        case LogCat::Transport: return "TRANSPORT";
        case LogCat::Satellite: return "SATELLITE";
        case LogCat::Config: return "CONFIG";
        case LogCat::Error: return "ERROR";
    }
    return "?";
}

inline const char* logSevName(int severity) {
    switch (severity) {
        case 0: return "DEBUG";
        case 1: return "INFO";
        case 2: return "WARN";
        case 3: return "ERROR";
        default: return "INFO";
    }
}

// Кольцевой буфер строк логов. Фиксированное число слотов, каждый — строка
// длиной до (kLineSize-1). Потокобезопасность — на вызывающем.
class LogRing {
public:
    static constexpr int kLineSize = 192;

    // slots: массив char[slots][kLineSize]; slotsCount — число слотов.
    LogRing(char* slots, int slotsCount) : m_slots(slots), m_slotsCount(slotsCount) {
        for (int i = 0; i < m_slotsCount; i++) slot(i)[0] = '\0';
    }

    void add(LogCat cat, int severity, const char* msg) {
        char line[kLineSize];
        int n = snprintf(line, sizeof(line), "[%lu] [%s] [%s] %s",
                         (unsigned long)millis(), logSevName(severity), logCatName(cat), msg);
        if (n < 0) return;
        if (n >= kLineSize) n = kLineSize - 1;
        write(line, n);
    }

    void addf(LogCat cat, int severity, const char* fmt, ...) {
        char line[kLineSize];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(line, sizeof(line), fmt, args);
        va_end(args);
        if (n < 0) return;
        if (n >= kLineSize) n = kLineSize - 1;
        add(cat, severity, line);
    }

    // Последние `limit` строк (или все при limit<=0) в хронологическом
    // порядке. Возвращает количество записанных указателей.
    int tail(const char* out[], int limit) const {
        int count = m_count < m_slotsCount ? m_count : m_slotsCount;
        if (limit > 0 && count > limit) count = limit;
        int first = m_start;
        if (m_count >= m_slotsCount) {
            // Буфер переполнен: m_start указывает на самую старую запись.
        } else {
            first = 0;
        }
        for (int i = 0; i < count; i++) {
            int idx = (first + i) % m_slotsCount;
            out[i] = slot(idx);
        }
        return count;
    }

    int count() const { return m_count; }
    void clear() {
        for (int i = 0; i < m_slotsCount; i++) slot(i)[0] = '\0';
        m_start = 0;
        m_count = 0;
    }

private:
    char* slot(int i) const { return m_slots + (size_t)i * kLineSize; }

    void write(const char* line, int len) {
        int idx = (m_start + m_count) % m_slotsCount;
        char* dst = slot(idx);
        memcpy(dst, line, (size_t)len);
        dst[len] = '\0';
        if (m_count < m_slotsCount) {
            m_count++;
        } else {
            m_start = (m_start + 1) % m_slotsCount;  // вытеснили самую старую
        }
    }

    char* m_slots;
    int m_slotsCount;
    int m_start = 0;
    int m_count = 0;
};

} // namespace audio21
