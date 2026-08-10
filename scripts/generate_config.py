#!/usr/bin/env python3
"""generate_config.py — генерация generated_config.h из config.env.

Читает файл в формате KEY=VALUE (как config.example.env) и создаёт
firmware/common/generated/generated_config.h с #define макросами,
которые использует firmware/common/config/node_config.h.

Использование:
    python3 scripts/generate_config.py config.env
    python3 scripts/generate_config.py config.env firmware/common/generated/generated_config.h
"""
import argparse
import os
import re
import sys

# --- Числовые ключи (генерируются как числа) ---
NUMERIC_KEYS = {
    "AUDIO_SAMPLE_RATE", "AUDIO_BITS_PER_SAMPLE", "AUDIO_CHANNELS",
    "AUDIO_CROSSOVER_HZ", "AUDIO_DELAY_LEFT_MS", "AUDIO_DELAY_RIGHT_MS",
    "AUDIO_DELAY_SUB_MS", "AUDIO_I2S_BCK", "AUDIO_I2S_WS", "AUDIO_I2S_DATA_OUT",
    "AUDIO_OLED_SDA", "AUDIO_OLED_SCL", "AUDIO_ENC_A", "AUDIO_ENC_B", "AUDIO_ENC_BTN",
}

# --- Строковые ключи ---
STRING_KEYS = {
    "AUDIO_WIFI_SSID", "AUDIO_WIFI_PASSWORD", "AUDIO_HOSTNAME",
}

# --- MAC-адреса ---
MAC_KEYS = {
    "AUDIO_LEFT_SAT_MAC", "AUDIO_RIGHT_SAT_MAC",
}

# --- Роль/источник/транспорт: значение из env -> имя макроса и его значение ---
ENUM_KEYS = {
    # env-ключ: (макрос, значение-маппинг)
    "AUDIO_NODE_ROLE": {
        "MASTER": ("AUDIO_NODE_ROLE_MASTER", "1"),
        "SATELLITE": ("AUDIO_NODE_ROLE_MASTER", "0"),
    },
    "AUDIO_SOURCE_MODE": {
        "A2DP": ("AUDIO_SOURCE_MODE_A2DP", "1"),
        "WIFI": ("AUDIO_SOURCE_MODE_A2DP", "0"),
    },
    "AUDIO_TRANSPORT_MODE": {
        "ESPNOW": ("AUDIO_TRANSPORT_MODE_ESPNOW", "1"),
        "UDP": ("AUDIO_TRANSPORT_MODE_ESPNOW", "0"),
    },
}

MAC_RE = re.compile(r"^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")


def parse_env(path):
    """Парсит KEY=VALUE, пропуская пустые строки и комментарии (# или //)."""
    values = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("//"):
                continue
            if "=" not in line:
                continue
            key, _, val = line.partition("=")
            values[key.strip()] = val.strip()
    return values


def expand(values):
    """Преобразует значения env в список (макрос, значение)."""
    out = []
    for key, val in values.items():
        if key in ENUM_KEYS:
            mapping = ENUM_KEYS[key]
            norm = val.upper()
            if norm not in mapping:
                raise ValueError(f"{key}: неверное значение: {val}")
            macro, macro_val = mapping[norm]
            out.append((macro, macro_val))
        elif key in NUMERIC_KEYS:
            if not val.isdigit():
                raise ValueError(f"{key}: ожидается число: {val}")
            out.append((key, val))
        elif key in MAC_KEYS:
            if not MAC_RE.match(val):
                raise ValueError(f"{key}: неверный MAC-адрес: {val}")
            init = ", ".join(f"0x{p}" for p in val.split(":"))
            out.append((key, "{" + init + "}"))
        elif key in STRING_KEYS:
            escaped = val.replace("\\", "\\\\").replace('"', '\\"')
            out.append((key, '"' + escaped + '"'))
        else:
            raise ValueError(f"неизвестный ключ: {key}")
    return out


def generate(values, source_path):
    lines = []
    lines.append("// generated_config.h — СГЕНЕРИРОВАН скриптом generate_config.py.")
    lines.append("// НЕ РЕДАКТИРУЙТЕ ВРУЧНУЮ. Изменяйте config.env и перегенерируйте.")
    lines.append(f"// Источник: {source_path}")
    lines.append("#pragma once")
    lines.append("")
    for macro, val in sorted(values, key=lambda kv: kv[0]):
        lines.append(f"#define {macro} {val}")
    lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="Генератор generated_config.h")
    ap.add_argument("env", help="путь к config.env")
    ap.add_argument("out", nargs="?", default="firmware/common/generated/generated_config.h",
                    help="путь к выходному файлу")
    args = ap.parse_args()

    env = parse_env(args.env)
    if not env:
        print("ОШИБКА: конфиг пуст", file=sys.stderr)
        sys.exit(1)

    try:
        macros = generate(expand(env), args.env)
    except ValueError as e:
        print(f"ОШИБКА: {e}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(macros)

    print(f"Сгенерировано {len(expand(env))} макросов -> {args.out}")


if __name__ == "__main__":
    main()