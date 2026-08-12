# Changelog

Формат — [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/).
Версии — по [SemVer](https://semver.org/lang/ru/).

## [0.2.1] — 2026-08-12 (фаза 1 аудита безопасности)

### Безопасность
- ESP-NOW шифрование: PMK/LMK + `encrypt=true` (V1; ключи `AUDIO_ESPNOW_PMK/LMK`).
- PBKDF2-HMAC-SHA256 (10000 итераций) вместо SHA-256 для пароля Web UI (V2);
  старый формат принимается до смены пароля.
- Предупреждение о заводском пароле AP: баннер Web UI + serial (S-1).

### Тесты
- Новый host-тест `auth_test` (SHA-256, HMAC-SHA256 RFC 4231, PBKDF2 RFC 7914).

### Известные ограничения
- Per-device соль PBKDF2 — техдолг (NVS v5).
- UDP-аудио без аутентификации источника (V4/C5.7), OTA без подписи (V5).

## [0.2.0] — 2026-08-12

- Канальные громкости (C2.2), TX-аудио батчами на сателлиты (C3.1/C3.2),
  сателлит: jitter/DelayLine/VolumeControl (C3.3/C3.5).
- Веб-безопасность (C5.1–C5.7), надёжность (C6.1–C6.7).
- Репитер APSTA+NAPT на мастере S3 (F18/F21).

## [0.1.0] — 2026-08-10

- Каркас прошивки: Wi-Fi, Web UI + REST, DSP (tone/limiter/volume/LR4),
  ESP-NOW/UDP транспорт, конфигурация через `config.env`.
