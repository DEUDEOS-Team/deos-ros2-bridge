#pragma once

#include <cstdint>

// Priorities
#define DEOS_PRIO_EMERGENCY        0
#define DEOS_PRIO_CRITICAL_CONTROL 1
#define DEOS_PRIO_CONTROL          2
#define DEOS_PRIO_STATUS           3
#define DEOS_PRIO_CONFIG           4
#define DEOS_PRIO_NETWORK          5
#define DEOS_PRIO_LOW              6
#define DEOS_PRIO_BACKGROUND       7

// Message Classes
#define DEOS_CLASS_SAFETY   0x0
#define DEOS_CLASS_COMMAND  0x1
#define DEOS_CLASS_STATUS   0x2
#define DEOS_CLASS_EVENT    0x3
#define DEOS_CLASS_REQUEST  0x4
#define DEOS_CLASS_RESPONSE 0x5
#define DEOS_CLASS_CONFIG   0x6
#define DEOS_CLASS_NETWORK  0x7

// Services
#define DEOS_SERVICE_SYSTEM      0x00
#define DEOS_SERVICE_TRACTION    0x01
#define DEOS_SERVICE_STEERING    0x02
#define DEOS_SERVICE_BRAKE       0x03
#define DEOS_SERVICE_BMS         0x04
#define DEOS_SERVICE_CALIBRATION 0x05
#define DEOS_SERVICE_DIAGNOSTIC  0x06
#define DEOS_SERVICE_SENSORS     0x07
#define DEOS_SERVICE_LIGHTING    0x08

// Node IDs
#define DEOS_NODE_INVALID        0x00
#define DEOS_NODE_TEXTUAL        0x01
#define DEOS_NODE_GROUND_CONTROL 0x02
#define DEOS_NODE_MAIN_STM32     0x03
#define DEOS_NODE_MICRO_ROS      0x04
#define DEOS_NODE_TRACTION       0x10
#define DEOS_NODE_STEERING       0x11
#define DEOS_NODE_BRAKE          0x12
#define DEOS_NODE_IMU            0x13
#define DEOS_NODE_BMS_MAIN       0x20
#define DEOS_NODE_BMS_AUX        0x21
#define DEOS_NODE_BMS_GATEWAY    0x30
#define DEOS_NODE_DIAG_TOOL      0xF0
#define DEOS_NODE_CAN_BROADCAST  0xFE
#define DEOS_NODE_GLOBAL_BROADCAST 0xFF

// System Commands
#define DEOS_CMD_SYSTEM_GET_STATE     0x01
#define DEOS_CMD_SYSTEM_SET_STATE     0x02
#define DEOS_CMD_SYSTEM_GET_MODE      0x03
#define DEOS_CMD_SYSTEM_SET_MODE      0x04
#define DEOS_CMD_SYSTEM_RESET_NODE    0x05
#define DEOS_CMD_SYSTEM_GET_NODE_INFO 0x06
#define DEOS_CMD_SYSTEM_HEARTBEAT     0x07
#define DEOS_CMD_SYSTEM_PING          0x08
#define DEOS_CMD_SYSTEM_PONG          0x09

// Traction Commands
#define DEOS_CMD_TRACTION_SET_TARGET_SPEED 0x01
#define DEOS_CMD_TRACTION_GET_STATUS       0x02
#define DEOS_CMD_TRACTION_SET_GEAR         0x04
#define DEOS_CMD_TRACTION_SET_TORQUE_LIMIT 0x05

// Steering Commands
#define DEOS_CMD_STEERING_SET_TARGET_ANGLE 0x01
#define DEOS_CMD_STEERING_GET_STATUS       0x02
#define DEOS_CMD_STEERING_SET_MODE         0x04

// Brake Commands
#define DEOS_CMD_BRAKE_SET_TARGET          0x01
#define DEOS_CMD_BRAKE_GET_STATUS          0x02
#define DEOS_CMD_BRAKE_SET_MODE            0x04
#define DEOS_CMD_BRAKE_SET_PARK            0x05
#define DEOS_CMD_BRAKE_SET_PREFILL         0x06

// Sensors Commands
#define DEOS_CMD_SENSOR_IMU_DATA           0x01

// Lighting Commands
#define DEOS_CMD_LIGHTING_TURN_ON          0x01
#define DEOS_CMD_LIGHTING_TURN_OFF         0x02
#define DEOS_CMD_LIGHTING_SET_ALL          0x03
#define DEOS_CMD_LIGHTING_GET_STATUS       0x04
#define DEOS_CMD_LIGHTING_STATUS           0x05

// Light IDs
#define DEOS_LIGHT_NONE                    0x00
#define DEOS_LIGHT_HEADLIGHT               0x01
#define DEOS_LIGHT_BRAKE                   0x02
#define DEOS_LIGHT_TURN_LEFT               0x03
#define DEOS_LIGHT_TURN_RIGHT              0x04
#define DEOS_LIGHT_HAZARD                  0x05
#define DEOS_LIGHT_ALL                     0xFF

// Light States
#define DEOS_LIGHT_STATE_OFF               0x00
#define DEOS_LIGHT_STATE_ON                0x01
#define DEOS_LIGHT_STATE_BLINK             0x02
#define DEOS_LIGHT_STATE_FAST_BLINK        0x03

// BMS Commands
#define DEOS_CMD_BMS_GET_STATUS            0x01
#define DEOS_CMD_BMS_STATUS                0x02

// Diagnostic Commands
#define DEOS_CMD_DIAG_GET_HEALTH           0x01
#define DEOS_CMD_DIAG_GET_FAULTS           0x02
#define DEOS_CMD_DIAG_CLEAR_FAULTS         0x03

// Protocol Version
#define DEOS_PROTOCOL_VERSION              0x11
#define DEOS_MAX_PAYLOAD_LEN               60

#pragma pack(push, 1)

/**
 * @brief DEOS Common 8-Byte Header on UDP/Ethernet wire
 * Matches offsetof(deos_message_t, payload) in deos-firmware eth_ros_bridge.cpp
 */
struct deos_header_t {
    uint8_t priority;
    uint8_t message_class;
    uint8_t service;
    uint8_t destination;
    uint8_t source;
    uint8_t version;
    uint8_t sequence;
    uint8_t command;
};

/**
 * @brief Wire UDP Frame format:
 * [Byte 0]: Length (8 + payload_len)
 * [Bytes 1..8]: Common Header
 * [Bytes 9..]: Payload
 */
struct deos_udp_frame_t {
    uint8_t length;
    deos_header_t header;
    uint8_t payload[DEOS_MAX_PAYLOAD_LEN];
};

// Heartbeat payload (1 byte)
struct deos_heartbeat_payload_t {
    uint8_t state;
};

// Ping/Pong payload (4 bytes)
struct deos_ping_payload_t {
    uint32_t ping_id; // Little-Endian
};

// Set State Command payload (1 byte)
struct deos_set_state_payload_t {
    uint8_t req_state;
};

// Traction Target Command payload (4 bytes)
struct deos_traction_target_payload_t {
    uint16_t target_speed; // 0.01 m/s (Little-Endian)
    int16_t  target_accel; // 0.01 m/s^2
};

// Traction Gear Command payload (1 byte)
struct deos_traction_gear_payload_t {
    uint8_t gear; // 0:Neutral, 1:Drive, 2:Reverse, 3:Park
};

// Traction Status Payload (10 bytes)
struct deos_traction_status_t {
    uint16_t current_speed; // 0.01 m/s
    uint16_t motor_current; // 0.1 A
    int16_t  inverter_temp; // 0.1 C
    int16_t  motor_temp;    // 0.1 C
    uint8_t  current_gear;  // 0:N, 1:D, 2:R, 3:P
    uint8_t  wheel_slip;    // 0:Normal, 1:Slipping
};

// Steering Target Command payload (4 bytes)
struct deos_steering_target_payload_t {
    int16_t  target_angle;            // 0.01 deg
    uint16_t target_angular_velocity; // 0.01 deg/s
};

// Steering Status Payload (8 bytes)
struct deos_steering_status_t {
    int16_t  current_angle;    // 0.01 deg
    uint16_t motor_current;   // 0.1 A
    int16_t  motor_temp;       // 0.1 C
    int16_t  chassis_yaw_rate; // 0.01 deg/s
    uint8_t  is_tracking;      // 0:No, 1:Yes
};

// Brake Target Command payload (4 bytes)
struct deos_brake_target_payload_t {
    uint16_t target_brake_demand; // 0.01 % (0..10000)
    int16_t  target_deceleration; // 0.01 m/s^2
};

// Brake Status Payload (9 bytes)
struct deos_brake_status_payload_t {
    uint16_t current_position;           // 0.01 mm
    uint16_t actuator_force;             // 1 N
    uint16_t motor_current;              // 0.1 A
    int16_t  chassis_longitudinal_accel; // 0.01 m/s^2
    uint8_t  brake_system_active;
    uint8_t  park_brake_state;
};

// SENSORS IMU Payload (18 bytes = 9 x int16_t)
struct deos_sensor_imu_payload_t {
    int16_t accel_x;    // 0.01 m/s^2
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;     // 0.01 deg/s
    int16_t gyro_y;
    int16_t gyro_z;
    int16_t mag_x;      // 1 uT
    int16_t mag_y;
    int16_t mag_z;
};

// Lighting Command Payload (1 byte: light_id)
struct deos_lighting_command_payload_t {
    uint8_t light_id;   // deos_light_id_t
};

// BMS Status Payload
struct deos_bms_status_payload_t {
    uint16_t pack_voltage;             // 0.01 V
    int16_t  pack_current;             // 0.1 A
    uint16_t state_of_charge;          // 0.01 %
    int16_t  bms_temperature;          // 0.1 degC
    uint16_t minimum_cell_voltage;     // 0.001 V
    uint16_t maximum_cell_voltage;     // 0.001 V
    uint16_t nominal_pack_voltage;     // 0.01 V
    uint8_t  cell_count;
    uint8_t  temperature_sensor_count;
};

#pragma pack(pop)

// Enums for Vehicle State
enum deos_state_t : uint8_t {
    DEOS_STATE_INIT        = 0x00,
    DEOS_STATE_STANDBY     = 0x01,
    DEOS_STATE_READY       = 0x02,
    DEOS_STATE_ACTIVE      = 0x03,
    DEOS_STATE_CALIBRATING = 0x04,
    DEOS_STATE_SAFE        = 0x05,
    DEOS_STATE_FAULT       = 0x06,
    DEOS_STATE_UNKNOWN     = 0xFF
};

// Traction Gears
enum deos_traction_gear_t : uint8_t {
    DEOS_GEAR_NEUTRAL = 0x00,
    DEOS_GEAR_DRIVE   = 0x01,
    DEOS_GEAR_REVERSE = 0x02,
    DEOS_GEAR_PARK    = 0x03
};

// CAN ID Helper Functions (For CAN-FD compatibility and logging)
inline uint32_t build_can_id(uint8_t priority, uint8_t msg_class, uint8_t service, uint8_t dst, uint8_t src) {
    return ((uint32_t)(priority & 0x7) << 26) |
           ((uint32_t)(msg_class & 0xF) << 22) |
           ((uint32_t)(service & 0x3F) << 16) |
           ((uint32_t)(dst & 0xFF) << 8) |
           ((uint32_t)(src & 0xFF));
}

inline void parse_can_id(uint32_t can_id, uint8_t& priority, uint8_t& msg_class, uint8_t& service, uint8_t& dst, uint8_t& src) {
    priority = (can_id >> 26) & 0x7;
    msg_class = (can_id >> 22) & 0xF;
    service = (can_id >> 16) & 0x3F;
    dst = (can_id >> 8) & 0xFF;
    src = can_id & 0xFF;
}
