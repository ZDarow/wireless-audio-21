# GUI/UX Отчёт: Интерфейс управления беспроводной аудиосистемой 2.1

Источник: глубокий веб-поиск (2026-11-18), анализ open-source репозиториев и современных UI/UX паттернов.

---

## 1. Обзор нативных и кроссплатформенных фреймворков

| Категория | Фреймворк | Язык | Размер бинарника | Память (idle) | Платформы | Лицензия | Вердикт для проекта |
|-----------|-----------|------|------------------|---------------|-----------|----------|---------------------|
| Веб-оболочка | Electron | JS/TS | 50–150 МБ | 90–180 МБ | Win, macOS, Linux | MIT | Тяжёлое, избыточно для локального управления |
| Веб-оболочка | Tauri 2 | Rust + веб | 3–15 МБ | 25–70 МБ | Win, macOS, Linux, iOS, Android | MIT/Apache | Хороший баланс размера и веб-навыков |
| Собственный рендерер | Flutter | Dart | 15–60 МБ | 45–80 МБ | Win, macOS, Linux, iOS, Android | BSD | Единый код с мобильной, но Dart — новый стек |
| Нативный C++ | Qt 6 (QML/Widgets) | C++ | 30–80 МБ | низкая | Все, включая встроенные | LGPL/коммерч. | Мощно, но дорогая лицензия и крутая кривая |
| Лёгкий декларативный | Slint | Rust/C++/JS | <1 МБ | <300 КБ RAM | Встроенные, десктоп, веб | GPL/Apache | Идеально для встроенных, но новый стек |
| Встроенный UI | LVGL | C | Килобайты | Килобайты | MCU/MPU, любой дисплей | MIT | Для OLED на устройстве, не для ПК/телефона |

### Ключевые выводы из сравнений (Kanopy, Sekin, Fora Soft, TT Lab, 2026)

- **Tauri** — лучший выбор для веб-команды, которой важен размер установщика и нативный доступ к файловой системе/сети.
- **Electron** — безопасный дефолт для веб-команды, но 100+ МБ избыточен для утилиты управления аудиоустройством.
- **Flutter** оправдан только если нужна мобильная версия с общим кодом.
- Для управления ESP32-устройством по локальной сети достаточно веб-интерфейса на устройстве + опционально десктопного клиента.

---

## 2. Ключевые архитектурные подходы и паттерны

### 2.1 Транспорт управления

| Паттерн | Реализация | Примеры из репозиториев |
|---------|-----------|------------------------|
| REST API + JSON | HTTP endpoints на устройстве | yoRadio, esp32-webradio, WVR |
| Server-Sent Events (SSE) | Поток метаданных в реальном времени | esp32-webradio (`/?events`) |
| Web Serial | Прямое управление по USB/UART | DQ-DSP UI (Chrome/Edge) |
| Captive portal + mDNS | Авто-открытие страницы настройки | yoRadio (AP mode, http://192.168.4.1), Sonos Setup 2.0 |
| OTA через веб | Загрузка прошивки браузером | yoRadio, esp32-webradio, CoyoPedal |

### 2.2 Архитектура интерфейса

```
Устройство (ESP32-S3)
├── Web сервер (архитектура уже есть: web_server.h)
│   ├── REST API (/api/volume, /api/crossover, /api/delay, /api/wifi)
│   ├── SPA (одностраничное приложение, PROGMEM)
│   ├── SSE поток статуса (сателлиты online/offline, уровень сигнала)
│   └── OTA endpoint (загрузка прошивки)
└── mDNS (http://audio-master.local)
```

### 2.3 Паттерны управления состоянием (из DQ-DSP, CoyoPedal)

- **Живые объекты DSP** — настройки применяются к конвейеру в реальном времени (уже реализовано в `master_s3/src/main.cpp` через Web UI).
- **Presets** — сохранение/загрузка конфигураций (localStorage в браузере + NVS на устройстве).
- **Dirty indicator** — визуальная метка «изменено, не сохранено» (Saved · Modified · Unsaved).
- **Live diff-streaming** — параметры стримятся на устройство при перетаскивании ползунка, «Apply» отправляет всю конфигурацию пакетом.

---

## 3. Рекомендации по UX/UI

### 3.1 Паттерны настройки (Sonos Setup 2.0, Cisco Headset, JBL One App)

| Принцип | Реализация |
|---------|-----------|
| Модульная настройка | Пользователь выбирает свой путь: базовое подключение → опциональная настройка в любом порядке (70+ happy paths у Sonos) |
| Прогрессивное раскрытие | Только основные шаги на первом экране, расширенные настройки скрыты (Cisco: 85% пользователей настраивают только первичное подключение) |
| Визуальные подсказки вместо текста | Анимации включения Bluetooth, индикация состояния устройства, минимум текста (yoRadio pairing redesign) |
| Graceful degradation | При ошибке соединения — возврат на альтернативный happy path, а не тупик (Sonos) |
| Контекстная помощь | Встроенная справка без выхода из приложения (Cisco headset) |

### 3.2 Экран управления аудио (Cisco, JBL, DQ-DSP)

| Элемент | Рекомендация |
|---------|-------------|
| Главный экран | Громкость, источник, статус сателлитов (L/R online), режим Wi-Fi, уровень заряда/питания |
| Эквалайзер | 3 пресета (баланс casual/power user), ползунки с анимацией, применение в реальном времени |
| Кроссовер | Частота раздела 70–120 Гц, визуальная кривая суммы каналов (как в DQ-DSP) |
| Задержки | L/R/Sub 0–200 мс, по 10 мс шаг, предварительный просмотр |
| Статус сателлитов | Индикатор online/offline с таймаутом 6 с, heartbeat-статус |
| Тема | Тёмная тема по умолчанию (Lovesac StealthTech), адаптация под освещение |

### 3.3 Навигация

- **Нижние вкладки** (bottom tabs) — для мобильных, легко доступны большим пальцем (Cisco)
- **Гамбургер-меню** — только для второстепенных настроек
- **Кнопки с иконкой + текстом** — для неопытных пользователей (Cisco)
- **Авто-выход из режима редактирования** через 10 с без активности (уже реализовано в OLED-меню)

---

## 4. План интеграции с проектом

### Этап 1 — Веб-интерфейс на устройстве (уже частично реализован)

`web_server.h` уже содержит REST API, SPA, аутентификацию, CSRF, rate limit. Необходимо:

1. **Добавить SSE endpoint** для потокового статуса сателлитов (online/offline, уровень сигнала) — вместо опроса каждые N мс.
2. **Добавить preset-менеджмент** — сохранение/загрузка конфигураций DSP (громкость, кроссовер, задержки) в NVS + экспорт/импорт JSON через веб.
3. **Добавить dirty indicator** на ползунках громкости/кроссовера — визуальная обратная связь о несохранённых изменениях.
4. **Адаптивная верстка** — SPA должна корректно отображаться на мобильных (смартфон как основной пульт) и десктопах.

### Этап 2 — Десктопное приложение (опционально)

Если требуется нативное приложение для ПК (не только веб-браузер):

| Вариант | Когда выбирать | Интеграция |
|---------|---------------|------------|
| Tauri 2 | Веб-команда, важен размер установщика, нужен нативный доступ к файлам/сети | Rust бэкенд + веб-фронтенд, общение с устройством по UDP/ESP-NOW через сеть |
| Electron | Нужен Chromium для медиа/аудио, команда знает JS/TS | Bundled Chromium, Node.js для сетевого доступа к устройству |
| Ничего (только веб) | Управление через браузер достаточно | Использовать существующий Web UI на устройстве |

### Этап 3 — Мобильное приложение (опционально)

Если нужен нативный клиент для iOS/Android:

- **Flutter** — единый код с десктопом, если выбран на этапе 2.
- **Tauri 2** — мобильная поддержка с октября 2024 (iOS + Android).
- **Нативная разработка** — Swift (iOS) + Kotlin (Android) для максимальной интеграции с системой (Bluetooth, уведомления).

### Этап 4 — UI/UX полировка (сроки)

| Задача | Приоритет | Оценочные затраты |
|--------|-----------|-------------------|
| SSE поток статуса | Высокий | 1–2 дня |
| Preset-менеджмент (сохранение/загрузка) | Высокий | 2–3 дня |
| Dirty indicator на ползунках | Средний | 0.5–1 день |
| Адаптивная верстка SPA | Средний | 1–2 дня |
| Десктопное приложение (Tauri/Electron) | Низкий | 1–2 недели |
| Мобильное приложение | Низкий | 2–4 недели |

### Рекомендация по стеку

Для текущего проекта (ESP32-S3, локальная сеть, веб-сервер уже есть):

1. **Оставить веб-интерфейс на устройстве** как основной интерфейс управления.
2. **Добавить SSE + presets + dirty indicator** в существующий `web_server.h`.
3. **Десктопное приложение** — только если есть конкретная потребность (управление несколькими устройствами одновременно, офлайн-режим, глубокая интеграция с ОС). В этом случае — **Tauri 2** (малый размер, Rust бэкенд, веб-фронтенд, можно переиспользовать существующие веб-компоненты).
4. **OLED-меню + энкодер** (уже реализовано в C4.x) — локальное управление без смартфона/ПК.

---

## 5. Анализируемые open-source репозитории

| Репозиторий | Описание | Стек UI | Транспорт | Ключевые паттерны |
|-------------|----------|---------|-----------|-------------------|
| agooddaytowork/dq-dsp-ui | Browser DSP cockpit для ESP32-S3 + PCM5102A | React, TypeScript, Vite, CSS | Web Serial (USB/UART) | Live diff-streaming, presets (localStorage + JSON), dirty indicator, Linkwitz-Riley crossover UI |
| agooddaytowork/dq-dsp-firmware | ESP32-S3 firmware, 2-in/4-out DSP | — | USB CDC-ACM UART0 | SLIP + CRC-8 frames, NVS save-to-device, separate audio/control cables |
| marchingband/wvr | ESP32 Sample Player | Web UI | Wi-Fi HTTP | Pin configuration, MIDI control, Web Audio API |
| e2002/yoradio | Web-radio на ESP32 | HTML/CSS/JS | HTTP GET API | Captive portal, Wi-Fi CSV config, telnet CLI, OTA via web |
| joruf/esp32-webradio | Internet radio с VS1053 + TFT | Bootstrap + jQuery | JSON REST API + SSE | FreeRTOS multitasking, rotary encoder, alarm scheduler, OTA, power log |
| dashersw/coyopedal | ESP32-S3 guitar pedal с NAM | TSX (Gea engine) | — | Touchscreen UI, presets, tuner, WebAssembly browser build |
| simzs/yoradio-fusion | yoRadio кастомизация | HTML/CSS | HTTP | Theme upload, multi-language, DLNA |

---

## 6. Ссылки на источники

- Tria Technologies: "Which Software Is Best for an Embedded GUI?" (2026-09-22) — Qt, Flutter, LVGL сравнение
- Spyrosoft: "Top 5 Embedded HMI UI Frameworks for 2025" — Qt, Slint, Flutter, React Native, Unreal Slate
- Qt.io: Cross-Platform UI Framework documentation
- SOMCO Software: "Embedded GUI development: choosing the right framework" (2026-04-21) — бенчмарк на Raspberry Pi 4B
- LVGL.io: Light and Versatile Embedded UI Ecosystem
- Slint.dev: Elegant GUI on Embedded
- Kanopy Labs: "Tauri vs Electron vs Flutter Desktop: Desktop Apps Compared 2026"
- Sekin: "Best Desktop App Frameworks in 2026"
- Fora Soft: "Desktop App Development in 2026: Electron, Tauri or Native"
- TT Lab Blog: "Desktop App Frameworks 2026 — Category-Wide Comparison"
- Sonos Tech Blog: "New Wizards for Tomorrow's Setup" (2021-01-14)
- James Babu: "Sonos Setup Experience" case study
- Lilian Qian Design: "Cisco headset companion app UI motion design"
- ZBLN: "The Human App - Case Study" (Human Headphones companion)
- EMcclainArt: "IoT Device Pairing & Provisioning"
- Scott Rossi: "UE BOOM" case study (Logitech speaker app)
- iF Design: "JBL One App" winner
- WE ARE FLUID: "FLUID for Sennheiser Smart Control"
- Actually Avery: "Cisco Headsets Mobile App" (2026-06-16)
- CitrusBits: "Lovesac StealthTech IoT Solution"
