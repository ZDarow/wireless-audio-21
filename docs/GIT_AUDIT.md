# Глубокий аудит Git-репозитория Wireless Audio 2.1

Дата: 12.08.2026
Анализируемый репозиторий: `https://github.com/ZDarow/wireless-audio-21.git` (public)
Анализируемая ревизия: `66b25c7` (main)
Метод: локальный клон (зеркало origin) + GitHub REST API (gh) + статический анализ

Смежный документ: `docs/REPO_AUDIT.md` (12.08.2026) — аудит стандартов GitHub,
архитектуры и безопасности кода. Здесь — фокус на Git-процессы: история,
ветвление, CI/CD, секреты, зависимости, лицензии, supply chain.

---

## 1. Профиль репозитория

| Параметр | Значение |
|---|---|
| Владелец / имя | `ZDarow` / `wireless-audio-21` |
| Видимость | public |
| Дата создания | 09.08.2026 |
| Ветка по умолчанию | `main` |
| Лицензия в GitHub | **отсутствует** (`licenseInfo: null`) |
| Размер (disk usage) | 340 КБ |
| Issues | включены |
| Homepage | не задана |
| GitHub Actions runs | **0** (никогда не выполнялись) |
| Теги / релизы | 0 / 0 |
| Vulnerability alerts | **отключены** |
| Dependabot | не настроен |

---

## 2. Структура репозитория

```
/                       # 4 md-файла в корне: ТЗ.md, ТЗ_Веб.md, Primer.md, аудит.txt ⚠️
├── AGENTS.md           # руководство для агентов (актуально)
├── README.md           # качественный, покрывает быстрый старт + REST
├── .github/workflows/ci.yml
├── platformio.ini      # 6 env: 3 целевых S3 + 3 legacy-стенд
├── platformio.master.ini  # изолированный core_dir (.pio-core-master)
├── config.example.env  # → generated_config.h (gitignored)
├── config.example.h    # ручной аналог
├── firmware/
│   ├── common/         # header-only (config/audio/transport/web/ui/util)
│   ├── master_s3/      # целевой мастер (S3, Wi-Fi UDP)
│   ├── master/         # legacy A2DP-стенд (C0.2)
│   └── satellite/      # сателлиты
├── scripts/            # generate_config.py, flash_master.sh, flash_satellite.sh
├── test/               # host-тесты (5 бинарей) + stubs/ + Makefile
├── docs/               # PLAN, TASKS, TASKS_DETAILED, architecture, hardware,
│                       # wiring, api.http, REPO_AUDIT.md, screenshots/ (пусто)
└── .vscode/extensions.json
```

**Положительно:** `config.env` и `.pio*` никогда не коммитились; `generated/`
исключено из git; бинарников в истории нет; `.gitignore` грамотный.
**Проблемы:** 4 документа в корне с кириллическими именами (ТЗ.md, ТЗ_Веб.md,
Primer.md, аудит.txt) — мешают навигации, ломают CI-инструменты без Unicode;
`docs/screenshots/` пуста и не в git (пустые каталоги не отслеживаются).

---

## 3. История коммитов

| Метрика | Значение | Оценка |
|---|---|---|
| Всего коммитов | 27 | — |
| Авторов | 1 (`ZDarow <oleg-mordovkin@yandex.ru>`) | ок для соло |
| Период | 09.08.2026 – 12.08.2026 (4 дня) | очень плотный |
| Граф | строго линейный, без merge | ок для соло, плохо для команды |
| Мусорных/пустых коммитов | 0 | ✅ |
| `git fsck` | чисто | ✅ |
| Веток, кроме main | 0 | ⚠️ |
| Тегов | 0 | ⚠️ |

**Анализ качества сообщений:**
- Сообщения осмысленные, разбивка логичная (каркас → этапы → багфиксы →
  docs), длина в пределах ~80 символов.
- Стиль **не однороден**: 26 из 27 коммитов — русские повелительные фразы без
  префикса («Реализовать F21: ...», «Исправить B1: ...»), 1 — `docs: ...`.
  В AGENTS.md объявлен формат `<тип>: <описание>` — фактически не соблюдается.
  ⚠️ Для автоматической генерации changelog (conventional commits) стиль
  нужно унифицировать.
- Нет коммитов с `password|secret|key|token` в теме — ✅.

**Рекомендации (история):**
1. Ввести единый Conventional-like префикс (`feat/fix/docs/chore/ci/test`)
   по образцу AGENTS.md (низкий приоритет — история уже записана, менять
   прошлое через rebase не требуется).
2. Создать тег `v0.1.0` (MVP-состояние) — первая точка восстановления.
3. Настроить `git config commit.gpgsign true` + подпись коммитов (GPG/SSH).

---

## 4. Стратегия ветвления

**Факт:** одна ветка `main`, коммиты напрямую, без PR.

**Оценка (критичность: MEDIUM):**
- Для соло-проекта в 4 дня — приемлемо и даже разумно.
- Для заявленного режима «несколько агентов работают параллельно» и
  будущей команды — **необходимо**:
  - feature-ветки `feature/<what>`, `fix/<what>` (соглашение уже в AGENTS.md);
  - pull request с проверкой CI (после активации Actions);
  - `delete_branch_on_merge: true` (уже включено в repo? — проверить в Settings);
  - защиту `main` (branch protection): required status check, linear history.
- **Правило**: никогда не коммитить в `main` напрямую при работе > 1 человека.

---

## 5. CI/CD — `.github/workflows/ci.yml`

### Что есть
- Триггеры: `push: main`, `pull_request`, `workflow_dispatch` ✅
- Один job `build-and-test` на `ubuntu-latest`:
  - checkout@v4, setup-python@v5 (3.12)
  - `pipx install platformio==6.1.19` — версия зафиксирована ✅ (T18)
  - кэш PlatformIO (пакеты+platforms+.pio-core-master) ✅
  - host-тесты (`make -C test test`) ✅
  - сборка `master_s3_wifi` (через master.ini) + `satellite_s3_left/right` ✅
- Legacy env не собираются — осознанно (C0.2), но в README не отражено ⚠️

### Проблемы (критичность: HIGH, т.к. CI мёртв)

| # | Проблема | Severity | Рекомендация |
|---|---|---|---|
| CI-1 | **0 запусков Actions**: workflow есть, но ни разу не выполнен (T1). CI не защищает ни от чего | 🔴 КРИТИЧНО | Settings → Actions → Enable; запустить `workflow_dispatch` вручную |
| CI-2 | Нет `permissions: contents: read` (принцип минимальных прав) | 🟠 HIGH | Добавить на уровне job/workflow |
| CI-3 | Нет `timeout-minutes` — зависшая сборка съедает квоту | 🟠 HIGH | `timeout-minutes: 30` |
| CI-4 | Actions по плавающим тегам (`@v4`, `@v5`) без SHA-пинов | 🟠 HIGH | Заменить на полные SHA коммитов (supply chain) |
| CI-5 | `ubuntu-latest` не закреплён — завтра может быть другая ОС | 🟡 MED | Закрепить `ubuntu-24.04` |
| CI-6 | Нет `concurrency` — параллельные запуски на один PR дублируют работу | 🟡 MED | `concurrency: group: ci-${{ github.ref }}` |
| CI-7 | yamllint: строка 33 (95 > 80), отсутствует `\n` в конце файла | 🟢 LOW | Поправить |
| CI-8 | Нет проверки legacy-сборки даже опционально | 🟢 LOW | Опциональный job `legacy` (if: inputs.legacy) |
| CI-9 | Нет Dependabot / vulnerability alerts | 🟠 HIGH | Включить alerts; добавить `.github/dependabot.yml` (python/pip + gitsubmodule?) — для PlatformIO lib_deps dependabot не покрывает, отметить вручную |

### Целевая схема CI (пошагово, см. раздел 10)
1. Активировать Actions.
2. Job `lint` (yamllint + actionlint, опционально clang-format --dry-run).
3. Job `host-tests` (как сейчас, отдельно — быстрее фейлится).
4. Job `build` (мастер + сателлиты) с `needs: [lint, host-tests]`.
5. Job `legacy` (manual trigger, `workflow_dispatch` с input) — стенд C0.2.
6. Job `release` на тег `v*`: собрать `.bin` + создать GitHub Release
   (softprops/action-gh-release, pinned SHA).

---

## 6. Безопасность и секреты

### Захардкоженные секреты — найдено

| # | Что | Где | Severity | Комментарий |
|---|---|---|---|---|
| S-1 | Пароль AP `audio21master` — **дефолт в прошивке** | `node_config.h:36,41-42,186` | 🟠 HIGH | Попадает в каждую прошивку при отсутствии config.env. Дефолт известен из ТЗ §6.3 — AP доступен любому, кто знает дефолт, до смены пароля |
| S-2 | Примеры паролей `MyHomePassword`, `audio21master` в example-конфигах | `config.example.env:25,27`, `config.example.h:18,20` | 🟡 MED | Примеры допустимы, но: (а) требуют обязательной смены, (б) `config.example.h` дублирует дефолт прошивки — единый источник не используется |
| S-3 | `adminPasswordHash` пуст по дефолту | `node_config.h:204` | 🟡 MED | Пароль админа задаётся при первом старте — ок, но стоит задокументировать |
| S-4 | API-ключей, токенов, приватных ключей в коде/истории | — | ✅ | **Не найдено** (искал: AKIA, ghp_, api_key, BEGIN RSA, sk-...) |
| S-5 | `config.env` в истории | — | ✅ | **Никогда не коммитился** |

### Известные уязвимости кода (детали — REPO_AUDIT.md V1–V10)
- V1 ESP-NOW без шифрования — 🔴
- V4 UDP-аудио без аутентификации источника — 🟠
- V2 SHA-256 без итераций, дефолтная соль — 🟠
- V5 OTA без подписи — 🟡
Все — в приоритете следующей итерации (см. план).

### Рекомендации (секреты)
1. Механизм «forced password change»: при первом старте с дефолтным паролем
   AP — печатать в serial предупреждение; в Web UI — баннер «смените пароль».
2. Соль пароля — per-device из `esp_random`, хранить в NVS (V2).
3. Добавить в CI job `gitleaks`/`trufflehog` — детект секретов на каждый push
   (проверено: в истории чисто, но автоматизация обязательна дальше).
4. Пересмотреть дефолт AP-пароля: генерировать уникальный при первом старте
   и печатать в serial (как делают роутеры).

---

## 7. Зависимости и лицензии

### Ревью lib_deps (platformio.ini)

| Зависимость | Где используется | Версия | Лицензия | Оценка |
|---|---|---|---|---|
| `arduino-audio-tools` (git) | legacy `master_a2dp` | SHA `4ba48c9d` (зафиксирован, T11 ✅) | **GPL-3.0** | ⚠️ см. L-1 |
| `ESP32-A2DP` (git) | legacy `master_a2dp` | SHA `3245602` (зафиксирован ✅) | Apache-2.0 | ✅ |
| `Preferences` | legacy env | из core 2.0.17 | Apache-2.0 (ESP) | ✅ |
| `WebServer/HTTPClient/Update/Network` | S3 env | из core 3.3.11 lib_deps (T22) | Apache-2.0 | ✅ |
| `bblanchon/ArduinoJson@^7.0.0` | S3 | `^7` (non-fixed) | MIT | ⚠️ `^7` допускает мажорные обновления... нет, `^` для 7.0.x ограничен мажором 7. Пин на host — желателен 7.0.4+ |
| PlatformIO | CI | `6.1.19` фикс. | — | ✅ |

### L-1 — **GPL-3.0 в прошивке без LICENSE репозитория** (🔴 КРИТИЧНО, правовой)
- `master_a2dp` (часть распространяемой сборки) линкуется с
  `arduino-audio-tools` (GPL-3.0). Производная работа распространяется под
  GPL-3.0 — **репозиторий обязан иметь LICENSE и объявление GPL**.
- Варианты:
  a) Принять GPL-3.0: добавить `LICENSE` (GPL-3.0), раздел «Лицензии
     зависимостей» в README (не снимает обязательство — исходники уже
     открыты, требуются уведомления).
  b) Выпилить legacy `master_a2dp` из публичного репо (стенд C0.2 → в
     приватный форк/отдельный архив): публичное ядро остаётся MIT-совместимым.
  c) Сменить legacy-DAC-код на компоненты Apache-2.0 — дорого, не оправдано
     для стенда.
- **Решение по умолчанию: (a) + (b-опция)**: добавить GPL-3.0 LICENSE;
  legacy отметить `EXPERIMENTAL/вне поставки` (уже сделано — C0.2 в docs).

### L-2 — Лицензии в README
- В README нет раздела «Лицензии» вообще. Добавить: основная лицензия +
  таблица зависимостей (MIT/Apache-2.0/GPL-3.0).

### L-3 — Уязвимости зависимостей
- GitHub vulnerability alerts отключены; Dependabot не настроен.
- For ESP-IDF/Arduino-библиотек CVE-фид слабый — но alerts обязателен как
  минимум для python/actions.
- Рекомендация: `.github/dependabot.yml` (python: pip — для scripts;
  github-actions: ecosystem → ежедневно, low severity).

---

## 8. Качество кода и документации

### Качество кода
| Аспект | Оценка | Замечание |
|---|---|---|
| Host-тесты | ✅ 5/5 зелёные (запускал локально) | audio, transport, util, udp_receiver, config_gen |
| TODO/FIXME | ✅ нет ни одного | — |
| Заглушки | ✅ stubs/ вынесены (T21) | — |
| Монолиты | 🟠 `web_server.h` 1594 строки (73 КБ) — C++ + HTML + CSS + JS | A1; декомпозиция — T16 |
| Монолиты | 🟡 `master_s3/src/main.cpp` 979 строк | A4: группировка в `MasterApp` |
| Форматтер | ❌ нет `.clang-format` | добавить; CI check |
| Статический анализ | ❌ нет clang-tidy/cppcheck | для header-only — низкий приоритет, но полезно |
| Покрытие | ❌ нет покрытия для web_server/storage/espnow | железо-зависимое — ручные проверки (допустимо для MVP) |
| `String` в веб-путях | 🟡 фрагментация heap | A6 |
| RTTI/exceptions | ✅ не используются (embedded ok) | — |

### Качество документации
| Документ | Статус |
|---|---|
| README.md | ✅ хорошо (схема, REST, быстрый старт) |
| docs/architecture, hardware, wiring, PLAN, TASKS | ✅ актуальны |
| docs/REPO_AUDIT.md | ✅ создан 12.08.2026 |
| конфиг-документация | ⚠️ двойной источник `config.example.env` vs `.h` (T6 закрыт, риск остаётся) |
| Корень | ⚠️ ТЗ.md/ТЗ_Веб.md/Primer.md/аудит.txt — перенести в docs/ |
| Бейджи в README | ❌ нет shields.io (CI/license/PIO) |
| CONTRIBUTING/SECURITY/CHANGELOG/шаблоны issues/PR | ❌ нет |
| Ссылки на правила репо (CODEOWNERS) | ❌ нет |

---

## 9. Матрица найденных проблем (сводно)

| ID | Проблема | Severity | Усилия |
|---|---|---|---|
| C1 | CI никогда не запускался (0 runs), T1 | 🔴 КРИТИЧНО | 10 мин |
| C2 | Repo без LICENSE при GPL-3.0-зависимости (audio-tools) | 🔴 КРИТИЧНО | 1 ч |
| H1 | ESP-NOW без шифрования (V1) | 🔴 HIGH | 2–4 ч |
| H2 | UDP-аудио без аутентификации источника (V4) | 🟠 HIGH | 2–4 ч |
| H3 | SHA-256 без итераций + дефолтная соль (V2) | 🟠 HIGH | 2 ч |
| H4 | Дефолтный AP-пароль `audio21master` в прошивке (S-1) | 🟠 HIGH | 2 ч |
| H5 | supply chain: actions по тегам, нет dependabot/alerts | 🟠 HIGH | 1 ч |
| M1 | Нет ветвления/PR/защиты main (несколько агентов!) | 🟠 MED | 1 ч настройки |
| M2 | Нет тегов/релизов/CHANGELOG | 🟡 MED | 2 ч |
| M3 | Монолит web_server.h (A1/T16) | 🟡 MED | 1–2 дня |
| M4 | Мусор в корне (кириллица), screenshots пусты | 🟡 MED | 30 мин |
| M5 | CI без permissions/timeout/concurrency (CI-2..6) | 🟡 MED | 30 мин |
| M6 | Нет .clang-format/.editorconfig/.gitattributes | 🟡 MED | 1 ч |
| L1 | Стиль коммитов неоднороден | 🟢 LOW | по ходу |
| L2 | yamllint-замечания к ci.yml | 🟢 LOW | 5 мин |
| L3 | `ArduinoJson@^7` без точного пина | 🟢 LOW | 5 мин |
| L4 | README без бейджей и раздела лицензий | 🟢 LOW | 30 мин |

---

## 10. Пошаговый план оптимизации

### Фаза 0 — Немедленно (день 1, ~3 ч)
1. **Активировать GitHub Actions** (Settings → Actions → Enable) и запустить
   `workflow_dispatch` → проверить зелёный CI. *(C1)*
2. **Добавить LICENSE** (GPL-3.0, файл + GitHub license) и раздел
   «Лицензии» в README. *(C2, L-2)*
3. **Hardening ci.yml**: `permissions: contents: read`, `timeout-minutes: 30`,
   `concurrency`, пин actions по SHA, `ubuntu-24.04`, `\n` в конце. *(M5, CI-2..7)*
4. **Включить vulnerability alerts** (Settings → Code security) + добавить
   `.github/dependabot.yml` (github-actions + pip). *(H5)*
5. Убрать мусор из корня: `ТЗ.md`, `ТЗ_Веб.md`, `Primer.md`, `аудит.txt` →
   `docs/`. *(M4)*

### Фаза 1 — Безопасность (дни 1–2, ~1 день)
6. V1: ESP-NOW PMK/LMK + `encrypt=true` (2 сателлита ≤ 7 encrypt-пиров).
   *(H1)*
7. V4: токен/проверка remoteIP в UDP-аудио (C5.7 остаток). *(H2)*
8. V2: PBKDF2-HMAC-SHA256 (≥1000 итераций) + per-device соль из
   `esp_random` в NVS; мин. длина пароля 8. *(H3)*
9. S-1: уникальный AP-пароль при первом старте + предупреждение в serial;
   баннер в Web UI «смените пароль». *(H4)*
10. Gitleaks-шаг в CI (или pre-commit hook). *(H5)*

### Фаза 2 — Git-процессы (день 2–3, ~4 ч)
11. **Ветвление**: feature-ветки + PR (обязательно при работе агентов),
    защита `main` (required status check = CI, linear history).
    *(M1)*
12. **Теги/релизы**: `v0.1.0` на текущем HEAD; CI job для релизов
    (сборка + attach .bin при теге `v*`). *(M2)*
13. Unify стиль коммитов (типы из AGENTS.md) для будущих changelog. *(L1)*
14. Шаблоны issues/PR, CONTRIBUTING.md, SECURITY.md, CHANGELOG.md. *(M4)*

### Фаза 3 — Качество кода (день 3–5)
15. Декомпозиция `web_server.h` → `common/web/` (T16): SPA в PROGMEM, класс
    в .cpp. *(M3)*
16. `.clang-format` + правило в CI (`--dry-run --Werror`); `.editorconfig`;
    `.gitattributes` (text eol=lf). *(M6)*
17. A9: FreeRTOS-очередь для RX ESP-NOW (по паттерну Espressif). *(из REPO_AUDIT)*
18. Пин `ArduinoJson@7.0.4+sha` (или точный `~7.0.4`). *(L3)*

### Фаза 4 — Финальная сборка и выпуск (день 5)
19. Сборка всех 6 env + host-тесты на чистых кэшах локально и в CI.
20. Ручная проверка звука на железе (§18 этапы): каналы L/R, sub-НЧ,
    задержки, громкости, OTA.
21. Релиз `v0.1.0`: тег → GitHub Release с прошивками (`.bin`), чейнджлог
    из conventional-коммитов, скриншоты Web UI в `docs/screenshots/`. *(M2/M4)*
22. Homepage репо → GitHub Pages (лендинг; материалы в REPO_AUDIT §6).

### Критерии выхода («done»)
- CI зелёный на push и PR, `workflow_dispatch` запускается.
- LICENSE + раздел лицензий в README.
- ESP-NOW шифрован, UDP-аудио аутентифицировано, пароль — PBKDF2 + смена
  дефолта.
- Тег `v0.1.0` + GitHub Release; защита main; ветвление по правилам.
- `.clang-format` в CI; `docs/screenshots/` наполнена; корень чистый.

---

## 11. Соответствие best practices (итоговая таблица)

| Практика | Статус |
|---|---|
| README + структура docs | ✅ |
| LICENSE | ❌ (блокер) |
| CI активен, защищает main | ❌ (0 runs) |
| Dependabot / vulnerability alerts | ❌ |
| Conventional commits / changelog | ⚠️ частично |
| Feature-ветки + PR | ❌ |
| Теги / релизы | ❌ |
| Минимальные permissions в CI | ❌ |
| Пин action по SHA | ❌ |
| Secret scanning (gitleaks) | ❌ |
| Форматтер/линтер в CI | ❌ |
| Тесты в CI | ✅ (готовы, не запускаются) |
| Воспроизводимость зависимостей (SHA) | ✅ (T11/T18) |
| Чистота истории (нет секретов/бинарников) | ✅ |
| AGENTS.md для агентов | ✅ |