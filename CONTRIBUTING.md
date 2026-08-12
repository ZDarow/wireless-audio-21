# Contributing

Спасибо за интерес к проекту. Краткие правила (подробнее — `docs/` и `AGENTS.md`).

## Ветки и коммиты

- Ветка: `feature/<что>`, `fix/<что>`, `docs/<что>`, `chore/<что>` (англ., kebab-case).
- Коммиты: `<тип>: <описание>` — `feat`, `fix`, `refactor`, `docs`, `test`,
  `chore`, `style`, `perf`, `ci` (русский язык, повелительное наклонение,
  заголовок ≤ 72 символа).
- История — линейная (rebase), `main` защищена; изменения входят через PR.

## Изменения в прошивке

- C++17, header-only модули в `firmware/common` без Arduino-железа в ядре —
  для host-тестов.
- Изменения в `firmware/common` должны проходить `make -C test test`.
- Сборка: `pio run -c platformio.master.ini` (мастер S3), `pio run -e satellite_s3_left|right`.
- Legacy env (`master_a2dp`, `satellite_left/right`) — отладочный стенд C0.2,
  ломать нельзя, но CI их не собирает.

## Конфигурация

Правки конфига — только через `config.env` (генерируется
`scripts/generate_config.py`), не вручную в `generated_config.h` или `node_config.h`.

## Проверки перед PR

1. `make -C test test` — зелёные host-тесты.
2. `yamllint -c .yamllint .github/workflows/*.yml .github/dependabot.yml`.
3. Сборка целевых env (S3) без ошибок.
4. `docs/TASKS.md` и `AGENTS.md` синхронизированы с изменениями.

## PR

- Описание на русском: что сделано, как тестировалось, breaking changes.
- Ссылки на связанные задачи из `docs/TASKS.md`.
- Скриншоты Web UI — в `docs/screenshots/`.
