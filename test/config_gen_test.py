#!/usr/bin/env python3
"""config_gen_test.py — host-тест генератора конфига.

Проверяет scripts/generate_config.py: генерирует generated_config.h из
тестового config.env и сверяет все макросы (числа, строки с кавычками,
MAC-адреса, enum-маппинги).

Запуск: python3 config_gen_test.py   (или через make test)
Возвращает 0 при успехе, 1 при провале.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "scripts", "generate_config.py")

# Тестовый конфиг: покрывает все типы ключей.
TEST_ENV = """\
AUDIO_NODE_ROLE=SATELLITE
AUDIO_SOURCE_MODE=WIFI
AUDIO_TRANSPORT_MODE=UDP
AUDIO_WIFI_MODE=APSTA
AUDIO_WIFI_AP_SSID=Audio21-Master
AUDIO_WIFI_AP_PASSWORD=audio21master
AUDIO_WIFI_SSID=My "Quoted" Net\\WithSlash
AUDIO_WIFI_PASSWORD=p@ss"word
AUDIO_HOSTNAME=audio-master
AUDIO_UDP_PORT=5004
AUDIO_LEFT_SAT_MAC=AA:BB:CC:DD:EE:01
AUDIO_RIGHT_SAT_MAC=0a:1b:2c:3d:4e:5f
AUDIO_SAMPLE_RATE=48000
AUDIO_BITS_PER_SAMPLE=16
AUDIO_CHANNELS=2
AUDIO_CROSSOVER_HZ=90
AUDIO_DELAY_LEFT_MS=0
AUDIO_DELAY_RIGHT_MS=10
AUDIO_DELAY_SUB_MS=20
AUDIO_I2S_BCK=26
AUDIO_I2S_WS=25
AUDIO_I2S_DATA_OUT=22
AUDIO_OLED_SDA=21
AUDIO_OLED_SCL=23
AUDIO_ENC_A=32
AUDIO_ENC_B=33
AUDIO_ENC_BTN=34
"""

EXPECTED = {
    # enum-маппинги
    "AUDIO_NODE_ROLE_MASTER": "0",
    "AUDIO_SOURCE_MODE_A2DP": "0",
    "AUDIO_TRANSPORT_MODE_ESPNOW": "0",
    # APSTA -> генерируется только AUDIO_WIFI_MODE_APSTA=1
    # (AUDIO_WIFI_MODE_AP не выдаётся; дефолт 1 в node_config.h, приоритет у APSTA)
    "AUDIO_WIFI_MODE_APSTA": "1",
    # строки (экранирование кавычек/слешей)
    'AUDIO_WIFI_SSID': '"My \\"Quoted\\" Net\\\\WithSlash"',
    'AUDIO_WIFI_PASSWORD': '"p@ss\\"word"',
    'AUDIO_WIFI_AP_SSID': '"Audio21-Master"',
    'AUDIO_WIFI_AP_PASSWORD': '"audio21master"',
    'AUDIO_HOSTNAME': '"audio-master"',
    # числа
    "AUDIO_UDP_PORT": "5004",
    "AUDIO_SAMPLE_RATE": "48000",
    "AUDIO_BITS_PER_SAMPLE": "16",
    "AUDIO_CHANNELS": "2",
    "AUDIO_CROSSOVER_HZ": "90",
    "AUDIO_DELAY_LEFT_MS": "0",
    "AUDIO_DELAY_RIGHT_MS": "10",
    "AUDIO_DELAY_SUB_MS": "20",
    "AUDIO_I2S_BCK": "26",
    "AUDIO_I2S_WS": "25",
    "AUDIO_I2S_DATA_OUT": "22",
    "AUDIO_OLED_SDA": "21",
    "AUDIO_OLED_SCL": "23",
    "AUDIO_ENC_A": "32",
    "AUDIO_ENC_B": "33",
    "AUDIO_ENC_BTN": "34",
    # MAC-адреса
    "AUDIO_LEFT_SAT_MAC": "{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01}",
    "AUDIO_RIGHT_SAT_MAC": "{0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f}",
}

MACRO_RE = re.compile(r"^#define\s+(\S+)\s+(.*)$")


def main():
    failures = 0

    def check(cond, msg):
        nonlocal failures
        if not cond:
            print(f"FAIL: {msg}")
            failures += 1

    with tempfile.TemporaryDirectory() as tmp:
        env_path = os.path.join(tmp, "config.env")
        out_path = os.path.join(tmp, "generated_config.h")
        with open(env_path, "w", encoding="utf-8") as f:
            f.write(TEST_ENV)

        res = subprocess.run(
            [sys.executable, GEN, env_path, out_path],
            capture_output=True, text=True,
        )
        check(res.returncode == 0, f"генератор завершился с ошибкой: {res.stderr}")

        with open(out_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Собираем фактические макросы.
        actual = {}
        for line in content.splitlines():
            m = MACRO_RE.match(line)
            if m:
                actual[m.group(1)] = m.group(2).strip()

        # Все ожидаемые макросы присутствуют и совпадают.
        for macro, value in EXPECTED.items():
            check(macro in actual, f"нет макроса {macro}")
            if macro in actual:
                check(actual[macro] == value,
                      f"{macro}: ожидается {value!r}, получено {actual[macro]!r}")

        # Лишних макросов быть не должно.
        check(len(actual) == len(EXPECTED),
              f"число макросов: ожидается {len(EXPECTED)}, получено {len(actual)}")

        # Неизвестный ключ → ошибка генератора.
        bad_env = os.path.join(tmp, "bad.env")
        with open(bad_env, "w", encoding="utf-8") as f:
            f.write("UNKNOWN_KEY=1\n")
        res = subprocess.run(
            [sys.executable, GEN, bad_env, os.path.join(tmp, "bad.h")],
            capture_output=True, text=True,
        )
        check(res.returncode != 0, "неизвестный ключ должен вызывать ошибку")

        # Неверный MAC → ошибка.
        bad_mac = os.path.join(tmp, "badmac.env")
        with open(bad_mac, "w", encoding="utf-8") as f:
            f.write("AUDIO_LEFT_SAT_MAC=not-a-mac\n")
        res = subprocess.run(
            [sys.executable, GEN, bad_mac, os.path.join(tmp, "badmac.h")],
            capture_output=True, text=True,
        )
        check(res.returncode != 0, "неверный MAC должен вызывать ошибку")

    if failures:
        print(f"ИТОГ: {failures} ПРОВАЛОВ")
        return 1
    print("ИТОГ: ВСЕ ТЕСТЫ ПРОЙДЕНЫ")
    return 0


if __name__ == "__main__":
    sys.exit(main())