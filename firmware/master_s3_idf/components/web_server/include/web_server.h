// web_server.h — HTTP сервер для Web UI и REST API (esp_http_server).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_http_server.h"
#include "esp_err.h"

namespace audio21 {

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

    // Перезагрузка устройства
    esp_err_t (*reboot)(void);

    // Сброс к заводским настройкам
    esp_err_t (*factoryReset)(void);

    // OTA обновление по URL (принимает URL прошивки)
    esp_err_t (*otaUpdate)(const char* url);
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

    // Обработчики URI
    static esp_err_t onGetStatus(httpd_req_t* req);
    static esp_err_t onPostVolume(httpd_req_t* req);
    static esp_err_t onGetVolume(httpd_req_t* req);
    static esp_err_t onPostCrossover(httpd_req_t* req);
    static esp_err_t onGetCrossover(httpd_req_t* req);
    static esp_err_t onPostReboot(httpd_req_t* req);
    static esp_err_t onPostFactoryReset(httpd_req_t* req);
    static esp_err_t onPostOta(httpd_req_t* req);
    static esp_err_t onSSE(httpd_req_t* req);
    static esp_err_t onGetRoot(httpd_req_t* req);
    static esp_err_t onNotFound(httpd_req_t* req);

    // Вспомогательные функции
    static esp_err_t sendJson(httpd_req_t* req, const char* json);
    static esp_err_t sendText(httpd_req_t* req, const char* text, size_t len);
    static esp_err_t readBody(httpd_req_t* req, char* buf, size_t bufSize);
};

} // namespace audio21
