# Безопасность

## Поддерживаемые версии

Актуальная ветка `main` — единственная поддерживаемая версия. Исправления
безопасности применяются в ней в приоритетном порядке.

## Сообщение об уязвимости

Публикация уязвимостей в issues **не рекомендуется** — используйте
[Private vulnerability reporting](https://docs.github.com/ru/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability)
или напишите владельцу напрямую. Указывайте:

- версию прошивки/коммит,
- сценарий эксплуатации (кратко, без деталей для воспроизведения),
- затронутые компоненты (Wi-Fi AP, Web UI, ESP-NOW, UDP-аудио, OTA).

## Принятые меры (актуально на 12.08.2026)

- **V1 — ESP-NOW шифрование**: PMK/LMK + `encrypt=true` (ключи в конфиге).
- **V2 — PBKDF2-HMAC-SHA256** (10000 итераций) для пароля Web UI.
- **Web**: SHA-256+соль сессий, CSRF, rate limit (C5.1–C5.7).
- **S-1**: предупреждение о заводском пароле AP в Web UI и serial.

## Известные остатки (техдолг)

- **V4**: UDP-аудио без аутентификации источника (C5.7).
- **V5**: OTA без подписи образа (secure boot v2 — в планах).
- **V2-остаток**: per-device соль для PBKDF2 (NVS v5).
- Пароли Wi-Fi/AP хранятся в NVS открытым текстом (требуется flash
  encryption для продакшена).
