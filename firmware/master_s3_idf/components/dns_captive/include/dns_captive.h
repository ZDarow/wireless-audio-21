#ifndef DNS_CAPTIVE_H
#define DNS_CAPTIVE_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Запустить captive DNS сервер на UDP порту 53.
// Все DNS-запросы будут отвечать A-записью с указанным IP (softAP адрес).
// Возвращает ESP_OK при успехе.
esp_err_t dns_captive_start(uint32_t ip);

// Остановить captive DNS сервер.
esp_err_t dns_captive_stop(void);

#ifdef __cplusplus
}
#endif

#endif // DNS_CAPTIVE_H
