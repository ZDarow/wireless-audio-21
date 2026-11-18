#!/usr/bin/env python3
"""stream_to_master.py — стриминг PCM-аудио на мастер (UDP port 5004).

Формат пакета (udp_audio_packet.h): 18-байт заголовок (magic 0xA210) + PCM payload.
Мастер слушает UDP порт 5004.

Использование:
    # Тестовый тон 440 Гц (без микрофона, только stdlib):
    python3 scripts/stream_to_master.py --host 192.168.4.1 --fake

    # Захват микрофона (нужен sounddevice + numpy):
    pip install sounddevice numpy
    python3 scripts/stream_to_master.py --host 192.168.4.1

    # Через ffmpeg (альтернатива):
    ffmpeg -f dshow -i audio=Микрофон -ar 48000 -ac 2 -f s16le - | \
        python3 scripts/stream_to_master.py --host 192.168.4.1 --stdin
"""
import argparse
import math
import socket
import struct
import sys
import time

SAMPLE_RATE = 48000
CHANNELS = 2
BITS_PER_SAMPLE = 16
MS_PER_PACKET = 5
PAYLOAD_MAX = 1200
SAMPLES_PER_PACKET = SAMPLE_RATE * MS_PER_PACKET // 1000

PACKET_HEADER_FORMAT = '<HBBIIHHBBH'
PACKET_HEADER_SIZE = struct.calcsize(PACKET_HEADER_FORMAT)


def build_packet(sequence, timestamp_samples, pcm_data):
    payload_len = min(len(pcm_data), PAYLOAD_MAX)
    header = struct.pack(PACKET_HEADER_FORMAT,
        0xA210, 1, 0, sequence, timestamp_samples,
        SAMPLE_RATE, CHANNELS, BITS_PER_SAMPLE, payload_len)
    return header + pcm_data[:payload_len]


def make_tone(freq=440, amplitude=0.5):
    """Генерация синусоидального тона (stdlib only)."""
    buf = bytearray(SAMPLES_PER_PACKET * CHANNELS * 2)
    for i in range(SAMPLES_PER_PACKET):
        sample = int(amplitude * 32767 * math.sin(2 * math.pi * freq * i / SAMPLE_RATE))
        sample = max(-32768, min(32767, sample))
        val = struct.pack('<h', sample)
        buf[i * 4] = val[0]
        buf[i * 4 + 1] = val[1]
        buf[i * 4 + 2] = val[0]
        buf[i * 4 + 3] = val[1]
    return bytes(buf)


def main():
    parser = argparse.ArgumentParser(description="Стриминг аудио на мастер")
    parser.add_argument("--host", required=True, help="IP-адрес мастера")
    parser.add_argument("--port", type=int, default=5004, help="UDP порт")
    parser.add_argument("--fake", action="store_true", help="Тестовый тон 440 Гц")
    parser.add_argument("--stdin", action="store_true", help="PCM из stdin (s16le, 48kHz, stereo)")
    parser.add_argument("--freq", type=int, default=440, help="Частота тона (fake mode)")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    seq = 0
    ts = 0

    print(f"Стриминг на {args.host}:{args.port} ...")
    print("Ctrl+C для остановки.")

    try:
        if args.fake:
            tone = make_tone(args.freq)
            while True:
                packet = build_packet(seq, ts, tone)
                sock.sendto(packet, (args.host, args.port))
                seq = (seq + 1) & 0xFFFFFFFF
                ts += SAMPLES_PER_PACKET
                time.sleep(MS_PER_PACKET / 1000)
        elif args.stdin:
            while True:
                chunk = sys.stdin.buffer.read(SAMPLES_PER_PACKET * CHANNELS * 2)
                if not chunk:
                    break
                packet = build_packet(seq, ts, chunk)
                sock.sendto(packet, (args.host, args.port))
                seq = (seq + 1) & 0xFFFFFFFF
                ts += len(chunk) // (CHANNELS * 2)
        else:
            print("Для захвата микрофона используй --fake или --stdin", file=sys.stderr)
            print("Пример: ffmpeg -f dshow -i audio=Микрофон -ar 48000 -ac 2 -f s16le - | python3 scripts/stream_to_master.py --host <IP> --stdin", file=sys.stderr)
            sys.exit(1)
    except KeyboardInterrupt:
        print("\nОстановлено.")
    finally:
        sock.close()


if __name__ == '__main__':
    main()
