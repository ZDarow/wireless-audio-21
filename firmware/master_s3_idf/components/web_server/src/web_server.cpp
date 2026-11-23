// web_server.cpp — реализация HTTP сервера.
#include "web_server.h"
#include "esp_log.h"
#include "cJSON.h"

namespace audio21 {

static const char *TAG = "web_server";

// Список подключённых SSE клиентов
static httpd_handle_t s_sseServer = nullptr;

// Вспомогательная функция для SSE — хранится в WebServer
// В реальном реализации нужно отслеживать клиенты SSE отдельно.

WebServer::~WebServer()
{
    deinit();
}

esp_err_t WebServer::init(uint16_t port)
{
    if (m_server) {
        ESP_LOGW(TAG, "Web server already initialized");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 16;
    config.max_resp_headers = 16;
    config.backlog_conn = 5;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&m_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_sseServer = m_server;

    // Регистрация URI обработчиков
    httpd_uri_t uriGetStatus = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = onGetStatus,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetStatus);

    httpd_uri_t uriPostVolume = {
        .uri = "/api/volume",
        .method = HTTP_POST,
        .handler = onPostVolume,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostVolume);

    httpd_uri_t uriGetVolume = {
        .uri = "/api/volume",
        .method = HTTP_GET,
        .handler = onGetVolume,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetVolume);

    httpd_uri_t uriPostCrossover = {
        .uri = "/api/crossover",
        .method = HTTP_POST,
        .handler = onPostCrossover,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostCrossover);

    httpd_uri_t uriGetCrossover = {
        .uri = "/api/crossover",
        .method = HTTP_GET,
        .handler = onGetCrossover,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetCrossover);

    httpd_uri_t uriPostReboot = {
        .uri = "/api/system/reboot",
        .method = HTTP_POST,
        .handler = onPostReboot,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostReboot);

    httpd_uri_t uriPostFactoryReset = {
        .uri = "/api/system/factory_reset",
        .method = HTTP_POST,
        .handler = onPostFactoryReset,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostFactoryReset);

    httpd_uri_t uriPostOta = {
        .uri = "/api/system/ota",
        .method = HTTP_POST,
        .handler = onPostOta,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostOta);

    httpd_uri_t uriGetWifi = {
        .uri = "/api/wifi",
        .method = HTTP_GET,
        .handler = onGetWifi,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetWifi);

    httpd_uri_t uriPostWifi = {
        .uri = "/api/wifi",
        .method = HTTP_POST,
        .handler = onPostWifi,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostWifi);

    httpd_uri_t uriSSE = {
        .uri = "/api/events",
        .method = HTTP_GET,
        .handler = onSSE,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriSSE);

    httpd_uri_t uriRoot = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = onGetRoot,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriRoot);

    httpd_register_err_handler(m_server, HTTPD_404_NOT_FOUND, [](httpd_req_t* req, httpd_err_code_t err) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    });

    ESP_LOGI(TAG, "Web server started on port %u", port);
    return ESP_OK;
}

esp_err_t WebServer::deinit()
{
    if (m_server) {
        esp_err_t ret = httpd_stop(m_server);
        m_server = nullptr;
        s_sseServer = nullptr;
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_stop failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Web server stopped");
    }
    return ESP_OK;
}

void WebServer::setCallbacks(const WebServerCallbacks* cb)
{
    m_cb = cb;
}

esp_err_t WebServer::sseBroadcast(const char* event, const char* data)
{
    // В упрощённой реализации SSE broadcast не отслеживает клиентов.
    // Полноценная реализация требует списка активных SSE сессий.
    ESP_LOGD(TAG, "SSE broadcast: event=%s data=%s", event, data);
    return ESP_OK;
}

// --- Обработчики URI ---

esp_err_t WebServer::onGetStatus(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->getStatus) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[512];
    int len = self->m_cb->getStatus(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return self->sendJson(req, buf);
}

esp_err_t WebServer::onPostVolume(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->setVolume) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[64];
    esp_err_t ret = self->readBody(req, buf, sizeof(buf));
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    cJSON* json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    cJSON* volItem = cJSON_GetObjectItemCaseSensitive(json, "volume");
    if (!volItem || !cJSON_IsNumber(volItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    int volume = volItem->valueint;
    ret = self->m_cb->setVolume(volume);
    cJSON_Delete(json);

    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetVolume(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->getVolume) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int volume = self->m_cb->getVolume();
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"volume\":%d}", volume);
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onPostCrossover(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->setCrossoverHz) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[64];
    esp_err_t ret = self->readBody(req, buf, sizeof(buf));
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    cJSON* json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    cJSON* hzItem = cJSON_GetObjectItemCaseSensitive(json, "crossoverHz");
    if (!hzItem || !cJSON_IsNumber(hzItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    int hz = hzItem->valueint;
    ret = self->m_cb->setCrossoverHz(hz);
    cJSON_Delete(json);

    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetCrossover(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->getCrossoverHz) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int hz = self->m_cb->getCrossoverHz();
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"crossoverHz\":%d}", hz);
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onPostReboot(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->reboot) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Отправляем ответ перед перезагрузкой
    self->sendJson(req, "{\"ok\":true, \"rebooting\":true}");

    // Небольшая задержка для доставки ответа
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t ret = self->m_cb->reboot();
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t WebServer::onPostFactoryReset(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->factoryReset) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    self->sendJson(req, "{\"ok\":true, \"resetting\":true}");
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t ret = self->m_cb->factoryReset();
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t WebServer::onPostOta(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->otaUpdate) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[256];
    esp_err_t ret = self->readBody(req, buf, sizeof(buf));
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    cJSON* json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    cJSON* urlItem = cJSON_GetObjectItemCaseSensitive(json, "url");
    if (!urlItem || !cJSON_IsString(urlItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    const char* url = urlItem->valuestring;
    cJSON_Delete(json);

    // Отправляем ответ перед началом OTA
    self->sendJson(req, "{\"ok\":true, \"ota\":true}");
    vTaskDelay(pdMS_TO_TICKS(100));

    ret = self->m_cb->otaUpdate(url);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA callback failed: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

esp_err_t WebServer::onGetWifi(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->getWifiConfig) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[512];
    int len = self->m_cb->getWifiConfig(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return self->sendJson(req, buf);
}

esp_err_t WebServer::onPostWifi(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->setWifiConfig) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[512];
    esp_err_t ret = self->readBody(req, buf, sizeof(buf));
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    cJSON* json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }

    cJSON* modeItem = cJSON_GetObjectItemCaseSensitive(json, "mode");
    cJSON* apSsidItem = cJSON_GetObjectItemCaseSensitive(json, "apSsid");
    cJSON* apPwdItem = cJSON_GetObjectItemCaseSensitive(json, "apPassword");
    cJSON* staSsidItem = cJSON_GetObjectItemCaseSensitive(json, "staSsid");
    cJSON* staPwdItem = cJSON_GetObjectItemCaseSensitive(json, "staPassword");

    const char* mode = modeItem && cJSON_IsString(modeItem) ? modeItem->valuestring : "ap";
    const char* apSsid = apSsidItem && cJSON_IsString(apSsidItem) ? apSsidItem->valuestring : nullptr;
    const char* apPwd = apPwdItem && cJSON_IsString(apPwdItem) ? apPwdItem->valuestring : nullptr;
    const char* staSsid = staSsidItem && cJSON_IsString(staSsidItem) ? staSsidItem->valuestring : nullptr;
    const char* staPwd = staPwdItem && cJSON_IsString(staPwdItem) ? staPwdItem->valuestring : nullptr;

    cJSON_Delete(json);

    ret = self->m_cb->setWifiConfig(mode, apSsid, apPwd, staSsid, staPwd);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onSSE(httpd_req_t* req)
{
    // Упрощённая реализация SSE — отправляем заголовки и удерживаем соединение
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    const char* msg = "data: {\"type\":\"connected\"}\n\n";
    httpd_resp_send(req, msg, strlen(msg));

    // Удерживаем соединение 30 секунд
    vTaskDelay(pdMS_TO_TICKS(30000));
    return ESP_OK;
}

esp_err_t WebServer::onGetRoot(httpd_req_t* req)
{
    const char* html =
        "<!DOCTYPE html>"
        "<html><head><title>Wireless Audio 2.1 Master</title></head>"
        "<body><h1>Wireless Audio 2.1 Master (ESP-IDF)</h1>"
        "<p>API endpoints:</p>"
        "<ul>"
        "<li>GET /api/status — устройство</li>"
        "<li>GET/POST /api/volume — громкость</li>"
        "<li>GET/POST /api/crossover — кроссовер</li>"
        "<li>POST /api/system/reboot — перезагрузка</li>"
        "<li>POST /api/system/factory_reset — сброс настроек</li>"
        "<li>GET /api/events — SSE события</li>"
        "</ul></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

// --- Вспомогательные функции ---

esp_err_t WebServer::sendJson(httpd_req_t* req, const char* json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

esp_err_t WebServer::sendText(httpd_req_t* req, const char* text, size_t len)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, text, len);
}

esp_err_t WebServer::readBody(httpd_req_t* req, char* buf, size_t bufSize)
{
    size_t remaining = req->content_len;
    if (remaining >= bufSize) {
        return ESP_ERR_INVALID_SIZE;
    }

    int received = 0;
    while (remaining > 0) {
        int recvLen = httpd_req_recv(req, buf + received, remaining);
        if (recvLen <= 0) {
            return ESP_FAIL;
        }
        received += recvLen;
        remaining -= recvLen;
    }
    buf[received] = '\0';
    return ESP_OK;
}

} // namespace audio21
