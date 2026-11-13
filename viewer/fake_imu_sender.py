#!/usr/bin/env python3
"""
Sends synthetic BMI088-shaped UDP packets, so you can test imu_viewer.py
without a Pi or any hardware at all. Simulates a slow yaw spin with a
gentle roll/pitch wobble and a small oscillating forward acceleration (so
you should see the axes spin, tilt a little, and drift slowly in the
viewer's position gauges).

Usage:
    python fake_imu_sender.py --host 127.0.0.1 --port 5005
"""
import argparse
import math
import socket
import struct
import time

PACKET_FMT = "<IQ6f"  # must match udp_stream.h / imu_viewer.py
GRAVITY = 9.80665


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--hz", type=float, default=100.0)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.host, args.port)
    dt = 1.0 / args.hz

    start = time.monotonic()
    seq = 0

    print(f"sending fake IMU data to {dest} at {args.hz} Hz -- Ctrl+C to stop")

    try:
        while True:
            t = time.monotonic() - start

            # Gentle roll/pitch wobble (as tilt angles, reflected into gravity's
            # components), a slow constant yaw spin, and a small oscillating
            # forward body-frame acceleration so position visibly drifts.
            roll = 15.0 * math.sin(2 * math.pi * 0.2 * t)
            pitch = 10.0 * math.sin(2 * math.pi * 0.15 * t + 1.0)
            roll_rate = 15.0 * 2 * math.pi * 0.2 * math.cos(2 * math.pi * 0.2 * t)
            pitch_rate = 10.0 * 2 * math.pi * 0.15 * math.cos(2 * math.pi * 0.15 * t + 1.0)
            yaw_rate = 15.0  # deg/s constant spin

            rr, pr = math.radians(roll), math.radians(pitch)
            ax = -GRAVITY * math.sin(pr) + 0.8 * math.sin(2 * math.pi * 0.1 * t)
            ay = GRAVITY * math.sin(rr) * math.cos(pr)
            az = GRAVITY * math.cos(rr) * math.cos(pr)

            t_us = int(t * 1e6)
            pkt = struct.pack(PACKET_FMT, seq, t_us, ax, ay, az, roll_rate, pitch_rate, yaw_rate)
            sock.sendto(pkt, dest)

            seq += 1
            time.sleep(dt)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()


if __name__ == "__main__":
    main()
