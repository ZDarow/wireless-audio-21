// web_server.h — Web UI + REST API мастер-узла (ТЗ_Веб.md, полная реализация).
//
// Использует встроенный WebServer ESP32 Arduino (синхронный) + ArduinoJson.
// Два сервера (B9): STA-интерфейс (localIP:80) + AP-интерфейс (softAPIP:80),
// чтобы Web UI был доступен клиентам AP в APSTA-режиме.
//
// Разделы (ТЗ §4): Dashboard, Wi-Fi, Internet, Audio, Delays, Satellites,
// System, Update, Logs. REST API — ТЗ §15-17. Авторизация — §18/§23.
//
// Аудио-зависимости (pipeline, delay, espnow, статусы) — опциональные
// (указатели, дефолт nullptr): мастер ESP32-S3 (этап 1, без аудио-конвейера)
// использует тот же Web UI для настройки Wi-Fi, интернета и диагностики;
// аудио-эндпоинты при этом применяют изменения только к конфигу.
//
// Header-only. Только для мастер-узла.
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <time.h>
#include <vector>
#include <utility>

#include "node_config.h"
#include "pcm_pipeline.h"
#include "delay_line.h"
#include "espnow.h"
#include "storage.h"
#include "logs.h"
#include "internet_check.h"
#include "auth.h"
#include "wifi_store.h"
#include "web_spa.h"

namespace audio21 {

// Контекст аудио-конвейера для MasterWebServer (A1/T16: ISP — сгруппировать
// 9 отдельных указателей конструктора в один объект). Передаётся по ссылке;
// все поля опциональные (nullptr = аудио-конвейер не подключён, этап 1 S3).
struct AudioContext {
    PcmPipeline* pipeline = nullptr;
    DelayLine** delayLeft = nullptr;
    DelayLine** delayRight = nullptr;
    DelayLine** delaySub = nullptr;
    EspNowTransport* espnow = nullptr;
    volatile bool* leftOnline = nullptr;
    volatile bool* rightOnline = nullptr;
    volatile bool* a2dpConnected = nullptr;
};

// Хук проверки интернета для InternetChecker (ТЗ §7.4). Реализация — сырой
// HTTP/1.1 поверх WiFiClient: резолвим имя в IPv4 (WiFi.hostByName), шлём
// GET и читаем первую строку ответа (код 200/204 → online, 3xx → captive
// portal). HTTPClient core 3.x ошибочно возвращает connection refused при
// рабочем TCP-соединении (проверено на железе), поэтому обходим его.
HttpCheckResult httpInternetCheck(const char* url, uint32_t timeoutMs);

class MasterWebServer {
public:
    // Конструктор принимает AudioContext (A1/T16: ISP).
    explicit MasterWebServer(NodeConfig& cfg, const AudioContext& ctx)
        : m_cfg(cfg),
          m_pipeline(ctx.pipeline),
          m_delayLeft(ctx.delayLeft),
          m_delayRight(ctx.delayRight),
          m_delaySub(ctx.delaySub),
          m_espnow(ctx.espnow),
          m_leftOnline(ctx.leftOnline),
          m_rightOnline(ctx.rightOnline),
          m_a2dpConnected(ctx.a2dpConnected) {}

    // --- Инъекции из main.cpp ---
    void setInternetChecker(InternetChecker* ic);
    void setLogs(LogRing* logs);
    void setCpuLoadPercent(uint32_t p); // C5.5

    // Мьютекс DSP (REPO_AUDIT: гонка loop() ↔ web-задача). Обработчики
    // /api/volume|mute|crossover|delay вызывают setVolume/setMute/
    // setCrossoverHz/setDelayMs из задачи WebServer, пока loop() мастера
    // читает те же объекты в process(). Хук сериализует доступ: lockFn/unlockFn
    // передаются из main.cpp (там DspGuard поверх FreeRTOS mutex).
    using LockFn = void (*)();
    void setDspLockHooks(LockFn lockFn, LockFn unlockFn);

    // Запустить серверы (Wi-Fi должен быть подключён).
    void begin();

    void handleClient();

    // Кеш найденных Wi-Fi сетей (заполняется до старта AP, B9).
    struct WifiNetInfo {
        String ssid;
        int rssi;
        bool open;
    };
    void setWifiCache(std::vector<WifiNetInfo> cache);

    // Флаг «запрошено сохранение» — опрашивается в loop() main.cpp.
    bool saveRequested() const;
    void clearSaveRequested();

    // Флаг переподключения Wi-Fi (для main.cpp).
    bool reconnectRequested() const;
    void clearReconnectRequested();

    // S-1: требуется ли смена дефолтного пароля AP при первом запуске.
    bool passwordChangeRequired() const;

    // S-1: отправить 403 если требуется смена пароля (кроме /api/wifi/ap_password).
    bool sendForcedPasswordChange(WebServer& s);

    // R-1: эндпоинты не вызывают ESP.restart()/ConfigStorage::erase() из
    // библиотечного класса — они выставляют флаги, а main.cpp выполняет
    // действие в loop() (после send() клиент успевает получить ответ).
    bool rebootRequested() const;
    void clearRebootRequested();
    bool factoryResetRequested() const;
    void clearFactoryResetRequested();
    bool updateRebootRequested() const;
    void clearUpdateRebootRequested();

    // Статус авторизации (для main.cpp/консоли).
    bool sessionActive() const;

private:
    // ------------------------------------------------------------------
    // Роуты
    // ------------------------------------------------------------------
    void bindRoutes(WebServer& s);

    // ------------------------------------------------------------------
    // Авторизация (ТЗ §18, §23)
    // ------------------------------------------------------------------
    // C5.7: адрес клиента в подсети STA или AP (ТЗ_Веб §23.2).
    static bool ipInSubnet(IPAddress ip, IPAddress net, IPAddress mask);

    static bool clientIsLocal(WebServer& s);

    // Авторизован только обладатель cookie текущей сессии (m_sessionActive
    // — глобальный флаг «сессия создана», но не заменяет cookie).
    bool isAuthed(WebServer& s) const;

    bool csrfOk(WebServer& s) const;

    // RAII-страховка DSP-мьютекса для web-обработчиков (см. setDspLockHooks).
    class DspWebLock {
    public:
        explicit DspWebLock(MasterWebServer& ws) : m_ws(ws) { if (m_ws.m_dspLock) m_ws.m_dspLock(); }
        ~DspWebLock() { if (m_ws.m_dspUnlock) m_ws.m_dspUnlock(); }

    private:
        MasterWebServer& m_ws;
    };

    // ------------------------------------------------------------------
    // Ответы
    // ------------------------------------------------------------------
    static void sendJson(WebServer& s, int code, JsonDocument& doc);

    static void sendOk(WebServer& s, const char* msg = "ok");

    static void sendErr(WebServer& s, const char* msg = "err");

    // C5.9: 302 вместо HTML со script-редиректом (работает и с JS-off,
    // не требует CSP 'unsafe-inline').
    static void redirectRoot(WebServer& s);

    // ------------------------------------------------------------------
    // Страницы (SPA — один HTML, разделы через hash-навигацию)
    // ------------------------------------------------------------------
    void handleRoot(WebServer& s);

    // C5.1: session-cookie только с HttpOnly+SameSite=Lax (без SameSite
    // токен читается из любого iframe/скрипта — CSRF в обход X-CSRF-Token).
    static void sendSessionCookie(WebServer& s, const char* token);

    static void clearSessionCookie(WebServer& s);

    // ------------------------------------------------------------------
    // GET /api/status (ТЗ §16.1)
    // ------------------------------------------------------------------
    void handleStatus(WebServer& s);

    // ------------------------------------------------------------------
    // GET /api/wifi/status (ТЗ §15.1)
    // ------------------------------------------------------------------
    void handleWifiStatus(WebServer& s);

    // ------------------------------------------------------------------
    // GET /api/wifi/scan (ТЗ §15.2) — кеш до старта AP (B9) или живой скан.
    // ------------------------------------------------------------------
    void handleWifiScan(WebServer& s);

    // ------------------------------------------------------------------
    // POST /api/wifi/connect (ТЗ §15.3)
    // ------------------------------------------------------------------
    void handleWifiConnect(WebServer& s);

    // ------------------------------------------------------------------
    // POST /api/wifi/save (ТЗ §15.4) — сохранить профиль без подключения.
    // ------------------------------------------------------------------
    void handleWifiSave(WebServer& s);

    // ------------------------------------------------------------------
    // POST /api/wifi/forget (ТЗ §15.5)
    // ------------------------------------------------------------------
    void handleWifiForget(WebServer& s);

    // ------------------------------------------------------------------
    // GET /api/wifi/profiles (ТЗ §6.5) — список сохранённых сетей.
    // ------------------------------------------------------------------
    void handleWifiProfiles(WebServer& s);

    // ------------------------------------------------------------------
    // GET /api/net/internet (ТЗ §15.6)
    // ------------------------------------------------------------------
    void handleInternetStatus(WebServer& s);

    // ------------------------------------------------------------------
    // POST /api/net/check (ТЗ §15.7) — принудительная проверка.
    // ------------------------------------------------------------------
    void handleInternetCheck(WebServer& s);

    // ------------------------------------------------------------------
    // PUT /api/volume | POST /api/mute (ТЗ §16.2, §16.5)
    // ------------------------------------------------------------------
    void handleVolume(WebServer& s);

    void handleMute(WebServer& s);

    // ------------------------------------------------------------------
    // PUT /api/crossover (ТЗ §16.3)
    // ------------------------------------------------------------------
    void handleCrossover(WebServer& s);

    // ------------------------------------------------------------------
    // PUT /api/delay (ТЗ §16.4)
    // ------------------------------------------------------------------
    void handleDelay(WebServer& s);

    // ------------------------------------------------------------------
    // POST /api/transport, /api/pair
    // ------------------------------------------------------------------
    void handleTransport(WebServer& s);

    void handlePair(WebServer& s);

    // ------------------------------------------------------------------
    // POST /api/save, /api/system/* (ТЗ §17)
    // ------------------------------------------------------------------
    void handleSave(WebServer& s);

    // PUT /api/system/hostname (W-1: эндпоинт использовался SPA, но не был
    // зарегистрирован — 404 через onNotFound).
    void handleHostname(WebServer& s);

    // PUT /api/wifi/mode (W-1): смена режима Wi-Fi мастера + запрос сохранения.
    void handleWifiMode(WebServer& s);

    // POST /api/wifi/ap_password (S-1): смена заводского пароля AP мастера.
    void handleApPassword(WebServer& s);

    void handleReboot(WebServer& s);

    void handleFactoryReset(WebServer& s);

    void handleConfigExport(WebServer& s);

    static String macToStr(const MacAddr& mac);

    void handleConfigImport(WebServer& s);

    // ------------------------------------------------------------------
    // GET /api/logs (ТЗ §17.5) — фильтры level (0..3) и module (C5.4)
    // ------------------------------------------------------------------
    void handleLogs(WebServer& s);

    // "[ts] [LEVEL] [CAT] msg" — уровень после второго '['
    static int parseLogSeverity(const char* line);

    // "[ts] [LEVEL] [CAT] msg" — категория после третьего '['
    static bool logLineHasModule(const char* line, const String& module);

    // ------------------------------------------------------------------
    // GET /api/diagnostics (ТЗ §17.6)
    // ------------------------------------------------------------------
    void handleDiagnostics(WebServer& s);

    // ------------------------------------------------------------------
    // POST /api/login, /api/logout, /api/admin/setup (ТЗ §18)
    // ------------------------------------------------------------------
    void handleLogin(WebServer& s);

    void handleLogout(WebServer& s);

    void handleAdminSetup(WebServer& s);

    // ------------------------------------------------------------------
    // OTA (ТЗ §12) — POST /api/update
    // ------------------------------------------------------------------
    void handleUpdateUpload(WebServer& s);

    void handleUpdateEnd(WebServer& s);

    // ------------------------------------------------------------------
    // Члены
    // ------------------------------------------------------------------
    NodeConfig& m_cfg;
    PcmPipeline* m_pipeline;
    DelayLine** m_delayLeft;
    DelayLine** m_delayRight;
    DelayLine** m_delaySub;
    EspNowTransport* m_espnow;
    volatile bool* m_leftOnline;
    volatile bool* m_rightOnline;
    volatile bool* m_a2dpConnected;
    WebServer* m_server = nullptr;
    WebServer* m_apServer = nullptr;
    std::vector<WifiNetInfo> m_wifiCache;
    bool m_saveRequested = false;
    bool m_reconnectRequested = false;
    volatile bool m_rebootRequested = false;          // R-1: POST /api/system/reboot
    volatile bool m_factoryResetRequested = false;   // R-1: POST /api/system/factory_reset
    volatile bool m_updateRebootRequested = false;   // R-1/H-3: рестарт после успешного OTA

    InternetChecker* m_net = nullptr;
    LogRing* m_logs = nullptr;

    char m_sessionToken[Auth::kTokenLen + 1] = "";
    bool m_sessionActive = false;
    bool m_updateActive = false;
    uint32_t m_sessionStartMs = 0;   // C5.2: момент создания сессии
    uint32_t m_loginFails = 0;       // C5.3: счётчик неудачных логинов
    uint32_t m_loginLockUntilMs = 0; // C5.3: до этого времени логин заблокирован
    uint32_t m_lastScanMs = 0;       // C5.3: rate limit живого сканирования
    uint32_t m_cpuLoadPercent = 0;   // C5.5: заполняется из main.cpp
    LockFn m_dspLock = nullptr;      // DSP-мьютекс (см. setDspLockHooks)
    LockFn m_dspUnlock = nullptr;

    static constexpr uint32_t kSessionTimeoutMs = 3600 * 1000UL; // ТЗ §11.4/§23.1
    static constexpr uint32_t kMaxLoginFails = 5;                // C5.3
    static constexpr uint32_t kLoginLockMs = 60 * 1000UL;        // C5.3: 60 с
    static constexpr uint32_t kScanMinIntervalMs = 5000;         // C5.3: 5 с

    static const char* kPageHtml;
};

} // namespace audio21
