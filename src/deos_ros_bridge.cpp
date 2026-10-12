#include "deos_ros_bridge/deos_ros_bridge.hpp"

#include <cstring>
#include <cmath>

using namespace std::chrono_literals;

DeosRosBridge::DeosRosBridge()
    : Node("deos_ros_bridge"), running_(true), tx_seq_(0),
      current_state_(DEOS_STATE_INIT),
      is_autonomy_enabled_(false), is_motion_enabled_(false)
{
    // Parameters
    this->declare_parameter<double>("wheelbase", 2.0);
    this->declare_parameter<std::string>("udp_bind_ip", "0.0.0.0");
    this->declare_parameter<int>("udp_bind_port", 5000);
    this->declare_parameter<std::string>("udp_remote_ip", "192.168.1.100");
    this->declare_parameter<int>("udp_remote_port", 5001);

    wheelbase_ = this->get_parameter("wheelbase").as_double();
    std::string bind_ip = this->get_parameter("udp_bind_ip").as_string();
    int bind_port = this->get_parameter("udp_bind_port").as_int();
    std::string remote_ip = this->get_parameter("udp_remote_ip").as_string();
    int remote_port = this->get_parameter("udp_remote_port").as_int();

    // QoS Profiles
    auto reliable_qos = rclcpp::QoS(1).reliable();
    auto transient_local_qos = rclcpp::QoS(1).reliable().transient_local();
    auto best_effort_qos = rclcpp::QoS(1).best_effort();
    auto best_effort_imu_qos = rclcpp::QoS(5).best_effort();

    // Subscribers
    cmd_vel_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
        "/cmd_vel", reliable_qos,
        std::bind(&DeosRosBridge::cmd_vel_callback, this, std::placeholders::_1));

    autonomy_enable_sub_ = this->create_subscription<std_msgs::msg::Bool>(
        "/vehicle/autonomy_enable", reliable_qos,
        std::bind(&DeosRosBridge::autonomy_enable_callback, this, std::placeholders::_1));

    motion_enable_sub_ = this->create_subscription<std_msgs::msg::Bool>(
        "/vehicle/motion_enable", reliable_qos,
        std::bind(&DeosRosBridge::motion_enable_callback, this, std::placeholders::_1));

    signals_right_sub_ = this->create_subscription<std_msgs::msg::Bool>(
        "/vehicle/signals/right", transient_local_qos,
        std::bind(&DeosRosBridge::signals_right_callback, this, std::placeholders::_1));

    signals_left_sub_ = this->create_subscription<std_msgs::msg::Bool>(
        "/vehicle/signals/left", transient_local_qos,
        std::bind(&DeosRosBridge::signals_left_callback, this, std::placeholders::_1));

    signals_hazard_sub_ = this->create_subscription<std_msgs::msg::Bool>(
        "/vehicle/signals/hazard", transient_local_qos,
        std::bind(&DeosRosBridge::signals_hazard_callback, this, std::placeholders::_1));

    lights_brake_sub_ = this->create_subscription<std_msgs::msg::Bool>(
        "/vehicle/lights/brake", transient_local_qos,
        std::bind(&DeosRosBridge::lights_brake_callback, this, std::placeholders::_1));

    lights_headlight_sub_ = this->create_subscription<std_msgs::msg::Bool>(
        "/vehicle/lights/headlight", transient_local_qos,
        std::bind(&DeosRosBridge::lights_headlight_callback, this, std::placeholders::_1));

    // Publishers
    speed_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/state/speed", best_effort_qos);
    steering_angle_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/state/steering_angle", best_effort_qos);
    imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>("/sensors/imu/data", best_effort_imu_qos);

    // UDP Init
    if (!udp_.init(bind_ip, bind_port, remote_ip, remote_port)) {
        RCLCPP_ERROR(this->get_logger(), "Failed to initialize UDP transport");
    }

    udp_.set_rx_callback([this](const uint8_t* data, size_t length) {
        this->handle_udp_rx(data, length);
    });

    rx_thread_ = std::thread([this]() {
        while (running_ && rclcpp::ok()) {
            udp_.poll();
        }
    });

    // Timers
    heartbeat_timer_ = this->create_wall_timer(
        1000ms, std::bind(&DeosRosBridge::send_heartbeat, this));

    timeout_timer_ = this->create_wall_timer(
        50ms, std::bind(&DeosRosBridge::check_timeouts, this));

    // Init timeout trackers
    last_cmd_vel_time_ = this->now();
    last_autonomy_enable_time_ = this->now();
    last_motion_enable_time_ = this->now();

    current_state_ = DEOS_STATE_IDLE;
    RCLCPP_INFO(this->get_logger(), "DeosRosBridge initialized.");
}

DeosRosBridge::~DeosRosBridge()
{
    running_ = false;
    if (rx_thread_.joinable()) {
        rx_thread_.join();
    }
}

void DeosRosBridge::cmd_vel_callback(const geometry_msgs::msg::Twist::SharedPtr msg)
{
    last_cmd_vel_time_ = this->now();

    if (!is_autonomy_enabled_ || !is_motion_enabled_) {
        // If not enabled, we may choose not to send commands, or send 0.
        // Returning to let the vehicle stop.
        return;
    }

    double v = msg->linear.x;
    double omega = msg->angular.z;
    double steering_angle_rad = 0.0;

    // Ackermann kinematics: tan(delta) = (omega * L) / v
    if (std::abs(v) > 0.01) {
        steering_angle_rad = std::atan((omega * wheelbase_) / v);
    } else if (std::abs(omega) > 0.01) {
        // In-place rotation command on an ackermann vehicle is not fully possible,
        // but we max out the steering angle in the requested direction.
        steering_angle_rad = (omega > 0) ? 0.6 : -0.6; 
    }

    // Convert rad to deg
    double steering_angle_deg = steering_angle_rad * 180.0 / M_PI;

    // Send Traction Command (0x01: SET_SPEED)
    // Scale: 0.01 m/s -> int16_t
    int16_t target_speed = static_cast<int16_t>(v * 100.0);
    uint32_t trac_can_id = build_can_id(DEOS_PRIO_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_TRACTION, DEOS_NODE_TRACTION, DEOS_NODE_MICRO_ROS);
    send_deos_message(trac_can_id, 0x01, (const uint8_t*)&target_speed, sizeof(target_speed));

    // Send Steering Command (0x01: SET_ANGLE)
    // Scale: 0.01 deg -> int16_t
    int16_t target_angle = static_cast<int16_t>(steering_angle_deg * 100.0);
    uint32_t steer_can_id = build_can_id(DEOS_PRIO_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_STEERING, DEOS_NODE_STEERING, DEOS_NODE_MICRO_ROS);
    send_deos_message(steer_can_id, 0x01, (const uint8_t*)&target_angle, sizeof(target_angle));
}

void DeosRosBridge::autonomy_enable_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    last_autonomy_enable_time_ = this->now();
    is_autonomy_enabled_ = msg->data;
}

void DeosRosBridge::motion_enable_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    last_motion_enable_time_ = this->now();
    is_motion_enabled_ = msg->data;
}

void DeosRosBridge::signals_right_callback(const std_msgs::msg::Bool::SharedPtr) {}
void DeosRosBridge::signals_left_callback(const std_msgs::msg::Bool::SharedPtr) {}
void DeosRosBridge::signals_hazard_callback(const std_msgs::msg::Bool::SharedPtr) {}
void DeosRosBridge::lights_brake_callback(const std_msgs::msg::Bool::SharedPtr) {}
void DeosRosBridge::lights_headlight_callback(const std_msgs::msg::Bool::SharedPtr) {}

void DeosRosBridge::check_timeouts()
{
    auto now = this->now();

    // cmd_vel timeout: 200 ms
    if ((now - last_cmd_vel_time_).seconds() > 0.200) {
        // cmd_vel timeout
    }

    // autonomy_enable timeout: 500 ms
    if ((now - last_autonomy_enable_time_).seconds() > 0.500) {
        is_autonomy_enabled_ = false;
    }

    // motion_enable timeout: 250 ms
    if ((now - last_motion_enable_time_).seconds() > 0.250) {
        is_motion_enabled_ = false;
    }
}

void DeosRosBridge::send_heartbeat()
{
    // DEOS Node ID = 0x04 (MICRO_ROS)
    // Priority = 5 (NETWORK)
    // Msg Class = 7 (NETWORK)
    // Service = 0 (SYSTEM)
    // Destination = FF (BROADCAST)
    uint32_t can_id = build_can_id(DEOS_PRIO_NETWORK, DEOS_CLASS_NETWORK, DEOS_SERVICE_SYSTEM, DEOS_NODE_GLOBAL_BROADCAST, DEOS_NODE_MICRO_ROS);
    
    deos_heartbeat_payload_t payload;
    payload.state = current_state_;

    send_deos_message(can_id, DEOS_CMD_SYSTEM_HEARTBEAT, (const uint8_t*)&payload, sizeof(payload));
}

void DeosRosBridge::send_deos_message(uint32_t can_id, uint8_t command, const uint8_t* payload, uint8_t length)
{
    deos_message_t msg;
    std::memset(&msg, 0, sizeof(msg));
    
    msg.can_id = can_id;
    msg.header.version = DEOS_PROTOCOL_VERSION;
    msg.header.sequence = tx_seq_++;
    msg.header.command = command;
    msg.header.length = length;
    
    if (payload && length > 0 && length <= 60) {
        std::memcpy(msg.payload, payload, length);
    }
    
    size_t total_len = sizeof(msg.can_id) + sizeof(msg.header) + length;
    udp_.send(&msg, total_len);
}

void DeosRosBridge::handle_udp_rx(const uint8_t* data, size_t length)
{
    if (length < (sizeof(uint32_t) + sizeof(deos_header_t))) {
        return; // Too short
    }
    
    const deos_message_t* msg = reinterpret_cast<const deos_message_t*>(data);
    
    // Quick sanity check on version
    if (msg->header.version != DEOS_PROTOCOL_VERSION) {
        return;
    }

    process_deos_message(*msg);
}

void DeosRosBridge::process_deos_message(const deos_message_t& msg)
{
    uint8_t prio, msg_class, service, dst, src;
    parse_can_id(msg.can_id, prio, msg_class, service, dst, src);

    // If it's a PING to us
    if (msg_class == DEOS_CLASS_NETWORK && service == DEOS_SERVICE_SYSTEM && 
        msg.header.command == DEOS_CMD_SYSTEM_PING && 
        dst == DEOS_NODE_MICRO_ROS && msg.header.length == 4) 
    {
        handle_ping(msg);
    }
    // Parse STATUS messages
    else if (msg_class == DEOS_CLASS_STATUS) 
    {
        if (service == DEOS_SERVICE_TRACTION && msg.header.command == 0x02 && msg.header.length == sizeof(deos_traction_status_t)) {
            deos_traction_status_t status;
            std::memcpy(&status, msg.payload, sizeof(status));

            // speed is 0.01 m/s
            float speed_ms = static_cast<float>(status.current_speed) * 0.01f;
            if (status.current_gear == 2) { // 2 = Reverse
                speed_ms = -speed_ms;
            }

            std_msgs::msg::Float32 speed_msg;
            speed_msg.data = speed_ms;
            speed_pub_->publish(speed_msg);
        }
        else if (service == DEOS_SERVICE_STEERING && msg.header.command == 0x02 && msg.header.length == sizeof(deos_steering_status_t)) {
            deos_steering_status_t status;
            std::memcpy(&status, msg.payload, sizeof(status));

            // angle is 0.01 deg
            float angle_deg = static_cast<float>(status.current_angle) * 0.01f;
            float angle_rad = angle_deg * M_PI / 180.0f;

            std_msgs::msg::Float32 angle_msg;
            angle_msg.data = angle_rad;
            steering_angle_pub_->publish(angle_msg);
        }
    }
}

void DeosRosBridge::handle_ping(const deos_message_t& msg)
{
    uint8_t prio, msg_class, service, dst, src;
    parse_can_id(msg.can_id, prio, msg_class, service, dst, src);
    
    deos_ping_payload_t payload;
    std::memcpy(&payload, msg.payload, 4);
    
    // PONG Destination alanini PING Source degerine esitler
    uint32_t pong_can_id = build_can_id(DEOS_PRIO_NETWORK, DEOS_CLASS_NETWORK, DEOS_SERVICE_SYSTEM, src, DEOS_NODE_MICRO_ROS);
    
    // PONG, Ping ID degerini degistirmeden aynen echo eder.
    send_deos_message(pong_can_id, DEOS_CMD_SYSTEM_PONG, (const uint8_t*)&payload, sizeof(payload));
    
    RCLCPP_DEBUG(this->get_logger(), "Replied PONG to Node %02X for Ping ID %u", src, payload.ping_id);
}
