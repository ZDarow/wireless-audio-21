#ifndef MDNS_RAW_H
#define MDNS_RAW_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация mDNS отвечателя на сыром UDP (порт 5353, multicast 224.0.0.251).
// hostname — имя хоста (без .local, например "audio-master").
// netif — esp_netif интерфейс (softAP).
esp_err_t mdns_raw_init(esp_netif_t* netif, const char* hostname);

// Деинициализация mDNS.
esp_err_t mdns_raw_deinit(void);

// Добавить сервис (например, "_http._tcp.local").
// proto — "tcp" или "udp".
esp_err_t mdns_raw_add_service(const char* service,
                               const char* proto, uint16_t port,
                               const char* instance_name);

#ifdef __cplusplus
}
#endif

#endif // MDNS_RAW_H
