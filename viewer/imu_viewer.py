#!/usr/bin/env python3
"""
Live viewer for the BMI088 bring-up program (../bmi088_test).

Run bmi088_test on the Pi pointed at this machine's IP:

    ./bmi088_test <this-machine-ip> 5005

then on this machine:

    pip install -r requirements.txt
    python imu_viewer.py --port 5005

A background thread receives UDP packets and runs a complementary filter
(gyro integration corrected toward the accelerometer's gravity vector) to
get roll/pitch/yaw, plus a very rough double-integration of acceleration
to get an XYZ position. The main thread just redraws a 3D coordinate frame
and two bar-gauge panels at ~20 fps from whatever the latest fused state is.

Position WILL drift (there's no magnetometer, no GPS, nothing absolute to
correct against) -- that's expected for a 6-DOF IMU and not a bug. Press
'r' in the plot window to zero position/velocity/yaw back out.
"""
import argparse
import math
import socket
import struct
import threading
import time

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 (registers '3d' projection)

PACKET_FMT = "<IQ6f"  # seq, t_us, ax, ay, az, gx, gy, gz -- matches udp_stream.h
PACKET_SIZE = struct.calcsize(PACKET_FMT)

GRAVITY = 9.80665
COMPLEMENTARY_ALPHA = 0.98  # weight toward gyro-integrated angle vs. accel tilt
# Zero-velocity-update thresholds. Note this checks accel *magnitude*, which is a
# poor motion detector for lateral acceleration: sqrt(g^2 + a^2) ~= g + a^2/(2g)
# for small sideways a, so gentle, sustained lateral motion (below roughly
# sqrt(2*g*tol)) can still get misclassified as stationary and have its
# velocity zeroed. Fine for a demo; a real ZUPT would also look at variance
# over a short window, not just one sample.
STATIONARY_ACCEL_TOL = 0.15  # m/s^2 around 1g to call it "not accelerating"
STATIONARY_GYRO_TOL = 3.0  # deg/s


class ImuState:
    def __init__(self):
        self.lock = threading.Lock()
        self.roll = 0.0
        self.pitch = 0.0
        self.yaw = 0.0
        self.pos = np.zeros(3)
        self.vel = np.zeros(3)
        self.accel = np.zeros(3)
        self.gyro = np.zeros(3)
        self.last_t_us = None
        self.last_rx_wall = 0.0
        self.packet_count = 0

    def reset(self):
        with self.lock:
            self.roll = self.pitch = self.yaw = 0.0
            self.pos[:] = 0.0
            self.vel[:] = 0.0

    def snapshot(self):
        with self.lock:
            return {
                "roll": self.roll,
                "pitch": self.pitch,
                "yaw": self.yaw,
                "pos": self.pos.copy(),
                "accel": self.accel.copy(),
                "gyro": self.gyro.copy(),
                "last_rx_wall": self.last_rx_wall,
                "packet_count": self.packet_count,
            }


def rotation_matrix(roll_deg, pitch_deg, yaw_deg):
    """Body-to-world rotation, applied as Rz * Ry * Rx (yaw * pitch * roll)."""
    r, p, y = (math.radians(a) for a in (roll_deg, pitch_deg, yaw_deg))
    cr, sr = math.cos(r), math.sin(r)
    cp, sp = math.cos(p), math.sin(p)
    cy, sy = math.cos(y), math.sin(y)

    rz = np.array([[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]])
    ry = np.array([[cp, 0, sp], [0, 1, 0], [-sp, 0, cp]])
    rx = np.array([[1, 0, 0], [0, cr, -sr], [0, sr, cr]])

    return rz @ ry @ rx


def update_state(state, seq, t_us, ax, ay, az, gx, gy, gz):
    with state.lock:
        state.accel[:] = (ax, ay, az)
        state.gyro[:] = (gx, gy, gz)
        state.packet_count += 1
        state.last_rx_wall = time.time()

        if state.last_t_us is None:
            state.last_t_us = t_us
            return

        dt = (t_us - state.last_t_us) / 1e6
        state.last_t_us = t_us

        if dt <= 0 or dt > 0.5:
            return  # stale/out-of-order/garbled packet -- skip this integration step

        # Gyro-integrated angle (drifts slowly; accurate short-term).
        roll_gyro = state.roll + gx * dt
        pitch_gyro = state.pitch + gy * dt
        state.yaw += gz * dt  # no accel/mag correction possible for yaw

        # Accel-derived tilt (accurate long-term only while roughly static;
        # noisy/wrong under real acceleration, which is why we blend it in
        # lightly via the complementary filter rather than trusting it alone).
        roll_acc = math.degrees(math.atan2(ay, az))
        pitch_acc = math.degrees(math.atan2(-ax, math.sqrt(ay * ay + az * az)))

        state.roll = COMPLEMENTARY_ALPHA * roll_gyro + (1 - COMPLEMENTARY_ALPHA) * roll_acc
        state.pitch = COMPLEMENTARY_ALPHA * pitch_gyro + (1 - COMPLEMENTARY_ALPHA) * pitch_acc

        # Rough position via double integration, with a zero-velocity update
        # whenever the IMU looks stationary (close to 1g, low rotation rate)
        # to keep velocity from wandering off during quiet periods. Position
        # will still drift during real motion -- there's nothing absolute
        # (GPS/vision/mag) to anchor it to.
        accel_mag = math.sqrt(ax * ax + ay * ay + az * az)
        gyro_mag = math.sqrt(gx * gx + gy * gy + gz * gz)
        stationary = abs(accel_mag - GRAVITY) < STATIONARY_ACCEL_TOL and gyro_mag < STATIONARY_GYRO_TOL

        if stationary:
            state.vel[:] = 0.0
        else:
            r_mat = rotation_matrix(state.roll, state.pitch, state.yaw)
            accel_world = r_mat @ np.array([ax, ay, az])
            accel_world[2] -= GRAVITY
            state.vel += accel_world * dt

        state.pos += state.vel * dt


def udp_receiver_thread(sock, state, stop_event):
    sock.settimeout(0.5)
    while not stop_event.is_set():
        try:
            data, _ = sock.recvfrom(PACKET_SIZE)
        except socket.timeout:
            continue
        except OSError:
            break

        if len(data) != PACKET_SIZE:
            continue

        seq, t_us, ax, ay, az, gx, gy, gz = struct.unpack(PACKET_FMT, data)
        update_state(state, seq, t_us, ax, ay, az, gx, gy, gz)


def wrap_deg(angle):
    return ((angle + 180.0) % 360.0) - 180.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0", help="local address to listen on")
    parser.add_argument("--port", type=int, default=5005, help="UDP port bmi088_test is sending to")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))

    state = ImuState()
    stop_event = threading.Event()
    receiver = threading.Thread(target=udp_receiver_thread, args=(sock, state, stop_event), daemon=True)
    receiver.start()

    fig = plt.figure(figsize=(11, 6))
    fig.canvas.manager.set_window_title("BMI088 live viewer")
    grid = fig.add_gridspec(2, 2, width_ratios=[2, 1])
    ax3d = fig.add_subplot(grid[:, 0], projection="3d")
    ax_pos = fig.add_subplot(grid[0, 1])
    ax_rpy = fig.add_subplot(grid[1, 1])

    fig.canvas.mpl_connect("key_press_event", lambda e: state.reset() if e.key == "r" else None)

    axis_len = 0.5
    pos_range = 1.5
    unit = np.eye(3)
    colors = ["r", "g", "b"]

    def draw(_frame):
        snap = state.snapshot()
        r_mat = rotation_matrix(snap["roll"], snap["pitch"], snap["yaw"])
        origin = snap["pos"]
        stale = snap["packet_count"] == 0 or (time.time() - snap["last_rx_wall"]) > 1.0

        ax3d.cla()
        ax3d.set_title("waiting for data..." if stale else f"packets received: {snap['packet_count']}")
        ax3d.set_xlim(origin[0] - pos_range, origin[0] + pos_range)
        ax3d.set_ylim(origin[1] - pos_range, origin[1] + pos_range)
        ax3d.set_zlim(origin[2] - pos_range, origin[2] + pos_range)
        ax3d.set_xlabel("X")
        ax3d.set_ylabel("Y")
        ax3d.set_zlabel("Z")

        # Faint fixed world-frame reference triad at the origin.
        for i in range(3):
            d = unit[i] * axis_len * 0.6
            ax3d.quiver(0, 0, 0, d[0], d[1], d[2], color="lightgray", linewidth=1)

        # IMU body frame, rotated + translated to the current fused estimate.
        for i, label in enumerate(("X", "Y", "Z")):
            d = (r_mat @ unit[i]) * axis_len
            ax3d.quiver(origin[0], origin[1], origin[2], d[0], d[1], d[2], color=colors[i], linewidth=2)
            ax3d.text(origin[0] + d[0], origin[1] + d[1], origin[2] + d[2], label, color=colors[i])

        ax_pos.cla()
        ax_pos.set_title("Translation (m) -- drifts, press 'r' to reset")
        ax_pos.barh(["X", "Y", "Z"], snap["pos"], color=colors)
        ax_pos.set_xlim(-pos_range, pos_range)
        for i, v in enumerate(snap["pos"]):
            ax_pos.text(v, i, f"{v:+.2f}", va="center")

        ax_rpy.cla()
        ax_rpy.set_title("Roll / Pitch / Yaw (deg)")
        rpy = [snap["roll"], wrap_deg(snap["pitch"]), wrap_deg(snap["yaw"])]
        ax_rpy.barh(["Roll", "Pitch", "Yaw"], rpy, color=colors)
        ax_rpy.set_xlim(-180, 180)
        for i, v in enumerate(rpy):
            ax_rpy.text(v, i, f"{v:+.1f}", va="center")

        return []

    ani = FuncAnimation(fig, draw, interval=50, cache_frame_data=False)
    plt.tight_layout()

    try:
        plt.show()
    finally:
        stop_event.set()
        sock.close()
        receiver.join(timeout=1.0)

    _ = ani  # keep a reference alive for the duration of plt.show()


if __name__ == "__main__":
    main()
