#include "dns_captive.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/dns.h"

static const char *TAG = "dns_captive";

static struct udp_pcb *s_dns_pcb = nullptr;
static uint32_t s_captive_ip = 0xC0A80401; // 192.168.4.1 little-endian
static bool s_running = false;

// DNS заголовок (12 байт)
#pragma pack(push, 1)
struct DnsHeader {
    uint16_t id;
    uint16_t flags;
    uint16_t questions;
    uint16_t answer_rrs;
    uint16_t authority_rrs;
    uint16_t additional_rrs;
};
#pragma pack(pop)

// DNS ответ: заголовок + 1 вопрос (копия) + 1 A-запись
// Максимальный размер UDP пакета DNS — 512 байт

static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                     const ip_addr_t *addr, u16_t port)
{
    if (p == nullptr || p->len < sizeof(DnsHeader)) {
        pbuf_free(p);
        return;
    }

    uint8_t *buf = (uint8_t *)p->payload;
    DnsHeader *hdr = (DnsHeader *)buf;

    // Формируем ответ: флаги = QR + RD + RA, код ответа = 0 (NoError)
    uint16_t flags = htons(0x8180); // QR=1, OPCODE=0, AA=0, TC=0, RD=1, RA=1, Z=0, RCODE=0
    uint16_t questions = htons(ntohs(hdr->questions));
    uint16_t answer_rrs = htons(1);
    uint16_t authority_rrs = 0;
    uint16_t additional_rrs = 0;

    // Вычисляем размер вопроса (имя + тип + класс)
    size_t qname_len = 0;
    uint8_t *qname_ptr = buf + sizeof(DnsHeader);
    while (qname_ptr < buf + p->len && *qname_ptr != 0) {
        qname_len += *qname_ptr + 1;
        qname_ptr += *qname_ptr + 1;
    }
    qname_len += 1; // нуль-терминатор имени
    qname_ptr++; // пропускаем нуль
    size_t qtype_qclass_len = 4; // тип (2) + класс (2)
    size_t question_len = qname_len + qtype_qclass_len;

    // Размер ответа: заголовок + вопрос + A-запись (имя=ссылка на вопрос, тип=A, класс=IN, TTL, длина=4, IP)
    size_t response_len = sizeof(DnsHeader) + question_len +
                          2 + // имя: ссылка на вопрос (0xC000 | offset)
                          2 + // тип A
                          2 + // класс IN
                          4 + // TTL
                          2 + // длина данных = 4
                          4;  // IP адрес

    struct pbuf *response = pbuf_alloc(PBUF_TRANSPORT, response_len, PBUF_RAM);
    if (response == nullptr) {
        pbuf_free(p);
        return;
    }

    uint8_t *resp_buf = (uint8_t *)response->payload;
    size_t off = 0;

    // Заголовок
    memcpy(resp_buf + off, &hdr->id, 2); off += 2;
    memcpy(resp_buf + off, &flags, 2); off += 2;
    memcpy(resp_buf + off, &questions, 2); off += 2;
    memcpy(resp_buf + off, &answer_rrs, 2); off += 2;
    memcpy(resp_buf + off, &authority_rrs, 2); off += 2;
    memcpy(resp_buf + off, &additional_rrs, 2); off += 2;

    // Вопрос (копия из запроса)
    memcpy(resp_buf + off, buf + sizeof(DnsHeader), question_len); off += question_len;

    // A-запись: имя — сжатая ссылка на начало вопроса (offset = sizeof(DnsHeader))
    uint16_t name_ref = htons(0xC000 | sizeof(DnsHeader));
    memcpy(resp_buf + off, &name_ref, 2); off += 2;
    uint16_t type_a = htons(1); // A
    memcpy(resp_buf + off, &type_a, 2); off += 2;
    uint16_t class_in = htons(1); // IN
    memcpy(resp_buf + off, &class_in, 2); off += 2;
    uint32_t ttl = htonl(60);
    memcpy(resp_buf + off, &ttl, 4); off += 4;
    uint16_t rdlength = htons(4);
    memcpy(resp_buf + off, &rdlength, 2); off += 2;
    memcpy(resp_buf + off, &s_captive_ip, 4); off += 4;

    err_t err = udp_sendto(pcb, response, addr, port);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "udp_sendto failed: %d", err);
    }

    pbuf_free(response);
    pbuf_free(p);
}

esp_err_t dns_captive_start(uint32_t ip)
{
    if (s_running) {
        ESP_LOGW(TAG, "DNS captive server already running");
        return ESP_OK;
    }

    s_captive_ip = ip;

    s_dns_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (s_dns_pcb == nullptr) {
        ESP_LOGE(TAG, "failed to create UDP PCB");
        return ESP_ERR_NO_MEM;
    }

    err_t err = udp_bind(s_dns_pcb, IP_ADDR_ANY, 53);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "failed to bind UDP port 53: %d", err);
        udp_remove(s_dns_pcb);
        s_dns_pcb = nullptr;
        return ESP_FAIL;
    }

    udp_recv(s_dns_pcb, dns_recv, nullptr);
    s_running = true;

    ESP_LOGI(TAG, "Captive DNS server started on port 53, responding with IP %d.%d.%d.%d",
             (int)((ip >> 24) & 0xFF), (int)((ip >> 16) & 0xFF), (int)((ip >> 8) & 0xFF), (int)(ip & 0xFF));
    return ESP_OK;
}

esp_err_t dns_captive_stop(void)
{
    if (!s_running || s_dns_pcb == nullptr) {
        return ESP_OK;
    }

    udp_remove(s_dns_pcb);
    s_dns_pcb = nullptr;
    s_running = false;

    ESP_LOGI(TAG, "Captive DNS server stopped");
    return ESP_OK;
}
