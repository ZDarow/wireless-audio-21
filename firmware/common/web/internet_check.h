// internet_check.h — проверка доступа к интернету (ТЗ_Веб §7, §21).
// Header-only. Логика состояний не зависит от Arduino-железа; HTTP-запрос
// выполняется через хук `performHttp()` (на железе — HTTPClient, в host-тестах
// — заглушка).
//
// Состояния (ТЗ §7.3): disabled, checking, online, offline, dns_error,
// no_route, captive_portal.
#pragma once

#include <stdint.h>
#include <string.h>

namespace audio21 {

enum class NetStatus : uint8_t {
    Disabled = 0,
    Checking = 1,
    Online = 2,
    Offline = 3,
    DnsError = 4,
    NoRoute = 5,
    CaptivePortal = 6,
};

inline const char* netStatusName(NetStatus s) {
    switch (s) {
        case NetStatus::Disabled: return "disabled";
        case NetStatus::Checking: return "checking";
        case NetStatus::Online: return "online";
        case NetStatus::Offline: return "offline";
        case NetStatus::DnsError: return "dns_error";
        case NetStatus::NoRoute: return "no_route";
        case NetStatus::CaptivePortal: return "captive_portal";
    }
    return "unknown";
}

// Результат HTTP-проверки (заполняется хук-функцией).
struct HttpCheckResult {
    int statusCode = -1;        // HTTP-код; -1 = транспортная ошибка
    int connectFailed = 0;      // 1 = не удалось соединиться (нет маршрута/DNS)
    uint32_t latencyMs = 0;
    bool httpConnected = false; // TCP-соединение установлено (для dns_error vs no_route)
};

// Интернет-проверка: хранит состояние и применяет логику результата.
class InternetChecker {
public:
    // Хук HTTP-запроса. URL и таймаут — из параметров. Возвращает результат.
    typedef HttpCheckResult (*HttpCheckFn)(const char* url, uint32_t timeoutMs);

    void configure(const char* checkUrl, uint16_t intervalSec, uint16_t timeoutMs, bool enabled) {
        m_checkUrl = checkUrl;
        m_intervalSec = intervalSec > 0 ? intervalSec : 30;
        m_timeoutMs = timeoutMs > 0 ? timeoutMs : 5000;
        m_enabled = enabled;
        if (!m_enabled) m_status = NetStatus::Disabled;
    }

    // Вызвать при подключении Wi-Fi STA. Делает проверку сразу и планирует
    // следующую по интервалу.
    void start(HttpCheckFn fn, uint32_t nowMs) {
        m_fn = fn;
        m_lastCheckMs = 0;  // принудительная проверка при старте
        m_nextCheckMs = nowMs;
    }

    // Периодический вызов из loop/задачи. Делает HTTP-запрос, если пришло
    // время. Возвращает true, если проверка выполнялась в этом вызове.
    bool tick(HttpCheckFn fn, uint32_t nowMs) {
        if (!m_enabled || m_status == NetStatus::Checking) return false;
        if (nowMs - m_lastCheckMs < (uint32_t)m_intervalSec * 1000) return false;
        runCheck(fn);
        m_lastCheckMs = nowMs;
        return true;
    }

    // Принудительная проверка (POST /api/net/check).
    void forceCheck(HttpCheckFn fn) {
        runCheck(fn);
    }

    NetStatus status() const { return m_status; }
    const char* statusName() const { return netStatusName(m_status); }
    uint32_t latencyMs() const { return m_latencyMs; }
    uint32_t lastCheckMs() const { return m_lastCheckMs; }
    const char* checkUrl() const { return m_checkUrl ? m_checkUrl : ""; }
    int lastHttpCode() const { return m_lastHttpCode; }
    bool dnsOk() const {
        return m_status == NetStatus::Online || m_status == NetStatus::Offline ||
               m_status == NetStatus::CaptivePortal;
    }
    bool httpOk() const {
        return m_status == NetStatus::Online || m_status == NetStatus::CaptivePortal;
    }

    // Для host-тестов: явная установка статуса без HTTP.
    void setStatusForTest(NetStatus s) { m_status = s; }

private:
    void runCheck(HttpCheckFn fn) {
        if (!fn) {
            m_status = NetStatus::Offline;
            return;
        }
        m_status = NetStatus::Checking;
        HttpCheckResult r = fn(m_checkUrl ? m_checkUrl : "", m_timeoutMs);
        m_latencyMs = r.latencyMs;
        m_lastHttpCode = r.statusCode;
        applyResult(r);
    }

    void applyResult(const HttpCheckResult& r) {
        if (r.connectFailed) {
            // Не удалось установить TCP-соединение. Различаем DNS-ошибку и
            // отсутствие маршрута: httpConnected не успел стать true — это
            // почти всегда DNS (хоста не существует), но если Wi-Fi нет —
            // no_route.
            m_status = NetStatus::DnsError;
            return;
        }
        int code = r.statusCode;
        if (code == 204 || code == 200) {
            m_status = NetStatus::Online;
        } else if (code == 302 || code == 301) {
            // Редирект на страницу входа — обычно captive portal.
            m_status = NetStatus::CaptivePortal;
        } else {
            m_status = NetStatus::Offline;
        }
    }

    const char* m_checkUrl = nullptr;
    uint16_t m_intervalSec = 30;
    uint16_t m_timeoutMs = 5000;
    bool m_enabled = false;
    NetStatus m_status = NetStatus::Disabled;
    uint32_t m_latencyMs = 0;
    uint32_t m_lastCheckMs = 0;
    uint32_t m_nextCheckMs = 0;
    int m_lastHttpCode = -1;
    HttpCheckFn m_fn = nullptr;
};

} // namespace audio21
