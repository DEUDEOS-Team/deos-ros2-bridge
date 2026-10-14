#!/usr/bin/env python3
"""
DEOS Mock STM32 UDP Server for ROS 2 Bridge Testing
Simulates eth_ros_bridge.cpp and ImuSensor.cpp on the STM32
"""

import socket
import struct
import time
import sys

STM32_BIND_IP = "0.0.0.0"
STM32_BIND_PORT = 5001

DEOS_SERVICE_SYSTEM = 0x00
DEOS_SERVICE_TRACTION = 0x01
DEOS_SERVICE_STEERING = 0x02
DEOS_SERVICE_SENSORS = 0x07
DEOS_SERVICE_LIGHTING = 0x08

DEOS_CMD_SYSTEM_HEARTBEAT = 0x07
DEOS_CMD_TRACTION_SET_TARGET_SPEED = 0x01
DEOS_CMD_TRACTION_SET_GEAR = 0x04
DEOS_CMD_STEERING_SET_TARGET_ANGLE = 0x01
DEOS_CMD_SENSOR_IMU_DATA = 0x01
DEOS_CMD_LIGHTING_SET_STATE = 0x01

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((STM32_BIND_IP, STM32_BIND_PORT))
    sock.settimeout(0.1)

    print(f"[Mock STM32] Listening on {STM32_BIND_IP}:{STM32_BIND_PORT}")
    print("[Mock STM32] Waiting for first Heartbeat/packet from ROS 2 Bridge...")

    client_addr = None
    counter = 0
    seq = 0
    last_imu_time = time.time()

    try:
        while True:
            # 1. Receive incoming commands from ROS 2 Bridge
            try:
                data, addr = sock.recvfrom(2048)
                if client_addr is None or client_addr != addr:
                    client_addr = addr
                    print(f"[Mock STM32] Discovered ROS 2 Bridge Client at {client_addr}!")

                if len(data) >= 9:
                    length = data[0]
                    prio, mclass, srv, dst, src, ver, rx_seq, cmd = struct.unpack('<8B', data[1:9])
                    payload = data[9:1+length]

                    if srv == DEOS_SERVICE_SYSTEM and cmd == DEOS_CMD_SYSTEM_HEARTBEAT:
                        state = payload[0] if len(payload) >= 1 else 0
                        print(f"  [RX Heartbeat] from Node 0x{src:02X}, State: 0x{state:02X}")
                    elif srv == DEOS_SERVICE_TRACTION and cmd == DEOS_CMD_TRACTION_SET_TARGET_SPEED:
                        speed, accel = struct.unpack('<Hh', payload[:4])
                        print(f"  [RX Traction] Target Speed: {speed * 0.01:.2f} m/s, Accel: {accel * 0.01:.2f} m/s^2")
                    elif srv == DEOS_SERVICE_STEERING and cmd == DEOS_CMD_STEERING_SET_TARGET_ANGLE:
                        angle, ang_vel = struct.unpack('<hH', payload[:4])
                        print(f"  [RX Steering] Target Angle: {angle * 0.01:.2f} deg, AngVel: {ang_vel * 0.01:.2f} deg/s")
                    elif srv == DEOS_SERVICE_LIGHTING:
                        print(f"  [RX Lighting] Cmd: 0x{cmd:02X}, Payload: {payload.hex()}")
                    else:
                        print(f"  [RX DEOS] Srv: 0x{srv:02X}, Cmd: 0x{cmd:02X}, Len: {length}")
            except socket.timeout:
                pass

            # 2. If client is discovered, stream simulated 10 Hz IMU telemetry back to ROS 2 Bridge
            now = time.time()
            if client_addr and (now - last_imu_time) >= 0.100:
                last_imu_time = now
                counter += 1

                # 18-byte IMU payload (9 x int16): accel_x/y/z, gyro_x/y/z, mag_x/y/z
                accel_x, accel_y, accel_z = 100, 50, 981
                gyro_x, gyro_y = 0, 0
                gyro_z = (counter % 200) - 100 # Swaying -1.0 to +1.0 deg/s
                mag_x, mag_y, mag_z = 0, 0, 0

                imu_payload = struct.pack('<9h', accel_x, accel_y, accel_z,
                                                 gyro_x, gyro_y, gyro_z,
                                                 mag_x, mag_y, mag_z)

                # Length = 8 (header) + 18 (payload) = 26
                header = struct.pack('<8B', 3, 2, DEOS_SERVICE_SENSORS, 0x04, 0x13, 0x11, seq, DEOS_CMD_SENSOR_IMU_DATA)
                seq = (seq + 1) & 0xFF
                packet = bytes([len(header) + len(imu_payload)]) + header + imu_payload

                sock.sendto(packet, client_addr)

    except KeyboardInterrupt:
        print("\n[Mock STM32] Stopping mock server.")
    finally:
        sock.close()

if __name__ == '__main__':
    main()
