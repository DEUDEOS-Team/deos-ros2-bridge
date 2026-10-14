#pragma once

#include <string>
#include <vector>
#include <functional>
#include <netinet/in.h>

class UdpTransport {
public:
    UdpTransport();
    ~UdpTransport();

    bool init(const std::string& bind_ip, uint16_t bind_port, const std::string& remote_ip, uint16_t remote_port);
    bool send(const void* data, size_t length);
    void close();

    // Rx callback
    using RxCallback = std::function<void(const uint8_t* data, size_t length)>;
    void set_rx_callback(RxCallback cb) { rx_callback_ = cb; }

    // Call this in a loop or thread to receive data
    void poll();

private:
    int socket_fd_;#include <cstdint>

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

// Node IDs
#define DEOS_NODE_TEXTUAL        0x01
#define DEOS_NODE_GROUND_CONTROL 0x02
#define DEOS_NODE_MAIN_STM32     0x03
#define DEOS_NODE_MICRO_ROS      0x04
#define DEOS_NODE_TRACTION       0x10
#define DEOS_NODE_STEERING       0x11
#define DEOS_NODE_BRAKE          0x12
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

// Version
#define DEOS_PROTOCOL_VERSION 0x11

#pragma pack(push, 1)

// Common Payload Header (4 bytes)
struct deos_header_t {
    uint8_t version;
    uint8_t sequence;
    uint8_t command;
    uint8_t length;
};

// Heartbeat payload (1 byte)
struct deos_heartbeat_payload_t {
    uint8_t state;
};

// Ping/Pong payload (4 bytes)
struct deos_ping_payload_t {
    uint32_t ping_id; // Little-Endian
};

// Traction Status Payload
struct deos_traction_status_t {
    uint16_t current_speed; // 0.01 m/s
    uint16_t motor_current; // 0.1 A
    int16_t inverter_temp;  // 0.1 C
    int16_t motor_temp;     // 0.1 C
    uint8_t current_gear;   // 0:N, 1:D, 2:R, 3:P
    uint8_t wheel_slip;     // 0:Normal, 1:Slipping
};

// Steering Status Payload
struct deos_steering_status_t {
    int16_t current_angle;    // 0.01 deg
    uint16_t motor_current;   // 0.1 A
    int16_t motor_temp;       // 0.1 C
    int16_t chassis_yaw_rate; // 0.01 deg/s
    uint8_t is_tracking;      // 0:No, 1:Yes
};

// Represents a UDP packet mapping to a DEOS CAN-FD frame
struct deos_message_t {
    uint32_t can_id;
    deos_header_t header;
    uint8_t payload[60];
};

#pragma pack(pop)

// Enums for State
enum deos_state_t : uint8_t {
    DEOS_STATE_UNKNOWN = 0,
    DEOS_STATE_INIT = 1,
    DEOS_STATE_IDLE = 2,
    DEOS_STATE_MANUAL = 3,
    DEOS_STATE_AUTONOMOUS = 4,
    DEOS_STATE_SAFETY = 5,
    DEOS_STATE_EMERGENCY = 6,
    DEOS_STATE_FAULT = 7
};

// CAN ID Builder
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

    struct sockaddr_in remote_addr_;
    RxCallback rx_callback_;
};
