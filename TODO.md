# DEOS ROS 2 Bridge TODO

## Aşama 1 - ROS 2 Interface
- [x] Publisher listesi (`/sensors/imu/data`, `/vehicle/state/speed`, `/vehicle/state/steering_angle`, `/vehicle/state/system_state`, `/vehicle/bms/*`)
- [x] Subscriber listesi (`/cmd_vel`, `/vehicle/autonomy_enable`, `/vehicle/motion_enable`, `/vehicle/set_state`, `/vehicle/lights/*`, `/vehicle/signals/*`)
- [x] QoS ayarları (Best Effort telemetry / Reliable commands)
- [x] Timeout timer'ları ve Safety Watchdog (cmd_vel 200ms, autonomy 500ms, motion 250ms)

## Aşama 2 - UDP
- [x] UDP socket (Non-blocking / poll-based background thread)
- [x] RX (Stream unpack, Length validation, DEOS header parse)
- [x] TX (Length prefix + 8B Header + NB payload)
- [x] IP/port config (Varsayılan STM32: 192.168.1.10:5001, Bind: 0.0.0.0:5000)

## Aşama 3 - DEOS Protocol
- [x] Node ID 0x04 (`DEOS_NODE_MICRO_ROS`)
- [x] Header (Priority, Class, Service, Dest, Src, Version 0x11, Sequence, Command)
- [x] Command mapping (Traction 0x01, Steering 0x02, Brake 0x03, BMS 0x04, Sensors 0x07, Lighting 0x08)
- [x] Payload encode/decode (IMU 10Hz, Traction Speed/Gear, Steering Angle, Lighting States)
- [x] Ping/Pong (Otomatik PING yanıtlayıcı)
- [x] Heartbeat (1000ms periyodik STM32 handshake)

## Aşama 4 - Test
- [ ] Raspberry Pi üzerinde colcon build
- [ ] Topic publish/subscribe test (`ros2 topic echo /sensors/imu/data`, `ros2 topic pub /cmd_vel ...`)
- [ ] UDP loopback test
- [ ] STM32 entegrasyon testi
