// web_server.h — HTTP сервер для Web UI и REST API (esp_http_server).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "esp_http_server.h"
#include "esp_err.h"

namespace audio21 {

// Клиент SSE для отслеживания активных соединений
struct SseClient {
    httpd_req_t* req;
    bool active;
};

// Коллбеки для интеграции с приложением
struct WebServerCallbacks {
    // Получить текущий статус (возвращает JSON строку в buf, размер bufSize)
    // Возвращает длину записи или отрицательное значение при ошибке.
    int (*getStatus)(char* buf, size_t bufSize);

    // Установить громкость (0..100)
    esp_err_t (*setVolume)(int volume);

    // Получить громкость
    int (*getVolume)(void);

    // Установить частоту кроссовера (Гц)
    esp_err_t (*setCrossoverHz)(int hz);

    // Получить частоту кроссовера
    int (*getCrossoverHz)(void);

    // Установить mute
    esp_err_t (*setMute)(bool mute);

    // Получить mute
    bool (*getMute)(void);

    // Установить задержку канала (мс): channel="left"/"right"/"sub"
    esp_err_t (*setDelay)(const char* channel, int delayMs);

    // Получить задержку канала
    int (*getDelay)(const char* channel);

    // Сканирование Wi-Fi сетей (JSON в buf)
    int (*wifiScan)(char* buf, size_t bufSize);

    // Установить режим Wi-Fi ("ap"/"sta"/"apsta")
    esp_err_t (*wifiSetMode)(const char* mode);

    // Сменить пароль точки доступа
    esp_err_t (*wifiSetApPassword)(const char* password);

    // Установить имя хоста
    esp_err_t (*setHostname)(const char* hostname);

    // Интернет-статус (GET) и принудительная проверка (POST)
    int (*netInternetStatus)(char* buf, size_t bufSize);
    esp_err_t (*netInternetCheck)(void);

    // Экспорт/импорт конфигурации
    int (*configExport)(char* buf, size_t bufSize);
    esp_err_t (*configImport)(const char* json);

    // Лог-буфер
    int (*logsGet)(char* buf, size_t bufSize);

    // Диагностика системы
    int (*diagnosticsGet)(char* buf, size_t bufSize);

    // Диагностика для агентов (мониторинг, тестирование)
    int (*agentHealth)(char* buf, size_t bufSize);      // простое состояние живости
    int (*agentDiagnostics)(char* buf, size_t bufSize); // детальная диагностика
    int (*agentPing)(char* buf, size_t bufSize);        // эхо-проверка связности

    // Перезагрузка устройства
    esp_err_t (*reboot)(void);

    // Сброс к заводским настройкам
    esp_err_t (*factoryReset)(void);

    // OTA обновление по URL (принимает URL прошивки)
    esp_err_t (*otaUpdate)(const char* url);

    // OTA обновление через загрузку файла (принимает бинарный образ прошивки в теле запроса)
    // signatureHex: Ed25519 подпись прошивки (hex, 128 символов) или NULL если подпись не требуется
    // Возвращает ESP_OK если обновление начато успешно
    esp_err_t (*otaUpdateFile)(const uint8_t* data, size_t size, const char* signatureHex);

    // Получить текущую конфигурацию Wi-Fi (JSON в buf, размер bufSize)
    int (*getWifiConfig)(char* buf, size_t bufSize);

    // Установить конфигурацию Wi-Fi (mode: "ap"/"sta"/"apsta")
    // staDhcp: "true" или "false"; staIp/staGateway/staNetmask/staDns — для статического IP
    esp_err_t (*setWifiConfig)(const char* mode,
                               const char* apSsid,
                               const char* apPassword,
                               const char* staSsid,
                               const char* staPassword,
                               const char* staDhcp,
                               const char* staIp,
                               const char* staGateway,
                               const char* staNetmask,
                               const char* staDns);

    // Получить CSRF токен для защиты state-changing запросов
    const char* (*getCsrfToken)(void);

    // Проверить CSRF токен из заголовка X-CSRF-Token
    bool (*validateCsrfToken)(const char* token);

    // Авторизация
    // login: принимает {"username":..., "password":...}, возвращает {"token":...}
    int (*login)(const char* username, const char* password, char* tokenOut, size_t tokenOutSize);
    // logout: инвалидирует сессию по токену
    esp_err_t (*logout)(const char* token);
    // admin setup: первичная настройка админа (установить пароль)
    esp_err_t (*adminSetup)(const char* username, const char* password);
};

class WebServer {
public:
    WebServer() = default;
    ~WebServer();

    // Инициализация HTTP сервера на порту 80
    esp_err_t init(uint16_t port = 80);

    // Деинициализация
    esp_err_t deinit();

    // Установить коллбеки
    void setCallbacks(const WebServerCallbacks* cb);

    // Отправить SSE событие всем подключённым клиентам
    // event — тип события (например, "satellite"), data — JSON данные
    esp_err_t sseBroadcast(const char* event, const char* data);

private:
    httpd_handle_t m_server = nullptr;
    const WebServerCallbacks* m_cb = nullptr;
    char m_csrfToken[33] = {0};
    std::vector<SseClient> m_sseClients; // Активные SSE соединения

    // Обработчики URI
    static esp_err_t onGetStatus(httpd_req_t* req);
    static esp_err_t onPostVolume(httpd_req_t* req);
    static esp_err_t onGetVolume(httpd_req_t* req);
    static esp_err_t onPostCrossover(httpd_req_t* req);
    static esp_err_t onGetCrossover(httpd_req_t* req);
    static esp_err_t onPostReboot(httpd_req_t* req);
    static esp_err_t onPostFactoryReset(httpd_req_t* req);
    static esp_err_t onPostOta(httpd_req_t* req);
    static esp_err_t onPostUpdate(httpd_req_t* req);
    static esp_err_t onGetWifi(httpd_req_t* req);
    static esp_err_t onPostWifi(httpd_req_t* req);
    static esp_err_t onGetCsrf(httpd_req_t* req);
    static esp_err_t onSSE(httpd_req_t* req);
    static esp_err_t onGetRoot(httpd_req_t* req);
    static esp_err_t onNotFound(httpd_req_t* req);
    static esp_err_t onPostMute(httpd_req_t* req);
    static esp_err_t onPutDelay(httpd_req_t* req);
    static esp_err_t onGetWifiScan(httpd_req_t* req);
    static esp_err_t onPutWifiMode(httpd_req_t* req);
    static esp_err_t onPostWifiApPassword(httpd_req_t* req);
    static esp_err_t onPutHostname(httpd_req_t* req);
    static esp_err_t onGetNetInternet(httpd_req_t* req);
    static esp_err_t onPostNetCheck(httpd_req_t* req);
    static esp_err_t onGetConfigExport(httpd_req_t* req);
    static esp_err_t onPostConfigImport(httpd_req_t* req);
    static esp_err_t onGetLogs(httpd_req_t* req);
    static esp_err_t onGetDiagnostics(httpd_req_t* req);
    static esp_err_t onGetAgentHealth(httpd_req_t* req);
    static esp_err_t onGetAgentDiagnostics(httpd_req_t* req);
    static esp_err_t onGetAgentPing(httpd_req_t* req);
    static esp_err_t onPostLogin(httpd_req_t* req);
    static esp_err_t onPostLogout(httpd_req_t* req);
    static esp_err_t onPostAdminSetup(httpd_req_t* req);
    static esp_err_t onGetCaptivePortal(httpd_req_t* req);
    static esp_err_t onPostCaptivePortal(httpd_req_t* req);

    // Проверка CSRF токена из заголовка X-CSRF-Token
    bool validateCsrfToken(httpd_req_t* req);

    // Вспомогательные функции
    static esp_err_t sendJson(httpd_req_t* req, const char* json);
    static esp_err_t sendText(httpd_req_t* req, const char* text, size_t len);
    static esp_err_t readBody(httpd_req_t* req, char* buf, size_t bufSize);
};

} // namespace audio21
