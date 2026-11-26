// web_server.cpp — реализация HTTP сервера.
#include "web_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <inttypes.h>
#include "cJSON.h"

namespace audio21 {

static const char *TAG = "web_server";

// Глобальный CSRF токен для доступа из main.cpp
static char g_csrf_token[33] = {0};

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
    config.max_uri_handlers = 48;
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

    httpd_uri_t uriPostUpdate = {
        .uri = "/api/update",
        .method = HTTP_POST,
        .handler = onPostUpdate,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostUpdate);

    httpd_uri_t uriGetWifi = {
        .uri = "/api/wifi",
        .method = HTTP_GET,
        .handler = onGetWifi,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetWifi);

    httpd_uri_t uriGetCsrf = {
        .uri = "/api/csrf",
        .method = HTTP_GET,
        .handler = onGetCsrf,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetCsrf);

    httpd_uri_t uriPostWifi = {
        .uri = "/api/wifi",
        .method = HTTP_POST,
        .handler = onPostWifi,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostWifi);

    httpd_uri_t uriPutMute = {
        .uri = "/api/mute",
        .method = HTTP_PUT,
        .handler = onPostMute,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPutMute);

    httpd_uri_t uriPutTone = {
        .uri = "/api/tone",
        .method = HTTP_PUT,
        .handler = onPostTone,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPutTone);

    httpd_uri_t uriGetTone = {
        .uri = "/api/tone",
        .method = HTTP_GET,
        .handler = onGetTone,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetTone);

    httpd_uri_t uriPutChannelVolumes = {
        .uri = "/api/channel-volumes",
        .method = HTTP_PUT,
        .handler = onPostChannelVolumes,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPutChannelVolumes);

    httpd_uri_t uriGetChannelVolumes = {
        .uri = "/api/channel-volumes",
        .method = HTTP_GET,
        .handler = onGetChannelVolumes,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetChannelVolumes);

    httpd_uri_t uriPutDelay = {
        .uri = "/api/delay",
        .method = HTTP_PUT,
        .handler = onPutDelay,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPutDelay);

    httpd_uri_t uriGetWifiScan = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = onGetWifiScan,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetWifiScan);

    httpd_uri_t uriPutWifiMode = {
        .uri = "/api/wifi/mode",
        .method = HTTP_PUT,
        .handler = onPutWifiMode,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPutWifiMode);

    httpd_uri_t uriPostWifiApPassword = {
        .uri = "/api/wifi/ap_password",
        .method = HTTP_POST,
        .handler = onPostWifiApPassword,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostWifiApPassword);

    httpd_uri_t uriPutHostname = {
        .uri = "/api/system/hostname",
        .method = HTTP_PUT,
        .handler = onPutHostname,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPutHostname);

    httpd_uri_t uriGetNetInternet = {
        .uri = "/api/net/internet",
        .method = HTTP_GET,
        .handler = onGetNetInternet,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetNetInternet);

    httpd_uri_t uriPostNetCheck = {
        .uri = "/api/net/check",
        .method = HTTP_POST,
        .handler = onPostNetCheck,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostNetCheck);

    httpd_uri_t uriGetConfigExport = {
        .uri = "/api/config/export",
        .method = HTTP_GET,
        .handler = onGetConfigExport,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetConfigExport);

    httpd_uri_t uriPostConfigImport = {
        .uri = "/api/config/import",
        .method = HTTP_POST,
        .handler = onPostConfigImport,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostConfigImport);

    httpd_uri_t uriGetLogs = {
        .uri = "/api/logs",
        .method = HTTP_GET,
        .handler = onGetLogs,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetLogs);

    httpd_uri_t uriGetDiagnostics = {
        .uri = "/api/diagnostics",
        .method = HTTP_GET,
        .handler = onGetDiagnostics,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetDiagnostics);

    httpd_uri_t uriGetAgentHealth = {
        .uri = "/api/agent/health",
        .method = HTTP_GET,
        .handler = onGetAgentHealth,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetAgentHealth);

    httpd_uri_t uriGetAgentDiagnostics = {
        .uri = "/api/agent/diagnostics",
        .method = HTTP_GET,
        .handler = onGetAgentDiagnostics,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetAgentDiagnostics);

    httpd_uri_t uriGetAgentPing = {
        .uri = "/api/agent/ping",
        .method = HTTP_GET,
        .handler = onGetAgentPing,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetAgentPing);

    // --- Авторизация ---
    httpd_uri_t uriPostLogin = {
        .uri = "/api/login",
        .method = HTTP_POST,
        .handler = onPostLogin,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostLogin);

    httpd_uri_t uriPostLogout = {
        .uri = "/api/logout",
        .method = HTTP_POST,
        .handler = onPostLogout,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostLogout);

    httpd_uri_t uriPostAdminSetup = {
        .uri = "/api/admin/setup",
        .method = HTTP_POST,
        .handler = onPostAdminSetup,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostAdminSetup);

    // --- Captive портал (настройка Wi-Fi через softAP) ---
    httpd_uri_t uriGetCaptivePortal = {
        .uri = "/captive",
        .method = HTTP_GET,
        .handler = onGetCaptivePortal,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriGetCaptivePortal);

    httpd_uri_t uriPostCaptivePortal = {
        .uri = "/captive",
        .method = HTTP_POST,
        .handler = onPostCaptivePortal,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriPostCaptivePortal);

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

    httpd_uri_t uriFavicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = onGetFavicon,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server, &uriFavicon);

    httpd_register_err_handler(m_server, HTTPD_404_NOT_FOUND, [](httpd_req_t* req, httpd_err_code_t err) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    });

    ESP_LOGI(TAG, "Web server started on port %u", port);

    // Генерация CSRF токена
    uint32_t random_val;
    esp_fill_random(&random_val, sizeof(random_val));
    snprintf(m_csrfToken, sizeof(m_csrfToken), "%08" PRIX32 "%08" PRIX32, random_val, random_val >> 16);
    snprintf(g_csrf_token, sizeof(g_csrf_token), "%s", m_csrfToken);

    return ESP_OK;
}

esp_err_t WebServer::initHttps(uint16_t port)
{
    if (m_httpsServer) {
        ESP_LOGW(TAG, "HTTPS server already running");
        return ESP_OK;
    }

    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.port_secure = port;
    conf.port_insecure = 0; // только HTTPS, без HTTP на этом сервере

    extern const unsigned char servercert_pem_start[] asm("_binary_servercert_pem_start");
    extern const unsigned char servercert_pem_end[]   asm("_binary_servercert_pem_end");
    conf.servercert = servercert_pem_start;
    conf.servercert_len = servercert_pem_end - servercert_pem_start;

    extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
    extern const unsigned char prvtkey_pem_end[]   asm("_binary_prvtkey_pem_end");
    conf.prvtkey_pem = prvtkey_pem_start;
    conf.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;

    esp_err_t ret = httpd_ssl_start(&m_httpsServer, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS server start failed: %s", esp_err_to_name(ret));
        m_httpsServer = nullptr;
        return ret;
    }

    // Регистрируем те же URI handlers на HTTPS сервере
    // (копируем из HTTP сервера — те же функции)
    // Примечание: httpd_register_uri_handler требует httpd_handle_t
    // Для простоты регистрируем только основные
    httpd_uri_t uriRoot = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = onGetRoot,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_httpsServer, &uriRoot);

    ESP_LOGI(TAG, "HTTPS server started on port %u", port);
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
    if (m_sseClients.empty()) {
        return ESP_OK;
    }
    // Формируем SSE сообщение
    char msg[256];
    int len = snprintf(msg, sizeof(msg), "event: %s\ndata: %s\n\n", event, data);
    if (len < 0 || len >= (int)sizeof(msg)) {
        return ESP_ERR_NO_MEM;
    }

    // Отправляем всем активным клиентам
    esp_err_t ret = ESP_OK;
    for (auto& client : m_sseClients) {
        if (!client.active || !client.req) continue;
        esp_err_t r = httpd_resp_send_chunk(client.req, msg, len);
        if (r != ESP_OK) {
            client.active = false;
            ret = r;
        }
    }
    return ret;
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

    // Проверка CSRF токена
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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

    // Проверка CSRF токена
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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

    // Проверка CSRF токена
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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

    // Проверка CSRF токена
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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

    // Проверка CSRF токена
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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

esp_err_t WebServer::onPostUpdate(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->otaUpdateFile) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Проверка CSRF токена
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
        return ESP_FAIL;
    }

    ssize_t contentLen = req->content_len;
    if (contentLen <= 0 || contentLen > (1024 * 1024 * 8)) { // max 8MB
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    uint8_t* data = (uint8_t*)malloc(contentLen);
    if (!data) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t remaining = (size_t)contentLen;
    size_t received = 0;
    while (remaining > 0) {
        int recvLen = httpd_req_recv(req, (char*)(data + received), remaining);
        if (recvLen <= 0) {
            free(data);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
            return ESP_FAIL;
        }
        received += recvLen;
        remaining -= recvLen;
    }

    // Читаем подпись из заголовка X-OTA-Signature
    char signatureHex[129] = {0};
    esp_err_t sigRet = httpd_req_get_hdr_value_str(req, "X-OTA-Signature", signatureHex, sizeof(signatureHex));
    const char* signaturePtr = (sigRet == ESP_OK && signatureHex[0] != '\0') ? signatureHex : nullptr;

    esp_err_t ret = self->m_cb->otaUpdateFile(data, received, signaturePtr);
    free(data);

    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA update failed");
        return ESP_FAIL;
    }

    self->sendJson(req, "{\"ok\":true, \"update\":true}");
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

    // Проверка CSRF токена
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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
    cJSON* staDhcpItem = cJSON_GetObjectItemCaseSensitive(json, "staDhcp");
    cJSON* staIpItem = cJSON_GetObjectItemCaseSensitive(json, "staIp");
    cJSON* staGatewayItem = cJSON_GetObjectItemCaseSensitive(json, "staGateway");
    cJSON* staNetmaskItem = cJSON_GetObjectItemCaseSensitive(json, "staNetmask");
    cJSON* staDnsItem = cJSON_GetObjectItemCaseSensitive(json, "staDns");

    const char* mode = modeItem && cJSON_IsString(modeItem) ? modeItem->valuestring : "ap";
    const char* apSsid = apSsidItem && cJSON_IsString(apSsidItem) ? apSsidItem->valuestring : nullptr;
    const char* apPwd = apPwdItem && cJSON_IsString(apPwdItem) ? apPwdItem->valuestring : nullptr;
    const char* staSsid = staSsidItem && cJSON_IsString(staSsidItem) ? staSsidItem->valuestring : nullptr;
    const char* staPwd = staPwdItem && cJSON_IsString(staPwdItem) ? staPwdItem->valuestring : nullptr;
    const char* staDhcp = staDhcpItem && cJSON_IsString(staDhcpItem) ? staDhcpItem->valuestring : nullptr;
    const char* staIp = staIpItem && cJSON_IsString(staIpItem) ? staIpItem->valuestring : nullptr;
    const char* staGateway = staGatewayItem && cJSON_IsString(staGatewayItem) ? staGatewayItem->valuestring : nullptr;
    const char* staNetmask = staNetmaskItem && cJSON_IsString(staNetmaskItem) ? staNetmaskItem->valuestring : nullptr;
    const char* staDns = staDnsItem && cJSON_IsString(staDnsItem) ? staDnsItem->valuestring : nullptr;

    cJSON_Delete(json);

    ret = self->m_cb->setWifiConfig(mode, apSsid, apPwd, staSsid, staPwd,
                                    staDhcp, staIp, staGateway, staNetmask, staDns);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onPostMute(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->setMute) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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
    cJSON* muteItem = cJSON_GetObjectItemCaseSensitive(json, "mute");
    if (!muteItem || !cJSON_IsBool(muteItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    bool mute = cJSON_IsTrue(muteItem);
    cJSON_Delete(json);
    ret = self->m_cb->setMute(mute);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onPostTone(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->setTone) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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
    cJSON* bassItem = cJSON_GetObjectItemCaseSensitive(json, "bass");
    cJSON* trebleItem = cJSON_GetObjectItemCaseSensitive(json, "treble");
    if (!bassItem || !cJSON_IsNumber(bassItem) || !trebleItem || !cJSON_IsNumber(trebleItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    float bass = (float)bassItem->valuedouble;
    float treble = (float)trebleItem->valuedouble;
    cJSON_Delete(json);
    ret = self->m_cb->setTone(bass, treble);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetTone(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->getTone) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    float bass = 0, treble = 0;
    self->m_cb->getTone(&bass, &treble);
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"bass\":%.1f,\"treble\":%.1f}", bass, treble);
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onPostChannelVolumes(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->setChannelVolumes) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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
    cJSON* leftItem = cJSON_GetObjectItemCaseSensitive(json, "left");
    cJSON* rightItem = cJSON_GetObjectItemCaseSensitive(json, "right");
    cJSON* subItem = cJSON_GetObjectItemCaseSensitive(json, "sub");
    if (!leftItem || !cJSON_IsNumber(leftItem) || !rightItem || !cJSON_IsNumber(rightItem) ||
        !subItem || !cJSON_IsNumber(subItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    int left = leftItem->valueint;
    int right = rightItem->valueint;
    int sub = subItem->valueint;
    cJSON_Delete(json);
    ret = self->m_cb->setChannelVolumes(left, right, sub);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetChannelVolumes(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->getChannelVolumes) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int left = 100, right = 100, sub = 100;
    self->m_cb->getChannelVolumes(&left, &right, &sub);
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"left\":%d,\"right\":%d,\"sub\":%d}", left, right, sub);
    return self->sendJson(req, buf);
}
esp_err_t WebServer::onPutDelay(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->setDelay) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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
    cJSON* chItem = cJSON_GetObjectItemCaseSensitive(json, "channel");
    cJSON* msItem = cJSON_GetObjectItemCaseSensitive(json, "delay_ms");
    if (!chItem || !cJSON_IsString(chItem) || !msItem || !cJSON_IsNumber(msItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    const char* channel = chItem->valuestring;
    int delayMs = msItem->valueint;
    cJSON_Delete(json);
    ret = self->m_cb->setDelay(channel, delayMs);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetWifiScan(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->wifiScan) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char buf[1024];
    int len = self->m_cb->wifiScan(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onPutWifiMode(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->wifiSetMode) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
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
    cJSON* modeItem = cJSON_GetObjectItemCaseSensitive(json, "mode");
    if (!modeItem || !cJSON_IsString(modeItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    const char* mode = modeItem->valuestring;
    cJSON_Delete(json);
    ret = self->m_cb->wifiSetMode(mode);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onPostWifiApPassword(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->wifiSetApPassword) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
        return ESP_FAIL;
    }
    char buf[128];
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
    cJSON* pwdItem = cJSON_GetObjectItemCaseSensitive(json, "password");
    if (!pwdItem || !cJSON_IsString(pwdItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    const char* password = pwdItem->valuestring;
    cJSON_Delete(json);
    ret = self->m_cb->wifiSetApPassword(password);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onPutHostname(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->setHostname) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
        return ESP_FAIL;
    }
    char buf[128];
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
    cJSON* hostItem = cJSON_GetObjectItemCaseSensitive(json, "hostname");
    if (!hostItem || !cJSON_IsString(hostItem)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    const char* hostname = hostItem->valuestring;
    cJSON_Delete(json);
    ret = self->m_cb->setHostname(hostname);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetNetInternet(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->netInternetStatus) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char buf[256];
    int len = self->m_cb->netInternetStatus(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onPostNetCheck(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->netInternetCheck) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
        return ESP_FAIL;
    }
    esp_err_t ret = self->m_cb->netInternetCheck();
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetConfigExport(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->configExport) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char buf[2048];
    int len = self->m_cb->configExport(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onPostConfigImport(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->configImport) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (!self->validateCsrfToken(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
        return ESP_FAIL;
    }
    char buf[2048];
    esp_err_t ret = self->readBody(req, buf, sizeof(buf));
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    ret = self->m_cb->configImport(buf);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetLogs(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->logsGet) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char buf[2048];
    int len = self->m_cb->logsGet(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onGetDiagnostics(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->diagnosticsGet) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char buf[2048];
    int len = self->m_cb->diagnosticsGet(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onGetAgentHealth(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->agentHealth) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char buf[2048];
    int len = self->m_cb->agentHealth(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onGetAgentDiagnostics(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->agentDiagnostics) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char buf[2048];
    int len = self->m_cb->agentDiagnostics(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onGetAgentPing(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->agentPing) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char buf[256];
    int len = self->m_cb->agentPing(buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onPostLogin(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->login) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char body[512];
    esp_err_t ret = self->readBody(req, body, sizeof(body));
    if (ret != ESP_OK || body[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    cJSON* j = cJSON_Parse(body);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    char username[64] = {0};
    char password[64] = {0};
    cJSON* u = cJSON_GetObjectItemCaseSensitive(j, "username");
    cJSON* p = cJSON_GetObjectItemCaseSensitive(j, "password");
    if (u && cJSON_IsString(u)) {
        strncpy(username, u->valuestring, sizeof(username) - 1);
    }
    if (p && cJSON_IsString(p)) {
        strncpy(password, p->valuestring, sizeof(password) - 1);
    }
    cJSON_Delete(j);
    if (username[0] == '\0' || password[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing username or password");
        return ESP_FAIL;
    }
    char token[65] = {0};
    int len = self->m_cb->login(username, password, token, sizeof(token));
    if (len < 0) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
        return ESP_FAIL;
    }
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"token\":\"%s\"}", token);
    return self->sendJson(req, resp);
}

esp_err_t WebServer::onPostLogout(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->logout) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    // Токен из заголовка X-Session-Token
    char tokenVal[65] = {0};
    esp_err_t ret = httpd_req_get_hdr_value_str(req, "X-Session-Token", tokenVal, sizeof(tokenVal));
    if (ret != ESP_OK || tokenVal[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-Session-Token");
        return ESP_FAIL;
    }
    ret = self->m_cb->logout(tokenVal);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid session");
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onPostAdminSetup(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->adminSetup) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char body[512];
    esp_err_t ret = self->readBody(req, body, sizeof(body));
    if (ret != ESP_OK || body[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    cJSON* j = cJSON_Parse(body);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    char username[64] = {0};
    char password[128] = {0};
    cJSON* u = cJSON_GetObjectItemCaseSensitive(j, "username");
    cJSON* p = cJSON_GetObjectItemCaseSensitive(j, "password");
    if (u && cJSON_IsString(u)) {
        strncpy(username, u->valuestring, sizeof(username) - 1);
    }
    if (p && cJSON_IsString(p)) {
        strncpy(password, p->valuestring, sizeof(password) - 1);
    }
    cJSON_Delete(j);
    if (username[0] == '\0' || password[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing username or password");
        return ESP_FAIL;
    }
    ret = self->m_cb->adminSetup(username, password);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetCaptivePortal(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Отдаём HTML страницу captive портала для настройки Wi-Fi
    const char* html = R"rawliteral(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Настройка Wi-Fi — Wireless Audio 2.1</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif; background: #0f0f0f; color: #eee; line-height: 1.5; display: flex; min-height: 100vh; }
.container { flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 20px; }
.card { background: #1c1c1c; border: 1px solid #333; border-radius: 12px; padding: 32px 28px; max-width: 400px; width: 100%; box-shadow: 0 8px 32px rgba(0,0,0,.4); }
.card h1 { font-size: 20px; margin-bottom: 8px; color: #fff; text-align: center; }
.card p { font-size: 14px; color: #aaa; text-align: center; margin-bottom: 24px; }
.form-group { margin-bottom: 16px; }
.form-group label { display: block; font-size: 14px; color: #ccc; margin-bottom: 6px; }
.form-group input { width: 100%; background: #222; border: 1px solid #444; border-radius: 6px; padding: 10px 12px; font-size: 14px; color: #eee; box-sizing: border-box; }
.form-group input:focus { outline: none; border-color: #2a6; }
.form-row { display: flex; gap: 12px; margin-bottom: 16px; }
.form-row .form-group { flex: 1; margin-bottom: 0; }
button { width: 100%; background: #2a6; color: #fff; border: none; border-radius: 6px; padding: 12px; font-size: 15px; cursor: pointer; margin-top: 8px; }
button:hover { background: #3b8; }
button:disabled { opacity: .5; cursor: not-allowed; }
button.secondary { background: #333; border: 1px solid #555; color: #ddd; margin-top: 12px; }
button.secondary:hover { background: #444; }
.network-list { margin-bottom: 16px; max-height: 200px; overflow-y: auto; border: 1px solid #333; border-radius: 6px; background: #222; }
.network-item { padding: 10px 12px; border-bottom: 1px solid #333; cursor: pointer; font-size: 13px; color: #eee; }
.network-item:last-child { border-bottom: none; }
.network-item:hover { background: #2a2a2a; }
.network-item.selected { background: #2a6; }
.network-item .rssi { float: right; font-size: 11px; color: #88c; }
.toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #2a6; color: #fff; padding: 10px 16px; border-radius: 6px; box-shadow: 0 4px 12px rgba(0,0,0,.4); display: none; z-index: 200; }
.toast.error { background: #a33; }
.spinner { display: inline-block; width: 16px; height: 16px; border: 2px solid rgba(255,255,255,.3); border-radius: 50%; border-top-color: #fff; animation: spin 1s linear infinite; margin-right: 8px; vertical-align: middle; }
@keyframes spin { to { transform: rotate(360deg); } }
</style>
</head>
<body>
<div class="container">
  <div class="card">
    <h1>Wireless Audio 2.1</h1>
    <p>Настройка точки доступа и Wi-Fi</p>

    <div class="form-group">
      <label>Имя сети (SSID)</label>
      <input type="text" id="ssid" placeholder="Wi-Fi сеть">
    </div>

    <div class="form-group">
      <label>Пароль</label>
      <input type="password" id="password" placeholder="Пароль Wi-Fi">
    </div>

    <div class="form-row">
      <div class="form-group">
        <label>Режим</label>
        <select id="mode" style="width:100%;background:#222;border:1px solid #444;border-radius:6px;padding:10px 12px;font-size:14px;color:#eee;">
          <option value="ap">Точка доступа (AP)</option>
          <option value="sta">Клиент (STA)</option>
          <option value="apsta">Репитер (AP+STA)</option>
        </select>
      </div>
    </div>

    <div id="staticFields" style="display:none;">
      <div class="form-group">
        <label>Статический IP</label>
        <input type="text" id="staIp" placeholder="192.168.1.100">
      </div>
      <div class="form-group">
        <label>Шлюз</label>
        <input type="text" id="staGateway" placeholder="192.168.1.1">
      </div>
      <div class="form-group">
        <label>Маска</label>
        <input type="text" id="staNetmask" placeholder="255.255.255.0">
      </div>
      <div class="form-group">
        <label>DNS</label>
        <input type="text" id="staDns" placeholder="192.168.1.1">
      </div>
    </div>

    <div class="form-group" style="margin-top:8px;">
      <label style="display:flex;align-items:center;gap:6px;">
        <input type="checkbox" id="dhcp" checked style="margin:0;"> DHCP (автоматический IP)
      </label>
    </div>

    <button id="saveBtn">Сохранить и подключиться</button>
    <button class="secondary" id="scanBtn">Сканировать сети</button>

    <div class="network-list" id="networkList"></div>
    <div id="message" style="margin-top:12px;font-size:13px;text-align:center;color:#aaa;"></div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
let networks = [];
const ssidInput = document.getElementById('ssid');
const passwordInput = document.getElementById('password');
const modeSelect = document.getElementById('mode');
const dhcpCheckbox = document.getElementById('dhcp');
const staticFields = document.getElementById('staticFields');
const saveBtn = document.getElementById('saveBtn');
const scanBtn = document.getElementById('scanBtn');
const networkList = document.getElementById('networkList');
const messageDiv = document.getElementById('message');
const toast = document.getElementById('toast');

modeSelect.addEventListener('change', () => {
  const isSta = modeSelect.value === 'sta' || modeSelect.value === 'apsta';
  dhcpCheckbox.disabled = !isSta;
  if (!isSta) {
    dhcpCheckbox.checked = true;
    staticFields.style.display = 'none';
  } else {
    staticFields.style.display = dhcpCheckbox.checked ? 'none' : 'block';
  }
});

dhcpCheckbox.addEventListener('change', () => {
  if (modeSelect.value === 'sta' || modeSelect.value === 'apsta') {
    staticFields.style.display = dhcpCheckbox.checked ? 'none' : 'block';
  }
});

function showToast(msg, isError) {
  toast.textContent = msg;
  toast.className = 'toast' + (isError ? ' error' : '');
  toast.style.display = 'block';
  setTimeout(() => { toast.style.display = 'none'; }, 3000);
}

function showMessage(msg) {
  messageDiv.textContent = msg;
}

async function scanNetworks() {
  scanBtn.disabled = true;
  scanBtn.textContent = 'Сканирование...';
  showMessage('Сканирование сетей...');
  try {
    const resp = await fetch('/api/wifi/scan');
    if (resp.ok) {
      const data = await resp.json();
      networks = data.networks || [];
      renderNetworks();
      showMessage(`Найдено сетей: ${networks.length}`);
    } else {
      showToast('Ошибка сканирования', true);
    }
  } catch (e) {
    showToast('Ошибка сети: ' + e.message, true);
  } finally {
    scanBtn.disabled = false;
    scanBtn.textContent = 'Сканировать сети';
  }
}

function renderNetworks() {
  networkList.innerHTML = '';
  networks.forEach((n, i) => {
    const div = document.createElement('div');
    div.className = 'network-item';
    div.innerHTML = '<span>' + escapeHtml(n.ssid) + '</span><span class="rssi">' + n.rssi + ' дБм</span>';
    div.onclick = () => {
      document.querySelectorAll('.network-item').forEach(el => el.classList.remove('selected'));
      div.classList.add('selected');
      ssidInput.value = n.ssid;
      passwordInput.focus();
    };
    networkList.appendChild(div);
  });
}

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

saveBtn.addEventListener('click', async () => {
  const ssid = ssidInput.value.trim();
  const password = passwordInput.value.trim();
  const mode = modeSelect.value;
  const dhcp = dhcpCheckbox.checked;

  if (!ssid) {
    showToast('Введите SSID', true);
    return;
  }

  saveBtn.disabled = true;
  saveBtn.textContent = 'Сохранение...';
  showToast('Подключение к сети...');

  const body = {
    mode: mode,
    apSsid: 'Audio21-Master',
    apPassword: 'audio21master',
    staSsid: ssid,
    staPassword: password,
    staDhcp: dhcp ? 'true' : 'false'
  };

  if (!dhcp) {
    body.staIp = document.getElementById('staIp').value.trim() || '192.168.1.100';
    body.staGateway = document.getElementById('staGateway').value.trim() || '192.168.1.1';
    body.staNetmask = document.getElementById('staNetmask').value.trim() || '255.255.255.0';
    body.staDns = document.getElementById('staDns').value.trim() || '192.168.1.1';
  }

  try {
    const resp = await fetch('/captive', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    if (resp.ok) {
      showToast('Настройки сохранены! Перезагрузка...', false);
      setTimeout(() => { window.location.reload(); }, 1500);
    } else {
      const err = await resp.text();
      showToast('Ошибка: ' + err, true);
      saveBtn.disabled = false;
      saveBtn.textContent = 'Сохранить и подключиться';
    }
  } catch (e) {
    showToast('Ошибка сети: ' + e.message, true);
    saveBtn.disabled = false;
    saveBtn.textContent = 'Сохранить и подключиться';
  }
});

scanBtn.addEventListener('click', scanNetworks);

// Автоматическое сканирование при загрузке
scanNetworks();
</script>
</body>
</html>
)rawliteral";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, html, strlen(html));
}

esp_err_t WebServer::onPostCaptivePortal(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self || !self->m_cb || !self->m_cb->setWifiConfig) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char body[512];
    esp_err_t ret = self->readBody(req, body, sizeof(body));
    if (ret != ESP_OK || body[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    cJSON* j = cJSON_Parse(body);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    char mode[16] = {0};
    char apSsid[33] = {0};
    char apPassword[65] = {0};
    char staSsid[33] = {0};
    char staPassword[65] = {0};
    char staDhcp[8] = {0};
    char staIp[16] = {0};
    char staGateway[16] = {0};
    char staNetmask[16] = {0};
    char staDns[16] = {0};

    cJSON* m = cJSON_GetObjectItemCaseSensitive(j, "mode");
    cJSON* as = cJSON_GetObjectItemCaseSensitive(j, "apSsid");
    cJSON* ap = cJSON_GetObjectItemCaseSensitive(j, "apPassword");
    cJSON* ss = cJSON_GetObjectItemCaseSensitive(j, "staSsid");
    cJSON* sp = cJSON_GetObjectItemCaseSensitive(j, "staPassword");
    cJSON* dh = cJSON_GetObjectItemCaseSensitive(j, "staDhcp");
    cJSON* ip = cJSON_GetObjectItemCaseSensitive(j, "staIp");
    cJSON* gw = cJSON_GetObjectItemCaseSensitive(j, "staGateway");
    cJSON* nm = cJSON_GetObjectItemCaseSensitive(j, "staNetmask");
    cJSON* dn = cJSON_GetObjectItemCaseSensitive(j, "staDns");

    if (m && cJSON_IsString(m)) strncpy(mode, m->valuestring, sizeof(mode) - 1);
    if (as && cJSON_IsString(as)) strncpy(apSsid, as->valuestring, sizeof(apSsid) - 1);
    if (ap && cJSON_IsString(ap)) strncpy(apPassword, ap->valuestring, sizeof(apPassword) - 1);
    if (ss && cJSON_IsString(ss)) strncpy(staSsid, ss->valuestring, sizeof(staSsid) - 1);
    if (sp && cJSON_IsString(sp)) strncpy(staPassword, sp->valuestring, sizeof(staPassword) - 1);
    if (dh && cJSON_IsString(dh)) strncpy(staDhcp, dh->valuestring, sizeof(staDhcp) - 1);
    if (ip && cJSON_IsString(ip)) strncpy(staIp, ip->valuestring, sizeof(staIp) - 1);
    if (gw && cJSON_IsString(gw)) strncpy(staGateway, gw->valuestring, sizeof(staGateway) - 1);
    if (nm && cJSON_IsString(nm)) strncpy(staNetmask, nm->valuestring, sizeof(staNetmask) - 1);
    if (dn && cJSON_IsString(dn)) strncpy(staDns, dn->valuestring, sizeof(staDns) - 1);

    cJSON_Delete(j);

    if (mode[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mode");
        return ESP_FAIL;
    }

    ret = self->m_cb->setWifiConfig(mode, apSsid, apPassword, staSsid, staPassword,
                                    staDhcp, staIp, staGateway, staNetmask, staDns);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save Wi-Fi config");
        return ESP_FAIL;
    }

    return self->sendJson(req, "{\"ok\":true}");
}

esp_err_t WebServer::onGetCsrf(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"csrfToken\":\"%s\"}", self->m_csrfToken);
    return self->sendJson(req, buf);
}

esp_err_t WebServer::onSSE(httpd_req_t* req)
{
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    if (!self) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Регистрируем клиента SSE для получения уведомлений в реальном времени
    SseClient client;
    client.req = req;
    client.active = true;
    self->m_sseClients.push_back(client);
    ESP_LOGI(TAG, "SSE client connected (total: %zu)", self->m_sseClients.size());

    // Отправляем приветственное сообщение
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    const char* msg = "event: connected\ndata: {\"type\":\"connected\"}\n\n";
    esp_err_t ret = httpd_resp_send_chunk(req, msg, strlen(msg));
    if (ret != ESP_OK) {
        for (auto& c : self->m_sseClients) {
            if (c.req == req) c.active = false;
        }
    }
    return ret;
}

esp_err_t WebServer::onGetFavicon(httpd_req_t* req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t WebServer::onGetRoot(httpd_req_t* req)
{
    const char* html = R"rawliteral(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wireless Audio 2.1 Master</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif; background: #0f0f0f; color: #eee; line-height: 1.5; }
header { background: #1a1a1a; border-bottom: 1px solid #333; padding: 12px 16px; position: sticky; top: 0; z-index: 100; }
header h1 { font-size: 17px; display: flex; align-items: center; gap: 10px; }
header .dot { width: 8px; height: 8px; border-radius: 50%; background: #2a6; animation: blink 2s infinite; }
@keyframes blink { 0%,100% { opacity: .4; } 50% { opacity: 1; } }
main { max-width: 720px; margin: 0 auto; padding: 16px; }
nav { display: flex; gap: 8px; margin-bottom: 16px; border-bottom: 1px solid #333; padding-bottom: 8px; }
nav button { flex: 1; background: #222; border: 1px solid #444; color: #aaa; padding: 10px; border-radius: 6px; cursor: pointer; font-size: 14px; }
nav button.active { background: #2a6; color: #fff; border-color: #2a6; }
.tab { display: none; }
.tab.active { display: block; }
.card { background: #1c1c1c; border: 1px solid #333; border-radius: 8px; padding: 16px; margin-bottom: 14px; }
.card h2 { font-size: 15px; margin-bottom: 12px; color: #88c; }
.row { display: flex; gap: 10px; align-items: center; margin-bottom: 10px; }
.row label { flex: 1; font-size: 14px; color: #ccc; }
input[type="range"] { flex: 2; accent-color: #2a6; }
input[type="text"], input[type="password"], select { background: #222; color: #eee; border: 1px solid #444; border-radius: 6px; padding: 8px; font-size: 14px; width: 100%; }
button { background: #2a6; color: #fff; border: none; border-radius: 6px; padding: 10px 16px; font-size: 14px; cursor: pointer; }
button:hover { background: #3b8; }
button.secondary { background: #333; border: 1px solid #555; color: #ddd; }
button.secondary:hover { background: #444; }
button.danger { background: #a33; }
button.danger:hover { background: #c44; }
button:disabled { opacity: .5; cursor: not-allowed; }
.val { font-family: monospace; color: #88c; margin-left: 8px; }
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th { text-align: left; color: #888; padding: 6px 8px; border-bottom: 1px solid #333; }
td { padding: 6px 8px; border-bottom: 1px solid #2a2a2a; }
.toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #2a6; color: #fff; padding: 10px 16px; border-radius: 6px; box-shadow: 0 4px 12px rgba(0,0,0,.4); display: none; z-index: 200; animation: slideIn .3s ease; }
@keyframes slideIn { from { transform: translate(-50%, 20px); opacity: 0; } to { transform: translate(-50%, 0); opacity: 1; } }
.toast.error { background: #a33; }
.small { font-size: 12px; color: #888; }
.login { max-width: 320px; margin: 60px auto; background: #1c1c1c; border: 1px solid #333; border-radius: 8px; padding: 24px; }
.login h2 { text-align: center; margin-bottom: 18px; font-size: 18px; }
.login .row { flex-direction: column; gap: 8px; }
.login input { width: 100%; }
.status-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
.status-item { background: #222; border-radius: 6px; padding: 12px; }
.status-item .lbl { font-size: 12px; color: #888; }
.status-item .val { font-size: 15px; color: #eee; font-weight: 600; }
</style>
</head>
<body>
<header>
<h1><span class="dot" id="connDot"></span> Wireless Audio 2.1 Master</h1>
</header>
<main>
<nav>
<button class="active" onclick="showTab('dashboard')">Панель</button>
<button onclick="showTab('wifi')">Wi-Fi</button>
<button onclick="showTab('system')">Система</button>
</nav>

<section id="dashboard" class="tab active">
<div class="card">
<h2>Аудио</h2>
<div class="row">
<label>Громкость мастера</label>
<input type="range" id="volSlider" min="0" max="100" value="50">
<span id="volVal" class="val">50%</span>
</div>
<div class="row">
<label>Отключение звука (Mute)</label>
<button id="muteBtn" onclick="toggleMute()">Выкл</button>
</div>
<div class="row">
<label>Частота кроссовера</label>
<input type="range" id="xoverSlider" min="40" max="500" value="90">
<span id="xoverVal" class="val">90 Гц</span>
</div>
<div class="row">
<label>Тембр: бас (dB)</label>
<input type="range" id="bassSlider" min="-12" max="12" value="0">
<span id="bassVal" class="val">0 dB</span>
</div>
<div class="row">
<label>Тембр: высокие (dB)</label>
<input type="range" id="trebleSlider" min="-12" max="12" value="0">
<span id="trebleVal" class="val">0 dB</span>
</div>
<div class="row">
<label>Громкость L</label>
<input type="range" id="volLeftSlider" min="0" max="100" value="100">
<span id="volLeftVal" class="val">100%</span>
</div>
<div class="row">
<label>Громкость R</label>
<input type="range" id="volRightSlider" min="0" max="100" value="100">
<span id="volRightVal" class="val">100%</span>
</div>
<div class="row">
<label>Громкость Sub</label>
<input type="range" id="volSubSlider" min="0" max="100" value="100">
<span id="volSubVal" class="val">100%</span>
</div>
<div class="row">
<label>Задержка L (мс)</label>
<input type="range" id="delayLeftSlider" min="0" max="200" value="0">
<span id="delayLeftVal" class="val">0 мс</span>
</div>
<div class="row">
<label>Задержка R (мс)</label>
<input type="range" id="delayRightSlider" min="0" max="200" value="0">
<span id="delayRightVal" class="val">0 мс</span>
</div>
<div class="row">
<label>Задержка Sub (мс)</label>
<input type="range" id="delaySubSlider" min="0" max="200" value="40">
<span id="delaySubVal" class="val">40 мс</span>
</div>
<div class="row">
<label>I2S выход</label>
<span id="i2sStatus" class="val">—</span>
</div>
<div class="row">
<label>Джиттер-буфер</label>
<span id="jitterVal" class="val">—</span>
</div>
</div>
<div class="status-grid">
<div class="status-item"><div class="lbl">Семпл. частота</div><div class="val" id="srVal">—</div></div>
<div class="status-item"><div class="lbl">Хостнейм</div><div class="val mono" id="hostVal">—</div></div>
<div class="status-item"><div class="lbl">Режим Wi-Fi</div><div class="val" id="wifiModeVal">—</div></div>
<div class="status-item"><div class="lbl">IP-адрес</div><div class="val mono" id="ipVal">—</div></div>
</div>
</section>

<section id="wifi" class="tab">
<div class="card">
<h2>Wi-Fi конфигурация</h2>
<div class="row">
<label>Режим</label>
<select id="wifiMode">
<option value="ap">Точка доступа (AP)</option>
<option value="sta">Клиент (STA)</option>
<option value="apsta">Репитер (AP+STA)</option>
</select>
</div>
<div class="row">
<label>AP SSID</label>
<input type="text" id="apSsid" placeholder="audio-master">
</div>
<div class="row">
<label>AP пароль</label>
<input type="password" id="apPassword" placeholder="••••••••">
</div>
<div class="row">
<label>STA SSID</label>
<input type="text" id="staSsid" placeholder="Keenetic">
</div>
<div class="row">
<label>STA пароль</label>
<input type="password" id="staPassword" placeholder="••••••••">
</div>
<button class="secondary" onclick="saveWifi()">Сохранить Wi-Fi</button>
</div>
</section>

<section id="system" class="tab">
<div class="card">
<h2>Система</h2>
<div class="row">
<label>OTA URL прошивки</label>
<input type="text" id="otaUrl" placeholder="http://192.168.1.100/firmware.bin">
</div>
<button onclick="doReboot()" class="danger">Перезагрузить устройство</button>
<button onclick="doFactoryReset()" class="danger secondary">Сброс к заводским настройкам</button>
<button onclick="doOta()">Начать OTA обновление</button>
</div>
</section>
</main>
<div class="toast" id="toast"></div>

<script>
let csrfToken = '';
let wsBase = window.location.origin;

function toast(msg, isError) {
const t = document.getElementById('toast');
t.textContent = msg;
t.className = 'toast' + (isError ? ' error' : '');
t.style.display = 'block';
setTimeout(() => { t.style.display = 'none'; }, 3000);
}

async function api(path, opts = {}) {
const headers = { 'X-CSRF-Token': csrfToken, ...opts.headers };
if (opts.body && typeof opts.body === 'object') {
headers['Content-Type'] = 'application/json';
opts.body = JSON.stringify(opts.body);
}
const res = await fetch(wsBase + path, { ...opts, headers });
if (!res.ok) throw new Error('HTTP ' + res.status);
const ct = res.headers.get('content-type') || '';
if (ct.includes('application/json')) return res.json();
return res.text();
}

async function loadCsrf() {
try {
const d = await api('/api/csrf');
csrfToken = d.csrfToken || '';
} catch (e) { console.warn('csrf load failed', e); }
}

async function loadStatus() {
try {
const s = await api('/api/status');
document.getElementById('volVal').textContent = s.volume + '%';
document.getElementById('volSlider').value = s.volume;
document.getElementById('xoverVal').textContent = s.crossoverHz + ' Гц';
document.getElementById('xoverSlider').value = s.crossoverHz;
// Загрузка тембра
try { const t = await api('/api/tone'); document.getElementById('bassSlider').value = t.bass; document.getElementById('bassVal').textContent = t.bass + ' dB'; document.getElementById('trebleSlider').value = t.treble; document.getElementById('trebleVal').textContent = t.treble + ' dB'; } catch {}
// Загрузка канальных громкостей
try { const cv = await api('/api/channel-volumes'); document.getElementById('volLeftSlider').value = cv.left; document.getElementById('volLeftVal').textContent = cv.left + '%'; document.getElementById('volRightSlider').value = cv.right; document.getElementById('volRightVal').textContent = cv.right + '%'; document.getElementById('volSubSlider').value = cv.sub; document.getElementById('volSubVal').textContent = cv.sub + '%'; } catch {}
document.getElementById('i2sStatus').textContent = s.i2sOn ? 'вкл' : 'выкл';
document.getElementById('jitterVal').textContent = s.jitterAvailable + ' семпл';
document.getElementById('srVal').textContent = s.sampleRate + ' Гц';
document.getElementById('hostVal').textContent = s.hostname;
document.getElementById('wifiModeVal').textContent = s.wifiMode;
document.getElementById('ipVal').textContent = s.ipAddress || '—';
document.getElementById('connDot').style.background = s.wifiMode === 'sta' ? '#2a6' : '#a62';
} catch (e) { toast('Ошибка загрузки статуса: ' + e.message, true); }
}

async function loadWifi() {
try {
const w = await api('/api/wifi');
document.getElementById('wifiMode').value = w.mode || 'ap';
document.getElementById('apSsid').value = w.apSsid || '';
document.getElementById('apPassword').value = w.apPassword || '';
document.getElementById('staSsid').value = w.staSsid || '';
document.getElementById('staPassword').value = w.staPassword || '';
} catch (e) { toast('Ошибка загрузки Wi-Fi: ' + e.message, true); }
}

function showTab(name) {
document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
document.querySelectorAll('nav button').forEach(b => b.classList.remove('active'));
document.getElementById(name).classList.add('active');
document.querySelectorAll('nav button').forEach(b => {
if (b.textContent.toLowerCase().includes(name === 'dashboard' ? 'панель' : name === 'wifi' ? 'wi-fi' : 'система')) b.classList.add('active');
});
}

document.getElementById('volSlider').addEventListener('input', async (e) => {
const v = e.target.value;
document.getElementById('volVal').textContent = v + '%';
await api('/api/volume', { method: 'POST', body: { volume: parseInt(v) } });
});
document.getElementById('xoverSlider').addEventListener('input', async (e) => {
const hz = e.target.value;
document.getElementById('xoverVal').textContent = hz + ' Гц';
await api('/api/crossover', { method: 'POST', body: { crossoverHz: parseInt(hz) } });
});

// Mute toggle
async function toggleMute() {
const btn = document.getElementById('muteBtn');
const isMuted = btn.textContent === 'Вкл';
btn.textContent = isMuted ? 'Выкл' : 'Вкл';
await api('/api/mute', { method: 'PUT', body: { mute: !isMuted } });
}

// Tone sliders
document.getElementById('bassSlider').addEventListener('input', async (e) => {
const v = e.target.value;
document.getElementById('bassVal').textContent = v + ' dB';
await api('/api/tone', { method: 'PUT', body: { bass: parseFloat(v), treble: parseFloat(document.getElementById('trebleSlider').value) } });
});
document.getElementById('trebleSlider').addEventListener('input', async (e) => {
const v = e.target.value;
document.getElementById('trebleVal').textContent = v + ' dB';
await api('/api/tone', { method: 'PUT', body: { bass: parseFloat(document.getElementById('bassSlider').value), treble: parseFloat(v) } });
});

// Channel volume sliders
['Left','Right','Sub'].forEach(ch => {
const slider = document.getElementById('vol' + ch + 'Slider');
const val = document.getElementById('vol' + ch + 'Val');
slider.addEventListener('input', async (e) => {
const v = e.target.value;
val.textContent = v + '%';
const body = { left: parseInt(document.getElementById('volLeftSlider').value), right: parseInt(document.getElementById('volRightSlider').value), sub: parseInt(document.getElementById('volSubSlider').value) };
await api('/api/channel-volumes', { method: 'PUT', body: body });
});
});

// Delay sliders
['Left','Right','Sub'].forEach(ch => {
const lower = ch.toLowerCase();
const slider = document.getElementById('delay' + ch + 'Slider');
const val = document.getElementById('delay' + ch + 'Val');
slider.addEventListener('input', async (e) => {
const v = e.target.value;
val.textContent = v + ' мс';
await api('/api/delay', { method: 'PUT', body: { channel: lower, delay_ms: parseInt(v) } });
});
});

async function saveWifi() {
const mode = document.getElementById('wifiMode').value;
await api('/api/wifi', {
method: 'POST',
body: {
mode: mode,
apSsid: document.getElementById('apSsid').value,
apPassword: document.getElementById('apPassword').value,
staSsid: mode !== 'ap' ? document.getElementById('staSsid').value : undefined,
staPassword: mode !== 'ap' ? document.getElementById('staPassword').value : undefined
}
});
toast('Wi-Fi конфигурация сохранена');
}

async function doReboot() {
if (!confirm('Перезагрузить устройство?')) return;
await api('/api/system/reboot', { method: 'POST' });
toast('Перезагрузка...');
}

async function doFactoryReset() {
if (!confirm('Сбросить все настройки к заводским?')) return;
await api('/api/system/factory_reset', { method: 'POST' });
toast('Сброс настроек. Перезагрузка...');
}

async function doOta() {
const url = document.getElementById('otaUrl').value.trim();
if (!url) { toast('Укажите URL прошивки', true); return; }
await api('/api/system/ota', { method: 'POST', body: { url: url } });
toast('OTA обновление запущено');
}

// SSE
try {
const evt = new EventSource(wsBase + '/api/events');
evt.onmessage = (e) => {
try { const d = JSON.parse(e.data); if (d.type === 'status') loadStatus(); } catch {}
};
evt.onerror = () => { evt.close(); setTimeout(() => location.reload(), 5000); };
} catch (e) { console.warn('SSE not available'); }

window.addEventListener('DOMContentLoaded', async () => {
await loadCsrf();
await loadStatus();
await loadWifi();
setInterval(loadStatus, 3000);
});
</script>
</body>
</html>
)rawliteral";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html, strlen(html));
}

// --- Вспомогательные функции ---

esp_err_t WebServer::sendJson(httpd_req_t* req, const char* json)
{
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sendJson failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t WebServer::sendText(httpd_req_t* req, const char* text, size_t len)
{
    httpd_resp_set_type(req, "text/plain");
    esp_err_t ret = httpd_resp_send(req, text, len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sendText failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t WebServer::readBody(httpd_req_t* req, char* buf, size_t bufSize)
{
    ssize_t contentLen = req->content_len;
    if (contentLen < 0) {
        // Длина неизвестна — читаем до конца потока
        int received = 0;
        while (received < (int)bufSize - 1) {
            int recvLen = httpd_req_recv(req, buf + received, bufSize - 1 - received);
            if (recvLen <= 0) break;
            received += recvLen;
        }
        buf[received] = '\0';
        return received > 0 ? ESP_OK : ESP_FAIL;
    }
    size_t remaining = (size_t)contentLen;
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

// Проверка CSRF токена из заголовка X-CSRF-Token
bool WebServer::validateCsrfToken(httpd_req_t* req)
{
    if (!m_cb || !m_cb->validateCsrfToken) {
        return false;
    }

    char token[128];
    size_t token_size = sizeof(token);
    int ret = httpd_req_get_hdr_value_str(req, "X-CSRF-Token", token, token_size);
    if (ret != ESP_OK) {
        return false;
    }

    return m_cb->validateCsrfToken(token);
}

} // namespace audio21

// Глобальный CSRF токен для доступа из main.cpp (объявлен выше в этом файле)
namespace audio21 {

const char* web_server_get_csrf_token(void)
{
    return g_csrf_token;
}

bool web_server_validate_csrf_token(const char* token)
{
    if (!token || token[0] == '\0') {
        return false;
    }
    return strcmp(token, g_csrf_token) == 0;
}

} // namespace audio21
