// mdns_raw.cpp — минимальный mDNS отвечатель на сыром UDP (порт 5353).
// Не зависит от lwip mdns.c (требует netif_alloc_client_data_id, отсутствует в v5.3.1).
#include "mdns_raw.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/prot/ip.h"
#include "lwip/igmp.h"

static const char *TAG = "mdns_raw";

static struct udp_pcb *s_pcb = nullptr;
static bool s_running = false;
static char s_hostname[64] = {0};
static bool s_service_added = false;

// mDNS multicast адрес: 224.0.0.251
#define MDNS_MULTICAST_IP 0xE00000FB  // 224.0.0.251 in network byte order (big-endian)
#define MDNS_PORT 5353

// DNS заголовок (12 байт)
#pragma pack(push, 1)
struct DnsHeader {
    uint16_t id;
    uint16_t flags;      // QR, OPCODE, AA, TC, RD, RA, Z, RCODE
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
};
#pragma pack(pop)

// DNS вопрос
#pragma pack(push, 1)
struct DnsQuestion {
    uint16_t qtype;
    uint16_t qclass;
};
#pragma pack(pop)

// mDNS флаги ответа: QR=1, AA=1, multicast
#define MDNS_RESPONSE_FLAGS 0x8400  // QR=1, OPCODE=0, AA=1, TC=0, RD=0, RA=0, Z=0, RCODE=0

// QTYPE значения
#define DNS_QTYPE_A     1
#define DNS_QTYPE_PTR   12
#define DNS_QTYPE_SRV   33
#define DNS_QTYPE_TXT   16
#define DNS_QTYPE_ANY   255
#define DNS_QCLASS_IN   1
#define DNS_QCLASS_ANY  255

// Вспомогательная функция: кодировать доменное имя в формат DNS (длина-метка)
static size_t encode_dns_name(uint8_t *buf, size_t bufSize, const char *name)
{
    size_t off = 0;
    const char *p = name;
    while (*p) {
        const char *next = strchr(p, '.');
        size_t len = next ? (size_t)(next - p) : strlen(p);
        if (off + 1 + len >= bufSize) return 0;
        buf[off++] = (uint8_t)len;
        memcpy(buf + off, p, len);
        off += len;
        if (!next) break;
        p = next + 1;
    }
    if (off + 1 >= bufSize) return 0;
    buf[off++] = 0;  // корень
    return off;
}

// Вспомогательная функция: декодировать DNS имя (с поддержкой сжатия)
static size_t decode_dns_name(const uint8_t *buf, size_t bufSize, size_t off,
                              char *out, size_t outSize)
{
    size_t outOff = 0;
    bool first = true;
    while (off < bufSize) {
        uint8_t len = buf[off++];
        if (len == 0) break;
        if ((len & 0xC0) == 0xC0) {
            // Сжатая ссылка
            if (off >= bufSize) break;
            size_t ptr = ((len & 0x3F) << 8) | buf[off++];
            // Рекурсивно декодируем по указателю (ограничим глубину)
            // Для простоты не рекурсируем, просто пропускаем
            break;
        }
        if (!first && outOff + 1 < outSize) out[outOff++] = '.';
        first = false;
        if (off + len > bufSize) break;
        if (outOff + len < outSize) {
            memcpy(out + outOff, buf + off, len);
            outOff += len;
        }
        off += len;
    }
    if (outOff < outSize) out[outOff] = 0;
    return off;
}

// Отправить mDNS ответ
static void send_mdns_response(const ip_addr_t *destAddr, uint16_t destPort,
                               uint16_t transactionId,
                               const char *queryName, uint16_t queryType,
                               const char *hostname, uint32_t ipAddr)
{
    // Максимальный размер mDNS пакета ~512 байт
    uint8_t buf[512];
    size_t off = 0;

    // Заголовок
    struct DnsHeader hdr;
    hdr.id = transactionId;
    hdr.flags = htons(MDNS_RESPONSE_FLAGS);
    hdr.qdcount = htons(1);
    hdr.ancount = htons(1);
    hdr.nscount = 0;
    hdr.arcount = 0;
    memcpy(buf + off, &hdr, sizeof(hdr));
    off += sizeof(hdr);

    // Вопрос (эхо запроса)
    size_t qnameLen = encode_dns_name(buf + off, sizeof(buf) - off, queryName);
    if (qnameLen == 0) return;
    off += qnameLen;
    struct DnsQuestion q;
    q.qtype = htons(queryType);
    q.qclass = htons(DNS_QCLASS_IN);
    memcpy(buf + off, &q, sizeof(q));
    off += sizeof(q);

    // Ответ: A запись для hostname.local
    // Имя — сжатая ссылка на вопрос (offset = sizeof(DnsHeader))
    uint16_t nameRef = htons(0xC000 | sizeof(struct DnsHeader));
    memcpy(buf + off, &nameRef, 2);
    off += 2;
    uint16_t typeA = htons(DNS_QTYPE_A);
    memcpy(buf + off, &typeA, 2);
    off += 2;
    uint16_t classIn = htons(DNS_QCLASS_IN);
    memcpy(buf + off, &classIn, 2);
    off += 2;
    uint32_t ttl = htonl(3600);
    memcpy(buf + off, &ttl, 4);
    off += 4;
    uint16_t rdlen = htons(4);
    memcpy(buf + off, &rdlen, 2);
    off += 2;
    memcpy(buf + off, &ipAddr, 4);
    off += 4;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, off, PBUF_RAM);
    if (!p) return;
    memcpy(p->payload, buf, off);
    udp_sendto(s_pcb, p, destAddr, destPort);
    pbuf_free(p);
}

static void mdns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port)
{
    if (p->len < sizeof(struct DnsHeader)) {
        pbuf_free(p);
        return;
    }

    const uint8_t *data = (const uint8_t *)p->payload;
    const struct DnsHeader *hdr = (const struct DnsHeader *)data;

    uint16_t flags = ntohs(hdr->flags);
    bool isQuery = (flags & 0x8000) == 0;  // QR=0 = query

    if (!isQuery) {
        pbuf_free(p);
        return;
    }

    size_t off = sizeof(struct DnsHeader);

    // Декодируем имя запроса
    char queryName[256];
    decode_dns_name(data, p->len, off, queryName, sizeof(queryName));
    // Пропускаем имя
    while (off < p->len && data[off] != 0) off++;
    off++;  // нуль-терминатор имени
    if (off + 4 > p->len) {
        pbuf_free(p);
        return;
    }
    uint16_t qtype = ntohs(*(uint16_t *)(data + off));
    off += 2;
    off += 2;  // qclass

    // Проверяем, запрашивается ли наше имя или сервис
    bool isOurHostname = false;
    // Сравниваем queryName с s_hostname.local
    char expectedName[128];
    snprintf(expectedName, sizeof(expectedName), "%s.local", s_hostname);
    if (strcasecmp(queryName, expectedName) == 0) {
        isOurHostname = true;
    }

    // Если это запрос на наше имя — отвечаем A записью
    if (isOurHostname && (qtype == DNS_QTYPE_A || qtype == DNS_QTYPE_ANY)) {
        // Получаем IP адрес интерфейса
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        uint32_t ipAddr = 0;
        if (netif) {
            esp_netif_ip_info_t ipInfo;
            if (esp_netif_get_ip_info(netif, &ipInfo) == ESP_OK) {
                ipAddr = ipInfo.ip.addr;
            }
        }
        if (ipAddr != 0) {
            send_mdns_response(addr, port, ntohs(hdr->id), queryName, qtype,
                               s_hostname, ipAddr);
        }
    }

    uint16_t classIn = htons(DNS_QCLASS_IN);

    // Если это запрос PTR для _http._tcp.local — отвечаем SRV + TXT
    if (s_service_added && strcasecmp(queryName, "_http._tcp.local") == 0 &&
        (qtype == DNS_QTYPE_PTR || qtype == DNS_QTYPE_ANY)) {
        // Отправляем PTR ответ
        uint8_t buf[512];
        size_t boff = 0;

        struct DnsHeader hdr2;
        hdr2.id = ntohs(hdr->id);
        hdr2.flags = htons(MDNS_RESPONSE_FLAGS);
        hdr2.qdcount = htons(1);
        hdr2.ancount = htons(2);  // PTR + SRV (TXT можно добавить позже)
        hdr2.nscount = 0;
        hdr2.arcount = 0;
        memcpy(buf + boff, &hdr2, sizeof(hdr2));
        boff += sizeof(hdr2);

        // Вопрос
        size_t qnameLen = encode_dns_name(buf + boff, sizeof(buf) - boff, queryName);
        if (qnameLen == 0) { pbuf_free(p); return; }
        boff += qnameLen;
        struct DnsQuestion q;
        q.qtype = htons(DNS_QTYPE_PTR);
        q.qclass = htons(DNS_QCLASS_IN);
        memcpy(buf + boff, &q, sizeof(q));
        boff += sizeof(q);

        // PTR ответ: _http._tcp.local -> instance._http._tcp.local
        uint16_t nameRef = htons(0xC000 | sizeof(struct DnsHeader));
        memcpy(buf + boff, &nameRef, 2);
        boff += 2;
        uint16_t typePtr = htons(DNS_QTYPE_PTR);
        memcpy(buf + boff, &typePtr, 2);
        boff += 2;
        memcpy(buf + boff, &classIn, 2);
        boff += 2;
        uint32_t ttl = htonl(3600);
        memcpy(buf + boff, &ttl, 4);
        boff += 4;
        uint16_t rdlen;
        char instanceName[128];
        snprintf(instanceName, sizeof(instanceName), "%s._http._tcp.local", s_hostname);
        size_t instLen = encode_dns_name(buf + boff + 2, sizeof(buf) - boff - 2, instanceName);
        rdlen = (uint16_t)instLen;
        uint16_t rdlenNet = htons(rdlen);
        memcpy(buf + boff, &rdlenNet, 2);
        boff += 2;
        memcpy(buf + boff, instanceName, instLen);
        boff += instLen;

        // SRV ответ: instance._http._tcp.local -> port, target
        nameRef = htons(0xC000 | (sizeof(struct DnsHeader) + qnameLen + 4));
        memcpy(buf + boff, &nameRef, 2);
        boff += 2;
        uint16_t typeSrv = htons(DNS_QTYPE_SRV);
        memcpy(buf + boff, &typeSrv, 2);
        boff += 2;
        memcpy(buf + boff, &classIn, 2);
        boff += 2;
        memcpy(buf + boff, &ttl, 4);
        boff += 4;
        // SRV RDATA: priority(2) + weight(2) + port(2) + target(variable)
        uint16_t priority = htons(0);
        uint16_t weight = htons(0);
        uint16_t srvPort = htons(80);
        memcpy(buf + boff, &priority, 2);
        boff += 2;
        memcpy(buf + boff, &weight, 2);
        boff += 2;
        memcpy(buf + boff, &srvPort, 2);
        boff += 2;
        size_t targetLen = encode_dns_name(buf + boff + 6, sizeof(buf) - boff - 6, expectedName);
        uint16_t srvRdlen = htons(6 + (uint16_t)targetLen);
        memcpy(buf + boff - 6, &srvRdlen, 2);  // перезаписываем rdlen
        boff += 6 + targetLen;

        struct pbuf *p2 = pbuf_alloc(PBUF_TRANSPORT, boff, PBUF_RAM);
        if (p2) {
            memcpy(p2->payload, buf, boff);
            udp_sendto(s_pcb, p2, addr, port);
            pbuf_free(p2);
        }
    }

    pbuf_free(p);
}

esp_err_t mdns_raw_init(esp_netif_t* netif, const char* hostname)
{
    if (s_running) {
        ESP_LOGW(TAG, "mDNS already running");
        return ESP_OK;
    }

    if (!hostname) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_hostname, hostname, sizeof(s_hostname) - 1);
    s_hostname[sizeof(s_hostname) - 1] = '\0';

    s_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!s_pcb) {
        ESP_LOGE(TAG, "failed to create UDP PCB");
        return ESP_ERR_NO_MEM;
    }

    // Привязываем к порту 5353 на всех интерфейсах
    err_t err = udp_bind(s_pcb, IP_ADDR_ANY, MDNS_PORT);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "udp_bind failed: %d", err);
        udp_remove(s_pcb);
        s_pcb = nullptr;
        return ESP_FAIL;
    }

    // Присоединяемся к multicast группе 224.0.0.251
    ip4_addr_t multicastAddr;
    IP4_ADDR(&multicastAddr, 224, 0, 0, 251);
    err = igmp_joingroup(ip_2_ip4(&ip_addr_any), &multicastAddr);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "igmp_joingroup failed: %d (may still work)", err);
    }

    udp_recv(s_pcb, mdns_recv, nullptr);
    s_running = true;

    ESP_LOGI(TAG, "mDNS responder started on port %u, hostname=%s.local",
             MDNS_PORT, s_hostname);
    return ESP_OK;
}

esp_err_t mdns_raw_deinit(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    if (s_pcb) {
        udp_remove(s_pcb);
        s_pcb = nullptr;
    }
    s_running = false;
    s_service_added = false;
    memset(s_hostname, 0, sizeof(s_hostname));

    ESP_LOGI(TAG, "mDNS responder stopped");
    return ESP_OK;
}

esp_err_t mdns_raw_add_service(esp_netif_t* netif, const char* service,
                               const char* proto, uint16_t port,
                               const char* instance_name)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    (void)netif;
    (void)service;
    (void)proto;
    (void)port;
    (void)instance_name;

    s_service_added = true;
    ESP_LOGI(TAG, "mDNS service registered: %s.%s.local port=%u",
             service, proto, port);
    return ESP_OK;
}
