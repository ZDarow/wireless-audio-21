# Глубокий аудит кодовой базы wireless-audio-21

**Дата:** 23.11.2026  
**Ветка:** `feature/esp32-idf-migration`  
**Платформа:** ESP-IDF v5.3.1, ESP32-S3  
**Статус:** Окончательный отчёт

---

## 1. Перечень проблем с файлами, строками и критичностью

### 🔴 КРИТИЧНЫЕ (блокируют безопасность/работу)

| ID | Файл | Строка | Проблема | Критичность | Доказательство |
|----|------|--------|----------|-------------|----------------|
| P1 | `components/udp_audio/src/udp_audio.cpp` | 135-139 | **Неверный HMAC-покрываемый диапазон**. `hmacCoverLen = 8 + (payloadLen - 32)` вычитает 32 байта из payloadLen, хотя HMAC должен вычисляться над magic(2)+sequence(2)+timestampSamples(4)+payload(payloadLen) = `8 + payloadLen`. Данные по смещению 8 в буфере — это поле hmac самого заголовка, а не payload. Валидные пакеты будут отвергаться, невалидные могут просочиться. | 🔴 Критично | Код: `size_t hmacCoverLen = sizeof(UdpAudioHeader) - sizeof(hdr->hmac) + payloadLen - 32;` followed by `verifyHmac(m_rxBuffer, hmacCoverLen, hdr->hmac)` |
| P2 | `components/web_server/src/web_server.cpp` | 346-386 | **OTA по HTTP без подписи и без CSRF**. Endpoint `POST /api/system/ota` принимает URL из тела запроса, не проверяет Origin/Referer, не требует аутентификации, не проверяет подпись прошивки. Атакующий может загрузить прошивку с произвольного HTTP-сервера (RCE через подмену прошивки). | 🔴 Критично | `onPostOta()` отправляет `{"ok\":true,\"ota\":true}` перед началом OTA, затем вызывает `ota_update_start(url)` без любой проверки |
| P3 | `components/web_server/src/web_server.cpp` | все | **Отсутствие CSRF-защиты на всех state-changing endpoints**. `/api/volume`, `/api/crossover`, `/api/system/reboot`, `/api/system/factory_reset`, `/api/system/ota`, `/api/wifi` — все принимают POST без CSRF-токена. Браузер посетителя вредоносного сайта может выполнить действия от имени пользователя. | 🔴 Критично | Никаких токенов, проверок Origin/Referer, SameSite куки. |
| P4 | `components/web_server/src/web_server.cpp` | 487-492 | **`Access-Control-Allow-Origin: *` на всех JSON ответах**. Разрешает кросс-доменные запросы с любого источника, усугубляя CSRF и позволяя вредоносным сайтам читать ответы API. | 🔴 Критично | `httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");` |
| P5 | `components/espnow_transport/src/espnow_transport.cpp` | 38-44 | **ESP-NOW receive callback не вызывает m_rxCb**. Лямбда в `esp_now_register_recv_cb` только логирует, но никогда не вызывает `m_rxCb->onPacket()`. Сателлиты не получают аудио, heartbeat не обрабатывается. | 🔴 Критично | Лямбда на строках 38-44 содержит только `ESP_LOGD`, нет вызова `m_rxCb` |
| P6 | `firmware/master_s3_idf/CMakeLists.txt` | 7 | **Пустая `sdkconfig.defaults` содержит только `CONFIG_LWIP_MDNS_RESPONDER=y`**, но этот параметр не существует в Kconfig v5.3.1 (проверено: `grep LWIP_MDNS_RESPONDER components/lwip/Kconfig` — пусто). Параметр игнорируется, collect2 ошибок при линковке не было, но настройки не применяются. | 🔴 Критично | `sdkconfig.defaults` содержит несуществующий ключ |
| P7 | `components/mdns_raw/src/mdns_raw.cpp` | 351 | **Неверный аргумент `igmp_joingroup`**. `ip_2_ip4(&ip_addr_any)` — `ip_addr_any` является `const ip_addr_t*`, но функция ожидает `const ip4_addr_t*`. Типизация не соответствует API lwip v5.3.1. | 🔴 Критично | `err = igmp_joingroup(ip_2_ip4(&ip_addr_any), &multicastAddr);` |
| P8 | `components/espnow_transport/src/espnow_transport.cpp` | 82-92 | **Добавление ESP-NOW peer при каждом вызове sendUnicast**. `esp_now_add_peer` вызывается на каждой отправке, хотя пир уже может существовать. Обработка `ESP_ERR_ESPNOW_EXIST` есть, но нет очистки пиров — накапливаются записи в таблице пиров (ESP-NOW лимит ~20 пиров). | 🔴 Критично | `sendUnicast` добавляет peer на строке 82-92 при каждом вызове |
| P9 | `firmware/master_s3_idf/partitions.csv` | 1-7 | **Отсутствует раздел `nvs_key`**. Для NVS с ключами (шифрование, пароли) требуется отдельный раздел `nvs_keys`. Без него зашифрованные NVS записи могут не сохраняться. | 🔴 Критично | Нет строки `nvs_key, data, nvs_keys, , 0x1000` |

---

### 🟠 ВЫСОКИЕ (уязвимости, риски логики)

| ID | Файл | Строка | Проблема | Критичность | Доказательство |
|----|------|--------|----------|-------------|----------------|
| P10 | `firmware/master_s3_idf/main/main.cpp` | 446 | **UDP-аутентификация отключена по умолчанию**. `g_cfg.udpAuthKey[0]` — пустая строка по умолчанию, поэтому `authKey = nullptr`, `m_authEnabled = false`, принимаются все пакеты `0xA210` без проверки. Уязвимость V4 (UDP-аудио без аутентификации источника). | 🟠 Высоко | `g_cfg.udpAuthKey[0] ? g_cfg.udpAuthKey : nullptr` |
| P11 | `firmware/master_s3_idf/main/main.cpp` | 241 | **I2S write только 1 семпла за вызов**. `g_i2sOut.write(outBuf, 1, 10)` передаёт 2 байта в DMA. PCM5102A ожидает полные I2S-фреймы. Одиночная запись неэффективна и может вызывать щелчки/заикания аудио. | 🟠 Высоко | `int16_t outBuf[1] = { subSample }; g_i2sOut.write(outBuf, 1, 10);` |
| P12 | `firmware/master_s3_idf/main/main.cpp` | 472 | **Блокирующий UDP receive в цикле 100 Гц**. `g_udpAudio.receive(10, ...)` может блокировать до 10 мс. При 100 Гц цикле (10 мс) суммарное время может достигать 20 мс, вызывая пропуск тактов и нарушение тайминга аудио. | 🟠 Высоко | `g_udpAudio.receive(10, &g_udpHandler);` в цикле `while(1)` |
| P13 | `firmware/master_s3_idf/main/main.cpp` | 468-482 | **Отсутствие watchdog сброса в основном цикле**. Код заявляет стабильность без WDT, но если `g_udpAudio.receive()` заблокируется, устройство зависнет без возможности автоматического восстановления. | 🟠 Высоко | Нет `esp_task_wdt_reset()` или équвалента в цикле |
| P14 | `firmware/master_s3_idf/main/main.cpp` | 292-357 | **`wifi_reconfigure()` не synchronised с основным циклом**. Во время переконфигурации Wi-Fi основной цикл продолжает вызывать `g_udpAudio.receive()` и ESP-NOW операции, что может привести к race condition при остановке/запуске Wi-Fi. | 🟠 Высоко | Нет мьютекса или флага `g_wifi_reconfiguring` |
| P15 | `components/web_server/src/web_server.cpp` | 462 | **SSE соединение удерживается 30 секунд фиксированно**. `vTaskDelay(pdMS_TO_TICKS(30000))` блокирует задачу HTTP сервера на 30 секунд, не обрабатывая другие запросы в это время. | 🟠 Высоко | `vTaskDelay(pdMS_TO_TICKS(30000));` в `onSSE()` |
| P16 | `components/mdns_raw/src/mdns_raw.cpp` | 26 | **Неиспользуемая константа `MDNS_MULTICAST_IP`**. Определена но не используется; код использует `IP4_ADDR()` вместо неё. Мертвый код. | 🟠 Высоко | `#define MDNS_MULTICAST_IP 0xE00000FB` нигде не используется |
| P17 | `components/mdns_raw/src/mdns_raw.cpp` | 243 | **`ancount = 2` но отправляются только PTR + SRV, не TXT**. Комментарий говорит "TXT можно добавить позже", но ancount уже равен 2. Если клиент ждёт 3 записи, получит неполный ответ. | 🟠 Высоко | `hdr2.ancount = htons(2);` с комментарием "PTR + SRV (TXT можно добавить позже)" |
| P18 | `components/dns_captive/src/dns_captive.cpp` | 56-64 | **Отсутствует проверка типа DNS-запроса**. Отвечает A-записью на любой запрос (включая MX, TXT, AAAA). Некоторые клиенты могут получить некорректные ответы. | 🟠 Высоко | Нет проверки `hdr->questions` или типа запроса |
| P19 | `components/nvs_config/src/nvs_config.cpp` | 87-91 | **Дефолтный пароль AP "1234567890" в коде**. Если NVS пуст, используется жёстко заданный слабый пароль. Уязвимость S-1 (дефолтный AP-пароль). | 🟠 Высоко | `strcpy(cfg.wifiApPassword, "1234567890");` |

---

### 🟡 СРЕДНИЕ (логика, антипаттерны, качество кода)

| ID | Файл | Строка | Проблема | Критичность | Доказательство |
|----|------|--------|----------|-------------|----------------|
| P20 | `firmware/master_s3_idf/main/main.cpp` | 363-368 | **Двойная инициализация NVS**. `nvs_flash_init()` вызывается и в `app_main()` (стр. 362), и в `g_nvsConfig.init()` (стр. 371, вызывает `nvs_flash_init()` снова). Вторй вызов вернёт `ESP_ERR_NVS_NO_FREE_PAGES` или уже инициализировано, но это избыточно. | 🟡 Средне | Двойной вызов `nvs_flash_init()` |
| P21 | `firmware/master_s3_idf/main/main.cpp` | 475-478 | **Лог статуса каждые 100 итераций ≈ каждую секунду**. При 100 Гц цикле логирование каждую секунду создаёт лишнюю нагрузку на UART и может влиять на timing. | 🟡 Средне | `if (loopCounter % 100 == 0)` — каждую секунду |
| P22 | `components/nvs_config/src/nvs_config.cpp` | 69-75 | **Отсутствие валидации диапазонов при загрузке из NVS**. Загруженные `volume` (0..100) и `crossoverHz` (70..120) не проверяются на соответствие допустимым диапазонам. Повреждённый NVS может задать некорректные значения. | 🟡 Средне | `cfg.volume = (int)vol;` без проверки диапазона |
| P23 | `components/nvs_config/src/nvs_config.cpp` | 125-183 | **Частая запись в NVS изнашивает flash**. `save()` вызывается при каждом изменении громкости/кроссовера/настроек Wi-Fi. NVS имеет ограниченное количество циклов записи (~100k). Нет кэширования или дебаунса. | 🟡 Средне | `g_nvsConfig.save(g_cfg)` в `setVolumeCallback`, `setCrossoverHzCallback`, `setWifiConfigCallback` |
| P24 | `components/espnow_transport/src/espnow_transport.cpp` | 130-153 | **`updateSatellite()` не удаляет оффлайн сателлиты**. Нет таймаута — сателлит остается "online" навсегда после первого обнаружения, даже если отключён. | 🟡 Средне | Нет проверки `lastSeenMs` или таймаута отключения |
| P25 | `firmware/master_s3_idf/components/mdns_raw/src/mdns_raw.cpp` | 112-167 | **Отсутствует TTL в исходящих mDNS пакетах**. UDP сокет не имеет установленного IP_MULTICAST_TTL, пакеты могут не пройти через маршрутизаторы или не достичь клиентов в разных сегментах. | 🟡 Средне | Нет `setsockopt` с `IP_MULTICAST_TTL` |
| P26 | `components/mdns_raw/src/mdns_raw.cpp` | 82-109 | **Декодирование DNS имён не поддерживает сжатие корректно**. При встрече сжатой ссылки (0xC0xx) функция просто прерывается (`break`), не рекурсивно декодируя. Может привести к некорректному разбору запросов. | 🟡 Средне | `if ((len & 0xC0) == 0xC0) { ... break; }` |
| P27 | `firmware/master_s3_idf/components/web_server/include/web_server.h` | 49-90 | **Отсутствие деструкторной очистки SSE клиентов**. `s_sseServer` статический, но нет списка активных SSE сессий для broadcast. `sseBroadcast()` всегда возвращает ESP_OK без реальной отправки. | 🟡 Средне | `sseBroadcast()` — заглушка |
| P28 | `firmware/master_s3_idf/components/i2s_output/src/i2s_output.cpp` | 93-113 | **`i2s_channel_write()` блокирующий вызов в аудио-коллбеке**. `UdpAudioHandler::onPcm` вызывается из контекста приёма UDP, и запись в I2S может заблокировать на `timeoutMs=10`, задерживая обработку следующего аудио-пакета. | 🟡 Средне | `g_i2sOut.write(outBuf, 1, 10);` в коллбеке приёма UDP |
| P29 | `firmware/master_s3_idf/components/nvs_config/include/nvs_config.h` | 41-42 | **Дефолтные учётные данные Wi-Fi в заголовочном файле**. `wifiApPassword = "1234567890"` и `wifiApSsid = "Audio21-Master"` в коде — уязвимость при коммите в репозиторий. | 🟡 Средне | Зашитые в коде учётные данные |

---

### 🟢 НИЗКИЕ (стиль, документация, улучшения)

| ID | Файл | Строка | Проблема | Критичность | Доказательство |
|----|------|--------|----------|-------------|----------------|
| P30 | `components/mdns_raw/src/mdns_raw.cpp` | 349-350 | **`IP4_ADDR(&multicastAddr, ...)` вместо использования `MDNS_MULTICAST_IP`**. Дублирование логики. | 🟢 Низко | Константа определена на стр. 26, не используется |
| P31 | `components/mdns_raw/src/mdns_raw.cpp` | 319-380 | **`mdns_raw_deinit()` не покидает multicast группу**. `igmp_leavegroup` не вызывается, ресурсы lwip не освобождаются полностью. | 🟢 Низко | Нет вызова `igmp_leavegroup` |
| P32 | `firmware/master_s3_idf/main/main.cpp` | 222-245 | **`UdpAudioHandler::onPcm()` обрабатывает семплы по одному**. Неэффективно для DSP конвейера, лучше обрабатывать батчами. | 🟢 Низко | Цикл `for (size_t i = 0; i < nSamplesPerChannel; i++)` с вызовом `g_pipeline.process()` на каждой итерации |
| P33 | `components/web_server/src/web_server.cpp` | 466-483 | **HTML-страница корня жёстко вшита в C++ строку**. Трудно поддерживать, нет разделения логики и представления. | 🟢 Низко | `const char* html = "..."` |

---

### 📋 Известные уязвимости в зависимостях (из веб-поиска)

| ID | Компонент | Версия | Проблема | CVE | Критичность |
|----|-----------|--------|----------|-----|-------------|
| P34 | `esp_http_server` (ESP-IDF v5.3.1) | v5.3.1 | NULL-pointer dereference в WebSocket subprotocol-negoсiации (достижимо только при использовании WebSocket handlers) | CVE-2026-45541 / GHSA-3j8v-xgrq-5vg8 | 🟠 Высоко (доступно исправление в v5.3.6+) |
| P35 | `esp-idf` (v5.3.1) | v5.3.1 | mDNS компонент выделен в отдельный репозиторий с v5.0. Наш `mdns_raw` — обходной путь, но официальный компонент `espressif/mdns` надёжнее. | — | 🟡 Средне |
| P36 | `esp_http_server` CSRF | — | Все state-changing endpoints без CSRF защиты. Аналогично CVE-2025-53540 (Arduino-ESP32 OTA CSRF → RCE). | — | 🔴 Критично (см. P3) |

---

## 2. Оценка архитектуры

### Сильные стороны

1. **Чистое разделение на компоненты ESP-IDF** — каждая функциональность (DSP, ESP-NOW, UDP audio, Web server, I2S, NVS, DNS captive, mDNS, OTA) вынесена в отдельный компонент с чёткими границами.

2. **Header-only DSP модули** — DSP конвейер не зависит от железа, что позволяет запускать хост-тесты на ПК без ESP32.

3. **100 Гц главный цикл с `vTaskDelayUntil()`** — детерминированное время цикла, стабильная работа без TG1WDT перезагрузок (подтверждено 24+ сек uptime).

4. **NVS конфигурация с версионированием** — `kConfigVersion=1` позволяет миграцию схемы при изменении структуры `NodeConfig`.

5. **HMAC-SHA256 для UDP-аудио** — частично реализовано (V4), поддерживает оба магических числа (0xA210/0xA211).

6. **mDNS через сырой UDP** — рабочий обходной путь для отсутствия официального mDNS компонента в ESP-IDF v5.3.1.

### Архитектурные слабости

1. **Отсутствие уровня аутентификации Web UI**. Весь веб-интерфейс открыт без авторизации. Для устройства, которое создаёт точку доступа Wi-Fi с паролем "1234567890" по умолчанию, любой подключившийся к AP получает полный контроль над устройством.

2. **Монотонный главный цикл блокирует всё**. UDP receive (до 10 мс), I2S write (до 10 мс), веб-сервер запросы — всё в одном потоке. Долгая операция в любом из компонентов блокирует остальные.

3. **Нет разделения ответственности между сетевым стеком и аудио конвейером**. `main.cpp` смешивает инициализацию Wi-Fi, ESP-NOW, I2S, UDP аудио, веб-сервера и цикл обработки аудио. Нарушение SRP (см. P14 в TASKS.md).

4. **Отсутствие обработки превышения лимитов ESP-NOW пиров**. `sendUnicast()` добавляет пир при каждом вызове, нет механизма удаления неактивных пиров.

5. **Нет gracefully degradation**. Если UDP аудио перестаёт работать, нет fallback-а или уведомления. Если I2S умирает, устройство продолжает работать как если бы ничего не случилось.

6. **Разные форматы пакетов для разных направлений**. Мастер→сателлит (16-байт заголовок, 0xA210/0xA211/0xA212) и смартфон→мастер (40-байт заголовок, 0xA210/0xA211) используют одинаковые магические числа для разных целей — потенциальная путаница при отладке.

### Соответствие спецификации ТЗ

| Спецификация § | Статус | Комментарий |
|----------------|--------|-------------|
| §6.8 Формат пакета мастер→сателлит | ✅ | Реализовано верно |
| §9.1 Формат пакета смартфон→мастер | ✅ | Реализовано верно |
| §10.1 timestampMs в пакетах | ⚠️ Частично | В ESP-NOW пакетах есть `timestampMs`, в UDP — `timestampSamples` |
| §16.3 Watchdog | ❌ | Нет WDT сброса в цикле |
| §17 MAC-фильтр/проверка источника UDP | ❌ | HMAC частично реализовано (V4), но ключ пуст по умолчанию |
| §18 Ручная проверка звука | ❌ | Не проведено |
| §23.2 Доступ только из локальной подсети | ❌ | Нет проверки `clientIsLocal` |
| C5.7 Аутентификация источника UDP | ⚠️ Частично | HMAC есть, но ключ не настроен по умолчанию |

---

## 3. Готовые патчи

### Патч 1: Исправление HMAC в udp_audio.cpp (КРИТИЧНО, P1)

```cpp
// В файле components/udp_audio/src/udp_audio.cpp, функция receive(), строки 133-139

// БЫЛО:
    size_t hmacDataLen = sizeof(UdpAudioHeader) - sizeof(hdr->hmac) + payloadLen - 32;
    // На самом деле структура: magic(2) + sequence(2) + timestampSamples(4) + hmac(32) + payload
    // HMAC считается над magic+sequence+timestampSamples+payload
    size_t hmacCoverLen = 8 + (payloadLen - 32);
    authOk = verifyHmac(m_rxBuffer, hmacCoverLen, hdr->hmac);

// СТАЛО:
// HMAC считается над: magic(2) + sequence(2) + timestampSamples(4) + payload(payloadLen)
// Данные лежат в буфере как: [magic(2)][seq(2)][ts(4)][hmac(32)][payload(N)]
// Нужно скопировать magic+seq+ts и payload в буфер, пропуская поле hmac
    uint8_t hmacData[1200];  // max payload 1200 + 8 header bytes
    size_t hmacDataLen = 0;
    memcpy(hmacData + hmacDataLen, m_rxBuffer, 8);           // magic + sequence + timestampSamples
    hmacDataLen += 8;
    memcpy(hmacData + hmacDataLen, m_rxBuffer + sizeof(UdpAudioHeader), payloadLen);  // payload
    hmacDataLen += payloadLen;
    authOk = verifyHmac(hmacData, hmacDataLen, hdr->hmac);
```

**Почему:** Текущий код вычисляет HMAC над буфером, включающим само поле hmac (смещение 8-39) и часть payload. Это делает аутентификацию неработоспособной — валидные пакеты отвергаются, невалидные могут пройти.

---

### Патч 2: CSRF-защита для веб-API (КРИТИЧНО, P3, P4)

```cpp
// В файле components/web_server/include/web_server.h, добавить в WebServerCallbacks:

// CSRF токен для проверки state-changing запросов
const char* (*getCsrfToken)(void);
bool (*validateCsrfToken)(const char* token);

// В WebServer добавить поле:
std::string m_csrfToken;

// В init() сгенерировать токен:
#include <esp_random.h>
uint32_t random_val;
esp_fill_random(&random_val, sizeof(random_val));
char csrf_buf[16];
snprintf(csrf_buf, sizeof(csrf_buf), "%08x", random_val);
m_csrfToken = csrf_buf;

// В onPostOta, onPostVolume, onPostCrossover, onPostReboot, onPostFactoryReset, onPostWifi
// Добавить проверку CSRF перед выполнением действия:
    const char* csrfHeader = httpd_req_get_hdr_value_str(req, "X-CSRF-Token", nullptr, 0);
    if (!csrfHeader || m_csrfToken != csrfHeader) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "CSRF token missing or invalid");
        return ESP_FAIL;
    }

// В sendJson() убрать wildcard CORS:
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "null");
// или лучше убрать заголовок полностью для API
```

**Почему:** Без CSRF защиты любой посетитель вредоносного сайта может выполнить действия на устройстве (перезагрузка, сброс настроек, изменение громкости, OTA с произвольного URL).

---

### Патч 3: HTTPS + подпись OTA (КРИТИЧНО, P2)

```cpp
// В файле components/ota_update/src/ota_update.cpp

#include "esp_crt_bundle.h"

esp_err_t ota_update_start(const char *url)
{
    if (url == nullptr) {
        ESP_LOGE(TAG, "OTA URL is null");
        return ESP_ERR_INVALID_ARG;
    }

    // Валидация URL — только HTTPS
    if (strncmp(url, "https://", 8) != 0) {
        ESP_LOGE(TAG, "OTA URL must use HTTPS");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting OTA update from: %s", url);

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,  // Использовать набор доверенных корневых сертификатов
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        // В будущем: добавить проверку подписи через esp_ota_set_boot_partition_skip_validate
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful, marking as valid and rebooting...");
        esp_ota_mark_app_valid_cancel_rollback();
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ret));
    }

    return ret;
}
```

**Почему:** Текущий код использует HTTP (перехват трафика), не проверяет сертификат сервера, не проверяет подпись прошивки. Атакующий может выполнить MITM и загрузить вредоносную прошивку.

---

### Патч 4: I2S батчевая запись (ВЫСОКО, P11)

```cpp
// В файле firmware/master_s3_idf/main/main.cpp, класс UdpAudioHandler

class UdpAudioHandler : public UdpAudioRxCallback {
public:
    void onPcm(const int16_t* data, size_t nSamplesPerChannel) override
    {
        // Обрабатываем стерео PCM через DSP конвейер батчем
        int16_t outBuf[128];  // батч для I2S
        size_t outIdx = 0;

        for (size_t i = 0; i < nSamplesPerChannel; i++) {
            int16_t l = data[i * 2];
            int16_t r = data[i * 2 + 1];

            PipelineOutput out = g_pipeline.process(l, r);

            // Сабвуфер — моно, через DelayLine
            int16_t subSample = static_cast<int16_t>(out.sub * 32767.0f);
            subSample = g_delaySub.process(subSample);

            outBuf[outIdx++] = subSample;

            // Пишем батч когда накопилось 128 семплов или конец входных данных
            if (outIdx >= 128 || i == nSamplesPerChannel - 1) {
                if (g_i2sOn && outIdx > 0) {
                    g_i2sOut.write(outBuf, outIdx, 100);
                }
                outIdx = 0;
            }
        }
    }
};
```

**Почему:** Запись 1 семпла (2 байта) за вызов неэффективна для DMA и может вызывать аудио-артефакты. Батчевая запись снижает нагрузку на I2S драйвер.

---

### Патч 5: Валидация диапазонов NVS (СРЕДНЕ, P22)

```cpp
// В файле components/nvs_config/src/nvs_config.cpp, функция load()

    // Читаем все поля с валидацией диапазонов
    int32_t vol = 100;
    ret = nvs_get_i32(m_handle, kKeyVolume, &vol);
    if (ret == ESP_OK) {
        cfg.volume = (int)vol;
        if (cfg.volume < 0 || cfg.volume > 100) cfg.volume = 100;  // clamp
    }

    int32_t xover = 90;
    ret = nvs_get_i32(m_handle, kKeyCrossoverHz, &xover);
    if (ret == ESP_OK) {
        cfg.crossoverHz = (int)xover;
        if (cfg.crossoverHz < 40 || cfg.crossoverHz > 500) cfg.crossoverHz = 90;  // clamp
    }
```

**Почему:** Повреждённые значения в NVS могут выйти за допустимые диапазоны и вызвать некорректную работу DSP.

---

### Патч 6: Watchdog в основном цикле (ВЫСОКО, P13)

```cpp
// В файле firmware/master_s3_idf/main/main.cpp, после инициализации:

#include "esp_task_wdt.h"

// В app_main() перед основным циклом:
    esp_task_wdt_init(30, false);  // 30 секунд таймаут, не паникуем при срабатывании

// В основном цикле:
    while (1) {
        // ... существующий код ...
        esp_task_wdt_reset();  // Сброс watchdog на каждой итерации
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(10));
    }
```

**Почему:** Без сброса WDT зависание в любом компоненте приведёт к бесконечному зависанию устройства без возможности восстановления.

---

### Патч 7: Исправление mDNS multicast (КРИТИЧНО, P7)

```cpp
// В файле components/mdns_raw/src/mdns_raw.cpp, функция mdns_raw_init(), строка 349-351

// БЫЛО:
    ip4_addr_t multicastAddr;
    IP4_ADDR(&multicastAddr, 224, 0, 0, 251);
    err = igmp_joingroup(ip_2_ip4(&ip_addr_any), &multicastAddr);

// СТАЛО:
    ip4_addr_t multicastAddr;
    IP4_ADDR(&multicastAddr, 224, 0, 0, 251);
    err = igmp_joingroup(IP4_ADDR_ANY, &multicastAddr);
    // Установить TTL для 멀тикаст пакетов
    uint8_t mcast_ttl = 1;
    setsockopt_raw(s_pcb, IPPROTO_IP, IP_MULTICAST_TTL, &mcast_ttl, sizeof(mcast_ttl));
```

**Почему:** `ip_2_ip4(&ip_addr_any)` передает указатель на `ip_addr_t` вместо `ip4_addr_t`, что вызывает несоответствие типов в lwip v5.3.1.

---

### Патч 8: Удаление ESP-NOW пиров (ВЫСОКО, P8)

```cpp
// В файле components/espnow_transport/src/espnow_transport.cpp, добавить метод:

esp_err_t EspNowTransport::removePeer(const uint8_t* dstMac)
{
    if (!m_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = esp_now_del_peer(dstMac);
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_NOT_FOUND) {
        ESP_LOGE(TAG, "esp_now_del_peer failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

// В sendUnicast(), добавить проверку лимита пиров перед добавлением:
// Если пиров слишком много, удалить старейший неактивный
```

**Почется:** Без удаления пиров таблица ESP-NOW заполняется (лимит ~20), и новые сателлиты не могут быть добавлены.

---

## 4. Приоритетный план исправлений

### Неделю 1 — Критичные (блокируют безопасность и работу)

| Приоритет | ID | Задача | Файлы | Ожидаемый результат |
|-----------|----|--------|-------|---------------------|
| 1 | P1 | Исправить HMAC покрываемый диапазон | `components/udp_audio/src/udp_audio.cpp` | HMAC аутентификация работает корректно |
| 2 | P3 | Добавить CSRF-защиту на все POST endpoints | `components/web_server/` | Защита от межсайтовой подделки запросов |
| 3 | P4 | Убрать `Access-Control-Allow-Origin: *` | `components/web_server/src/web_server.cpp` | Защита от кросс-доменных запросов |
| 4 | P2 | Перевести OTA на HTTPS + проверка подписи | `components/ota_update/src/ota_update.cpp` | Защита от подмены прошивки |
| 5 | P5 | Исправить ESP-NOW receive callback | `components/espnow_transport/src/espnow_transport.cpp` | Аудио и heartbeat от сателлитов работают |
| 6 | P7 | Исправить `igmp_joindroup` аргумент | `components/mdns_raw/src/mdns_raw.cpp` | mDNS multicast работает корректно |
| 7 | P8 | Добавить удаление ESP-NOW пиров | `components/espnow_transport/src/espnow_transport.cpp` | Таблица пиров не переполняется |

### Неделю 2 — Высокие (уязвимости и риски логики)

| Приоритет | ID | Задача | Файлы | Ожидаемый результат |
|-----------|----|--------|-------|---------------------|
| 8 | P10 | Настроить дефолтный UDP auth key | `firmware/master_s3_idf/main/main.cpp`, `components/nvs_config/include/nvs_config.h` | UDP-аудио аутентифицировано по умолчанию |
| 9 | P11 | Батчевая запись в I2S | `firmware/master_s3_idf/main/main.cpp` | Эффективный вывод аудио без щелчков |
| 10 | P12 | Неблокирующий UDP receive или отдельная задача | `firmware/master_s3_idf/main/main.cpp` | Цикл 100 Гц не блокируется на приём UDP |
| 11 | P13 | Добавить watchdog сброс | `firmware/master_s3_idf/main/main.cpp` | Автовосстановление после зависания |
| 14 | P14 | Синхронизация Wi-Fi переконфигурации | `firmware/master_s3_idf/main/main.cpp` | Нет race condition при изменении Wi-Fi |
| 15 | P15 | Исправить SSE блокировку 30 секунд | `components/web_server/src/web_server.cpp` | Веб-сервер отвечает на запросы во время SSE |

### Неделю 3 — Средние (качество и надёжность)

| Приоритет | ID | Задача | Файлы | Ожидаемый результат |
|-----------|----|--------|-------|---------------------|
| 16 | P22 | Валидация диапазонов NVS | `components/nvs_config/src/nvs_config.cpp` | Защита от повреждённых значений в NVS |
| 17 | P23 | Дебаунс/кеширование записи NVS | `components/nvs_config/src/nvs_config.cpp` | Снижение износа flash при частых сохранениях |
| 18 | P24 | Таймаут оффлайн сателлитов | `components/espnow_transport/src/espnow_transport.cpp` | Корректный статус онлайн/оффлайн |
| 19 | P26 | Исправить декодирование DNS имён | `components/mdns_raw/src/mdns_raw.cpp` | Корректный разбор всех DNS запросов |
| 20 | P28 | Неблокирующая запись I2S | `components/i2s_output/src/i2s_output.cpp` | Не блокирует обработку аудио коллбеком |

### Неделю 4 — Низкие и верификация

| Приоритет | ID | Задача | Файлы | Ожидаемый результат |
|-----------|----|--------|-------|---------------------|
| 21 | P31 | `igmp_leavegroup` в `mdns_raw_deinit()` | `components/mdns_raw/src/mdns_raw.cpp` | Корректная очистка ресурсов |
| 22 | P32 | Батчевая обработка DSP | `firmware/master_s3_idf/main/main.cpp` | Более эффективный аудио конвейер |
| 23 | P9 | Добавить `nvs_key` раздел | `firmware/master_s3_idf/partitions.csv` | Поддержка зашифрованных NVS ключей |
| 24 | P1 | Обновить ESP-IDF с v5.3.1 на v5.3.6+ | `sdkconfig`, `.pio-core-master/` | Исправление CVE-2026-45541 (WebSocket NULL deref) |
| 25 | E16 | Ручное тестирование звука на железе | Железо + смартфон | Подтверждение работоспособности аудио конвейера |

---

## 5. Ответы на ключевые вопросы из веб-поиска

### Вопрос: mDNS в ESP-IDF v5.3.1
**Ответ:** Начиная с ESP-IDF v5.0, компонент mDNS выделен в отдельный репозиторий `espressif/mdns`. Добавить можно через `idf.py add-dependency espressif/mdns`. Наш кастомный `mdns_raw` компонент — рабочий обходной путь, но для продакшена рекомендуется перейти на официальный компонент.

### Вопрос: OTA безопасность
**Ответ:** Espressif рекомендует стек: Secure Boot v2 (подпись образов) + HTTPS (защита транзита) + Flash Encryption (данные на часто). Наш код использует HTTP без подписи — критическая уязвимость (V5).

### Вопрос: CVE-2026-45541 (esp_http_server NULL deref)
**Ответ:** Затрагивает v5.3.x включая v5.3.1. Исправлено в v5.3.6+. Наш веб-сервер не использует WebSocket, поэтому уязвимость не достижима в текущей конфигурации, но обновление на v5.3.6+ рекомендуется.

### Вопрос: I2S однократная запись семпла
**Ответ:** ESP32-S3 I2S использует GDMA без CPU FIFO. Запись малых буферов (1 семпл = 2 байта) неэффективна и может приводить к артефактам. Рекомендуется батчевая запись (min 64-128 семплов).

### Вопрос: I2S DMA портит операции AES/SHA
**Ответ:** Известная проблема ESP32-S3 (GitHub issue #18640): после использования I2S TX с GDMA, последующие операции mbedtls с аппаратным ускорением могут давать неверные результаты. Если в будущем будет использоваться шифрование ESP-NOW вместе с I2S, это может стать проблемой.

---

## Итог

**Критичных проблем:** 9 (P1-P9)  
**Высоких:** 10 (P10-P19)  
**Средних:** 10 (P20-P29)  
**Низких:** 4 (P30-P33)  
**Уязвимости в зависимостях:** 3 (P34-P36)

**Общая оценка:** Кодовая база требует существенных доработок безопасности перед внедрением на продакшен устройства. Критически важно исправить P1 (HMAC), P2 (OTA без подписи), P3 (CSRF), P5 (ESP-NOW callback), P7 (mDNS multicast) в первую очередь.
