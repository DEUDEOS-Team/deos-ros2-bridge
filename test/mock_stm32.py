#!/usr/bin/env python3
"""Loopback UDP test peer for the Universal Header bridge."""

import argparse
from pathlib import Path
import socket
import struct
import sys
from time import monotonic

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "vendor/deos_protocol/python"))
import deos

IMU_KEY = bytes((deos.SERVICE_SENSORS, 1))
UNSUPPORTED = 1
LIGHTING_TURN_ON, LIGHTING_TURN_OFF = 1, 2
LIGHT_INDEX = {1: 0, 2: 1, 3: 2, 4: 3}


class MockStm32:
    def __init__(self, sock, reject_imu):
        self.socket = sock
        self.reject_imu = reject_imu
        self.client = None
        self.interval = None
        self.next_sample = None
        self.sequence = 0
        self.lights = [0] * 4

    def send(self, **fields):
        frame = deos.DeosMessage(destination=deos.NODE_ROS_BRIDGE,
                                 sequence=self.sequence, **fields).encode()
        self.sequence = (self.sequence + 1) % 256
        self.socket.sendto(bytes([len(frame)]) + frame, self.client)

    def handle(self, datagram, peer):
        if not datagram or datagram[0] != len(datagram) - 1:
            return
        message = deos.DeosMessage.decode(datagram[1:])
        if (message is None or message.source != deos.NODE_ROS_BRIDGE or
                message.version != deos.DEOS_PROTOCOL_VERSION or
                datagram[0] != deos.DEOS_UNIVERSAL_HEADER_SIZE + len(message.payload)):
            return
        print(f"RX {message}", flush=True)
        key = (message.destination, message.message_class, message.service, message.command)
        if key == (deos.NODE_MAIN_STM32, deos.CLASS_NETWORK, deos.SERVICE_SYSTEM,
                   deos.CMD_SYSTEM_PING) and len(message.payload) == 4:
            self.client = peer
            self.send(source=deos.NODE_MAIN_STM32, message_class=deos.CLASS_NETWORK,
                      priority=deos.PRIO_NETWORK, service=deos.SERVICE_SYSTEM,
                      command=deos.CMD_SYSTEM_PONG, payload=message.payload)
            return
        if (key[:3] == (deos.NODE_MAIN_STM32, deos.CLASS_COMMAND, deos.SERVICE_LIGHTING)
                and message.command in (LIGHTING_TURN_ON, LIGHTING_TURN_OFF)
                and len(message.payload) == 1 and message.payload[0] in LIGHT_INDEX):
            self.client = peer
            self.lights[LIGHT_INDEX[message.payload[0]]] = int(message.command == LIGHTING_TURN_ON)
            self.send(source=deos.NODE_MAIN_STM32, message_class=deos.CLASS_RESPONSE,
                      priority=message.priority, service=deos.SERVICE_LIGHTING,
                      command=message.command, payload=bytes([0, *self.lights, 0, 0]))
            return
        if key != (deos.NODE_IMU, deos.CLASS_REQUEST, deos.SERVICE_SYSTEM, deos.CMD_SYSTEM_SUBSCRIBE):
            return
        if len(message.payload) != 6 or message.payload[:2] != IMU_KEY:
            return
        interval, ttl = struct.unpack_from("<HH", message.payload, 2)
        if interval < 10 or interval == 0xFFFF or ttl != 0:
            return
        self.client = peer
        result = UNSUPPORTED if self.reject_imu else 0
        self.send(source=deos.NODE_IMU, message_class=deos.CLASS_RESPONSE,
                  priority=deos.PRIO_CONFIG, service=deos.SERVICE_SYSTEM,
                  command=deos.CMD_SYSTEM_SUBSCRIBE,
                  payload=bytes([result, result]) + IMU_KEY +
                  struct.pack("<H", 0 if result else interval))
        self.send(source=deos.NODE_MAIN_STM32, message_class=deos.CLASS_NETWORK,
                  priority=deos.PRIO_NETWORK, service=deos.SERVICE_SYSTEM,
                  command=deos.CMD_SYSTEM_PING, payload=b"\x12\x34\x56\x78")
        if result == 0:
            self.interval = interval / 1000
            self.next_sample = monotonic()

    def run(self):
        while True:
            now = monotonic()
            if self.next_sample is not None and now >= self.next_sample:
                self.send(source=deos.NODE_IMU, message_class=deos.CLASS_STATUS,
                          service=deos.SERVICE_SENSORS, command=IMU_KEY[1],
                          payload=struct.pack("<9h", 100, 50, 981, 0, 0, -100, 0, 0, 0))
                self.next_sample = now + self.interval
            self.socket.settimeout(max(0.001, self.next_sample - monotonic())
                                   if self.next_sample is not None else None)
            try:
                self.handle(*self.socket.recvfrom(128))
            except TimeoutError:
                pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=5001)
    parser.add_argument("--reject-imu", action="store_true")
    args = parser.parse_args()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", args.port))
        print("UDP test peer ready", flush=True)
        MockStm32(sock, args.reject_imu).run()


if __name__ == "__main__":
    main()
