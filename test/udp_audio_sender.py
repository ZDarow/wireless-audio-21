#!/usr/bin/env python3
"""Test UDP audio sender for Wireless Audio 2.1 master.
Sends int16 stereo PCM packets to master at 192.168.10.42:5004.
Packet format: magic 0xA210, protocolVersion=1, flags=0, sequence, timestamp,
sample_rate=48000, channels=2, bits_per_sample=16, payload=int16 stereo.
"""
import argparse
import math
import socket
import struct
import time

DEST_IP = "192.168.10.42"
DEST_PORT = 5004
SAMPLE_RATE = 48000
CHANNELS = 2
BITS_PER_SAMPLE = 16
PAYLOAD_SAMPLES = 120  # 120 stereo samples = 240 int16 = 480 bytes per packet
PACKET_INTERVAL = PAYLOAD_SAMPLES / SAMPLE_RATE  # 2.5 ms


def build_packet(sequence: int, timestamp_samples: int, samples: bytes) -> bytes:
    payload_length = len(samples)
    header = struct.pack(
        "<H B B I I H B B H",
        0xA210,
        1,
        0,
        sequence & 0xFFFFFFFF,
        timestamp_samples & 0xFFFFFFFF,
        SAMPLE_RATE,
        CHANNELS,
        BITS_PER_SAMPLE,
        payload_length,
    )
    return header + samples


def generate_tone(freq: float, duration_ms: int, phase_step: float):
    samples = []
    phase = 0.0
    total_samples = SAMPLE_RATE * duration_ms // 1000
    for _ in range(total_samples):
        val = int(32767.0 * 0.5 * math.sin(2.0 * math.pi * phase))
        phase += phase_step
        if phase >= 1.0:
            phase -= 1.0
        samples.extend([val, val])  # stereo
    return bytes(struct.pack("<" + "h" * len(samples), *samples))


def main():
    parser = argparse.ArgumentParser(description="UDP audio test sender")
    parser.add_argument("--ip", default=DEST_IP)
    parser.add_argument("--port", type=int, default=DEST_PORT)
    parser.add_argument("--freq", type=float, default=440.0)
    parser.add_argument("--duration", type=int, default=10)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    phase_step = args.freq / SAMPLE_RATE
    audio = generate_tone(args.freq, args.duration * 1000, phase_step)

    offset = 0
    sequence = 0
    timestamp = 0
    start = time.perf_counter()
    print(f"sending {args.freq} Hz to {args.ip}:{args.port} ...")
    while offset < len(audio):
        chunk = audio[offset:offset + PAYLOAD_SAMPLES * CHANNELS * 2]
        pkt = build_packet(sequence, timestamp, chunk)
        sock.sendto(pkt, (args.ip, args.port))
        sequence += 1
        timestamp += PAYLOAD_SAMPLES
        offset += len(chunk)
        # pace
        target = start + PACKET_INTERVAL * sequence
        sleep = target - time.perf_counter()
        if sleep > 0:
            time.sleep(sleep)
    print("done")
    sock.close()


if __name__ == "__main__":
    main()
