// web_sse.cpp — Server-Sent Events /api/events (модуль A1/N3).
// Поток событий статуса сателлитов для Web UI в реальном времени.
#include "web_server.h"

namespace audio21 {

MasterWebServer* g_webServer = nullptr;

void MasterWebServer::sseBroadcast(const char* event, const char* data) {
    if (!event || !data) return;
    // Удаляем отключённых клиентов.
    auto it = m_sseClients.begin();
    while (it != m_sseClients.end()) {
        WiFiClient* c = *it;
        if (!c || !c->connected()) {
            delete c;
            it = m_sseClients.erase(it);
        } else {
            c->print("event: ");
            c->print(event);
            c->print("\r\n");
            c->print("data: ");
            c->print(data);
            c->print("\r\n\r\n");
            ++it;
        }
    }
}

void MasterWebServer::handleSSE(WebServer& s) {
    // C5.8: SSE требует CORS для кросс-доменного доступа (если нужно).
    s.sendHeader("Content-Type", "text/event-stream");
    s.sendHeader("Cache-Control", "no-cache");
    s.sendHeader("Connection", "keep-alive");
    s.sendHeader("Access-Control-Allow-Origin", "*");
    s.send(200, "text/event-stream", "");

    WiFiClient* client = new WiFiClient(s.client());
    m_sseClients.push_back(client);

    // Отправляем текущий статус сразу после подключения.
    // (Клиент может сделать GET /api/status отдельно, но событие initial ускоряет синхронизацию.)
    client->print("event: initial\r\n");
    client->print("data: {\"status\":\"connected\"}\r\n\r\n");

    // Держим соединение открытым: клиент сам отключится, когда захочет.
    // Сервер не блокируется — handleClient() в main.cpp опрашивает все серверы.
}

} // namespace audio21
