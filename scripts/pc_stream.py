#!/usr/bin/env python3
"""pc_to_master.bat helper — стриминг всего звука ПК на мастер.

Требуется:
  1. VB-Cable (https://vb-audio.com/Cable/) — установлен и установлен
     как устройство по умолчанию для воспроизведения.
  2. ffmpeg в PATH.
  3. Python 3.11+.

Использование:
    python3 scripts/pc_stream.py --host 192.168.4.1
    python3 scripts/pc_stream.py --host 192.168.4.1 --cable "CABLE Input"
    python3 scripts/pc_stream.py --host 192.168.4.1 --wasapi
"""
import argparse
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="Стриминг всего звука ПК на мастер")
    parser.add_argument("--host", required=True, help="IP-адрес мастера")
    parser.add_argument("--port", type=int, default=5004, help="UDP порт")
    parser.add_argument("--cable", default="CABLE Input (VB-Audio Virtual Cable)",
                        help="Имя устройства dshow для VB-Cable")
    parser.add_argument("--wasapi", action="store_true",
                        help="Использовать WASAPI loopback вместо dshow")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="Путь к ffmpeg")
    args = parser.parse_args()

    if args.wasapi:
        ffmpeg_cmd = [args.ffmpeg, "-f", "wasapi", "-i", "loopback",
                      "-ar", "48000", "-ac", "2", "-f", "s16le", "-"]
    else:
        ffmpeg_cmd = [args.ffmpeg, "-f", "dshow",
                      f"-i", f"audio={args.cable}",
                      "-ar", "48000", "-ac", "2", "-f", "s16le", "-"]

    stream_cmd = [sys.executable, "scripts\\stream_to_master.py",
                  "--host", args.host, "--port", str(args.port), "--stdin"]

    print(f"Стриминг звука ПК -> {args.host}:{args.port}")
    print("Ctrl+C для остановки.")
    print(f"Убедись, что VB-Cable установлен как устройство по умолчанию.")

    try:
        p = subprocess.Popen(ffmpeg_cmd, stdout=subprocess.PIPE)
        sp = subprocess.Popen(stream_cmd, stdin=p.stdout)
        p.stdout.close()
        sp.wait()
    except KeyboardInterrupt:
        print("\nОстановлено.")
        p.terminate()
        sp.terminate()


if __name__ == '__main__':
    main()
