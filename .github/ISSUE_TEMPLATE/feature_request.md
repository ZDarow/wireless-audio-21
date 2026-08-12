---
name: Задача/улучшение
about: Новая функциональность или технический долг
title: ""
labels: enhancement
assignees: ''

---

## Задача (как в docs/TASKS.md)
- ID: (например, C4.1)
- Что нужно сделать:
- Критерий готовности:

## Мотивация
Зачем это нужно.

## Затрагиваемые модули
- [ ] `firmware/common/...`
- [ ] `firmware/master_s3/` / `firmware/master/` / `firmware/satellite/`
- [ ] Web UI / REST API
- [ ] Документация (`docs/`)

## Проверка
- [ ] `make -C test test` зелёные
- [ ] Сборка env S3
- [ ] `docs/TASKS.md` и `AGENTS.md` обновлены
