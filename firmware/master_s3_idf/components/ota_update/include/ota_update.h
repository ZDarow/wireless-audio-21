#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Запустить OTA обновление по указанному URL.
// Блокирующий вызов: возвращает ESP_OK при успешном обновении,
// иначе код ошибки ESP-IDF.
// После успешного обновления устройство перезагружается автоматически.
esp_err_t ota_update_start(const char *url);

#ifdef __cplusplus
}
#endif

#endif // OTA_UPDATE_H
