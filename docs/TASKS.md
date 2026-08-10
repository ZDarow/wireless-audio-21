# Wireless Audio 2.1 — Файл задач

> Список задач по итогам проверки проекта (10.08.2026) и план дальнейших
> итераций. Живой документ: статусы обновляются по мере выполнения.
> Формат задач: `T<номер>` — техдолг/правки, `F<номер>` — функциональность
> (совпадает с нумерацией требований в `docs/PLAN.md`), `B<номер>` — баги по
> итогам ревизии кода.

---

## 1. Срочно / блокеры

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| T1 | **CI не запускается**: на GitHub 0 runs при активном workflow и включённых Actions. Вероятно, Actions не активированы в настройках репозитория (Settings → Actions → Enable). Плюс в `ci.yml` нет `workflow_dispatch` для ручного запуска | `.github/workflows/ci.yml` | Высокий | ⬜ |

---

## 2. Техдолг и мелкие правки

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| T2 | Docstring `generate_config.py` упоминает флаг `--out`, фактический интерфейс — позиционный аргумент. Привести справку/docstring в соответствие с реальным CLI | `scripts/generate_config.py` | Низкий | ✅ |
| T3 | Расхождение со спецификацией §6.4: REST использует POST вместо PUT для volume/crossover/delay. Решить: привести к PUT или зафиксировать POST в документации | `firmware/master/include/web_server.h` | Низкий | ✅ (приведено к PUT) |
| T4 | `/api/status` отдаёт `left_online`/`right_online` (bool), спецификация §6.4 ожидает `satellites: {left, right}` со значениями «online»/«offline». Привести формат ответа к спецификации (обратная совместимость не нужна — MVP) | `firmware/master/include/web_server.h` | Низкий | ✅ |
| T5 | UDP-транспорт: мастер шлёт broadcast, спецификация §5.6 предполагает UDP unicast по MAC/IP сателлитов. Нужен discovery (сателлиты отвечают на broadcast-запрос, мастер запоминает IP) или явная настройка IP в конфиге | `firmware/common/transport/udp.h`, `firmware/master/src/main.cpp` | Средний | ✅ (discovery: request/response, unicast после запоминания IP) |
| T6 | `config.example.h` — ручная конфигурация; проверить, что она не расходится с `generated_config.h` (макросы дублируются в двух источниках) | `config.example.h`, `firmware/common/generated/generated_config.h` | Низкий | ✅ (23 макроса совпадают, обновлён комментарий) |

---

## 2.1 Баг-лист по итогам ревизии кода (10.08.2026)

| ID | Проблема | Место | Приоритет | Статус |
|---|---|---|---|---|
| B1 | `g_leftOnline`/`g_rightOnline` нигде не выставляются в true: статус сателлитов в `/api/status` и serial всегда «offline». Нет ESP-NOW sent-callback и UDP-сердцебиений | `firmware/master/src/main.cpp:47` | Высокий | ⬜ |
| B2 | Конфликт пинов в `config.example.h`: `AUDIO_I2S_DATA_OUT=22` и `AUDIO_OLED_SCL=22` (актуально при реализации F12) | `config.example.h` | Средний | ⬜ |
| B3 | `UdpTransport::broadcast()` считает broadcast-IP как `~localIP` + `bc[3]=255` — корректен только для /24-подсети | `firmware/common/transport/udp.h:53` | Низкий | ⬜ (связано с T10) |
| B4 | `handleVolume` без поля `volume` в JSON молча ставит громкость 0 вместо ошибки | `firmware/master/include/web_server.h:118` | Низкий | ⬜ |
| B5 | `generate_config.py:133` повторно вызывает `expand(env)` при выводе счётчика макросов (косметика) | `scripts/generate_config.py` | Низкий | ⬜ |
| B6 | Риск нестабильности A2DP + Wi-Fi STA coexistence на ESP32 (нужна проверка на железе) | мастер | Риск | ⬜ |
| B7 | Partition `huge_app.csv` без OTA — осознанное MVP-решение, блокирует будущий OTA | `platformio.ini:25` | Замечание | ⬜ |

---

## 3. Пробелы в тестах

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| T7 | Тест переполнения и дефицита jitter buffer (overwrite старых семплов, pop из пустого буфера) | `test/audio_filter_test.cpp` | Средний | ✅ |
| T8 | Host-тест генератора конфига: `generate_config.py` → проверить все 23 макроса, включая MAC-адреса и строки с кавычками | `test/` (новый `config_gen_test.py`) | Средний | ⬜ |
| T9 | Тест `ConfigStorage` (NVS) — только на железе; добавить round-trip тест в firmware-тестах или документировать ручную проверку | `firmware/common/config/storage.h` | Низкий | ⬜ |
| T10 | Тест UDP broadcast-адресации и `sendTo` (чистая логика выделения broadcast-IP) | `test/transport_packet_test.cpp` | Низкий | ⬜ |

---

## 4. Функциональность (следующие итерации, из PLAN.md)

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| F12 | OLED-меню (SSD1306, U8g2) + энкодер (KY-040): громкость, кроссовер, задержки, статус сателлитов | `firmware/common/ui/display.h`, `encoder.h` | Средний | ⬜ |
| F13 | Wi-Fi UDP источник (мастер принимает поток по UDP вместо A2DP) | `firmware/master/src/main.cpp` | Средний | ⬜ |
| F14 | Синхронизация воспроизведения по `timestampMs` в пакете (компенсация дрейфа часов) | `firmware/common/transport/audio_packet.h`, `firmware/satellite/src/main.cpp` | Средний | ⬜ |
| F15 | Защита от щелчков при включении/остановке: fade-in/out на мастере и сателлитах | `firmware/common/audio/volume_control.h` | Средний | ⬜ |
| F16 | mDNS: доступ к Web UI по `http://audio-master.local` | `firmware/master/src/main.cpp` | Низкий | ⬜ |
| F17 | Документация: `docs/architecture.md`, `docs/hardware.md`, `docs/wiring.md` | `docs/` | Низкий | ✅ |

---

## 5. Статус на момент создания (10.08.2026)

- Проект собран: 3 env PlatformIO — SUCCESS (master 58.4% flash, сателлиты 24.2%).
- Host-тесты: 3 файла, все зелёные (`make test`).
- Генератор конфига: 23 макроса, работает.
- Git: `main` синхронизирован с `origin/main`, артефакты не коммитятся.
- Открытых блокеров нет; единственный срочный пункт — активация CI (T1).

## 6. Выполнено (обновление 10.08.2026)

- **T2–T7** — техдолг закрыт: docstring генератора, REST PUT, формат
  `/api/status` с `satellites`, UDP discovery (unicast), синхронизация
  `config.example.h`, тесты jitter buffer (коммит `a93e202`).
- **F17** — документация: `docs/architecture.md`, `docs/hardware.md`,
  `docs/wiring.md`, ссылки из README.
- **Ревизия кода** — host-тесты проверены на машине разработчика (g++ 14.2,
  все 3 бинаря зелёные), добавлен `AGENTS.md` и баг-лист B1–B7.
- Осталось: T8 (host-тест генератора), T9 (round-trip ConfigStorage),
  T10 (тест broadcast-адресации), F12–F16 (OLED, Wi-Fi источник,
  синхронизация timestampMs, fade, mDNS) + баги B1–B7.