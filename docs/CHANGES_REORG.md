# CHANGES_REORG.md — отчёт о реорганизации репозитория

> Дата: 22.11.2026
> Статус: все изменения в working tree, не закоммичены
> Объём: удаление мёртвого кода, дублей и артефактов; рефакторинг имён;
> добавление линтера; обновление документации.

---

## 1. Удалённые файлы (dead code / дубли / артефакты)

| Файл | Причина удаления |
|------|------------------|
| `config.example.h` | Ручная конфигурация; единственный источник — `config.env` + `node_config.h` (см. T6/T13). Конфликт пинов B2 был здесь. |
| `docs/аудит.txt` | Устаревший отчёт аудита; актуальные задачи — в `docs/TASKS.md` и `docs/REPO_AUDIT.md`. |
| `firmware/idf_component.yml` | Не используется проектом (PlatformIO, не IDF component manager). |
| `firmware/master_s3/include/master_s3_config.h` | Содержал только `kDefaultWifiChannel` — перенесён в `firmware/common/config/node_config.h`. |
| `test/*.bin`, `test/*.bin.d` | Скомпилированные артефакты хост-тестов; не коммитятся (в `.gitignore` не было, удалены вручную). |

## 2. Изменённые файлы

### `firmware/master_s3/src/main.cpp`
- Убран `#include "master_s3_config.h"` (файл удалён).
- `kDefaultWifiChannel` теперь из `node_config.h` (перенесён из удалённого `master_s3_config.h`).
- Includes перегруппированы по категориям: system Arduino → ESP-IDF → FreeRTOS → C stdlib → проект.
- Удалены неиспользуемые includes: `<esp_mac.h>`, `<esp_coexist.h>`, `<stdexcept>`, `<math.h>`.
- Переименован `g_udpBuf` → `g_udpAudioBuffer` (4 вхождения: объявление, `g_udp.read`, `parseUdpPacket`, `s_mono`). Имя было запутанным — неясно, буфер чего (аудио? пакет? raw?).

### `firmware/common/config/node_config.h`
- Добавлен `kDefaultWifiChannel` (из удалённого `master_s3_config.h`).

### `.gitignore`
- Добавлен `.pio-core-master/` (изолированный core-каталог PlatformIO для `master_s3_wifi` на pioarduino core 3.x).

### `.github/workflows/ci.yml`
- Добавлен шаг clang-format dry-run (проверка форматирования C++ без модификации файлов).

### `AGENTS.md`
- Убраны ссылки на удалённые файлы (`config.example.h`, `docs/аудит.txt`, `firmware/master_s3/include/master_s3_config.h`).

### `README.md`
- Убраны ссылки на удалённые файлы.

### `docs/PLAN.md`
- Убраны ссылки на удалённые файлы.

### `docs/architecture.md`
- Убраны ссылки на удалённые файлы.

### `docs/TASKS.md`
- Обновлён (см. раздел 4 ниже).

## 3. Созданные файлы

| Файл | Назначение |
|------|------------|
| `.clang-tidy` | Конфиг линтера clang-tidy: проверки clang-analyzer, cppcoreguidelines, performance, portability, readability, bugprone, modernize с исключениями для embedded (raw pointers, C-style casts, avoiding `std::` в некоторых местах). |
| `docs/EXTERNAL_REFERENCES.md` | Готовые примеры кода, копируемые фрагменты и конфигурации. |
| `docs/INTEGRATION_RECOMMENDATIONS.md` | Пошаговые рекомендации по внедрению изменений. |
| `docs/TECHNICAL_SURVEY.md` | Сравнение внешних решений (транспорты, DSP, OTA, веб, CI/CD). |
| `docs/WORK_PLAN.md` | Детальный план работ на 4 спринта (безопасность, синхронизация, CI/архитектура, функции). |

## 4. Обновлённые задачи в docs/TASKS.md

Закрытые задачи реорганизации:
- **T6** — `config.example.h` удалён; единственный источник конфига — `config.env` + `node_config.h` (макросы не дублируются).
- **T12** — `master_s3_config.h` удалён; `kDefaultWifiChannel` перенесён в `node_config.h`.
- **T16** — `web_server.h` уже в `firmware/common/web/` (сделано ранее); декомпозиция на web-core + audio-хендлеры остаётся в спринте 3 (A1).
- **T22** — запись `WebServer`/`Preferences` в `lib_deps` оставлена без изменений (на pioarduino core 3.x они не встроены в фреймворк, без них сборка падает).

Остающиеся открытые задачи (из WORK_PLAN.md):
- V4 — UDP-аутентификация источника (HMAC-SHA256) — 🔴, 16 ч, ⬜
- V5 — Подпись OTA (Ed25519) — 🔴, 40 ч, ⬜
- S-1 — Forced password change при первом запуске — 🔴, 16 ч, ⬜
- N1 — Активация GitHub Actions — 🟠, 4 ч, ⬜
- A1 — Дekomпозиция `web_server.h` на модули — 🟠, 40 ч, ⬜
- T19 — Миграция сателлитов на Arduino core 3.x — 🟡, ⬜

## 5. Чек-лист предотвращения хаоса (после реорганизации)

- [ ] Конфиг правится только через `config.env` → `scripts/generate_config.py` → `generated_config.h`; ручные правки в `.h` запрещены.
- [ ] Тестовые бинарники (`test/*.bin`, `*.bin.d`) не коммитятся — `.gitignore` проверен.
- [ ] `.pio-core-master/` в `.gitignore` (изолированный core для S3-мастера).
- [ ] Новые документы в `docs/` добавляются только через PR с ревью.
- [ ] `AGENTS.md`/`README.md`/`architecture.md` синхронизированы с кодом после каждого изменения структуры.
- [ ] `docs/TASKS.md` обновляется при закрытии/добавлении задач.
- [ ] Хост-тесты (`make -C test test`) зелёные после любого изменения в `firmware/common`.
- [ ] `clang-format --dry-run` проходит перед коммитом (шаг в CI).
- [ ] `clang-tidy` проверяет новые файлы перед мержем.

## 6. Итоговая статистика изменений

| Категория | Файлов | Строк |
|-----------|--------|-------|
| Удалено | 5 | ~117 |
| Изменено | 8 | +50 / −135 |
| Создано | 5 | ~новые |
| Всего в working tree | 18 | netto −102 строки кода |
