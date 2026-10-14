#include "deos_ros_bridge/deos_ros_bridge.hpp"

#include <cstring>
#include <cmath>
#include <algorithm>

using namespace std::chrono_literals;

DeosRosBridge::DeosRosBridge()
    : Node("deos_ros_bridge"),
      running_(true),
      tx_seq_(0),
      current_bridge_state_(DEOS_STATE_STANDBY),
      is_autonomy_enabled_(false),
      is_motion_enabled_(false),
      vehicle_stopped_on_timeout_(false)
{
    // -----------------------------------------------------------------
    // 1. Declare and Load ROS 2 Parameters
    // -----------------------------------------------------------------
    this->declare_parameter<double>("wheelbase", 2.0);
    this->declare_parameter<double>("max_steering_angle_rad", 0.60); // ~34.4 degrees
    this->declare_parameter<std::string>("udp_remote_ip", "192.168.1.10");
    this->declare_parameter<int>("udp_remote_port", 5001);
    this->declare_parameter<int>("heartbeat_period_ms", 1000);
    this->declare_parameter<int>("cmd_vel_timeout_ms", 200);
    this->declare_parameter<int>("autonomy_timeout_ms", 500);
    this->declare_parameter<int>("motion_timeout_ms", 250);
    this->declare_parameter<bool>("enable_safety_timeouts", true);

    wheelbase_ = this->get_parameter("wheelbase").as_double();
    max_steering_angle_rad_ = this->get_parameter("max_steering_angle_rad").as_double();
    std::string remote_ip = this->get_parameter("udp_remote_ip").as_string();
    int remote_port = this->get_parameter("udp_remote_port").as_int();
    int heartbeat_ms = this->get_parameter("heartbeat_period_ms").as_int();
    enable_safety_timeouts_ = this->get_parameter("enable_safety_timeouts").as_bool();

    // -----------------------------------------------------------------
    // 2. Configure QoS Profiles
    // -----------------------------------------------------------------
    auto reliable_qos = rclcpp::QoS(10).reliable();
    auto transient_local_qos = rclcpp::QoS(1).reliable().transient_local();
    auto telemetry_qos = rclcpp::QoS(10).best_effort();
    auto imu_qos = rclcpp::QoS(20).best_effort();

    // -----------------------------------------------------------------
    // 3. Create Subscribers (ROS 2 -> STM32 Commands)
    // -----------------------------------------------------------------
    cmd_vel_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
        "/cmd_vel", reliable_qos,
        std::bind(&DeosRosBridge::cmd_vel_callback, this, std::placeholders::_1));

    autonomy_enable_sub_ = this->create_subscription<std_msgs::msg::Bool>(
        "/vehicle/autonomy_enable", reliable_qos,
        std::bind(&DeosRosBridge::autonomy_enable_callback, this, std::placeholders::_1));

    motion_enable_sub_ = this->create_subscription<std_msgs::msg::Bool>(
        "/vehicle/motion_enable", reliable_qos,
        std::bind(&DeosRosBridge::motion_enable_callback, this, std::placeholders::_1));

    set_state_sub_ = this->create_subscription<std_msgs::msg::UInt8>(
        "/vehicle/set_state", reliable_qos,
        std::bind(&DeosRosBridge::set_state_callback, this, std::placeholders::_1));

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

    // -----------------------------------------------------------------
    // 4. Create Publishers (STM32 -> ROS 2 Telemetry)
    // -----------------------------------------------------------------
    speed_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/state/speed", telemetry_qos);
    steering_angle_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/state/steering_angle", telemetry_qos);
    imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>("/sensors/imu/data", imu_qos);
    system_state_pub_ = this->create_publisher<std_msgs::msg::UInt8>("/vehicle/state_machine/state", telemetry_qos);
    bms_voltage_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/bms/voltage", telemetry_qos);
    bms_soc_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/bms/soc", telemetry_qos);

    // -----------------------------------------------------------------
    // 5. Initialize UDP Socket
    // -----------------------------------------------------------------
    if (!udp_.init(remote_ip, static_cast<uint16_t>(remote_port))) {
        RCLCPP_ERROR(this->get_logger(), "Failed to initialize UDP transport targeting %s:%d",
                     remote_ip.c_str(), remote_port);
    } else {
        RCLCPP_INFO(this->get_logger(), "UDP Transport ready, target STM32: %s:%d",
                    remote_ip.c_str(), remote_port);
    }

    udp_.set_rx_callback([this](const uint8_t* data, size_t length) {
        this->handle_udp_rx(data, length);
    });

    // Start background thread for non-blocking UDP reception
    rx_thread_ = std::thread([this]() {
        while (running_.load() && rclcpp::ok()) {
            udp_.poll(50); // 50ms timeout
        }
    });

    // -----------------------------------------------------------------
    // 6. Timers
    // -----------------------------------------------------------------
    heartbeat_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(heartbeat_ms),
        std::bind(&DeosRosBridge::send_heartbeat, this));

    timeout_timer_ = this->create_wall_timer(
        50ms, std::bind(&DeosRosBridge::check_timeouts, this));

    // Initialize timeout timestamps
    last_cmd_vel_time_ = this->now();
    last_autonomy_enable_time_ = this->now();
    last_motion_enable_time_ = this->now();

    RCLCPP_INFO(this->get_logger(), "DEOS ROS 2 Bridge Node initialized successfully (Node ID: 0x%02X).",
                DEOS_NODE_MICRO_ROS);
}

DeosRosBridge::~DeosRosBridge()
{
    running_.store(false);
    udp_.close();
    if (rx_thread_.joinable()) {
        rx_thread_.join();
    }
    RCLCPP_INFO(this->get_logger(), "DEOS ROS 2 Bridge Node shut down.");
}

// ---------------------------------------------------------------------
// Universal DEOS Frame Transmitter
// Framing: [1B Length] + [8B Header] + [NB Payload]
// ---------------------------------------------------------------------
void DeosRosBridge::send_deos_frame(uint8_t priority, uint8_t msg_class, uint8_t service,
                                    uint8_t destination, uint8_t command,
                                    const void* payload, uint8_t payload_length)
{
    if (payload_length > DEOS_MAX_PAYLOAD_LEN) {
        RCLCPP_WARN(this->get_logger(), "Payload length %u exceeds max %u, truncating",
                    payload_length, DEOS_MAX_PAYLOAD_LEN);
        payload_length = DEOS_MAX_PAYLOAD_LEN;
    }

    deos_wire_prefix_t prefix;
    prefix.length = sizeof(deos_header_t) + payload_length;
    prefix.header.priority = priority;
    prefix.header.message_class = msg_class;
    prefix.header.service = service;
    prefix.header.destination = destination;
    prefix.header.source = DEOS_NODE_MICRO_ROS; // Always 0x04
    prefix.header.version = DEOS_PROTOCOL_VERSION;
    prefix.header.command = command;

    struct iovec iov[2];
    iov[0].iov_base = &prefix;
    iov[0].iov_len = sizeof(prefix);

    size_t iovcnt = 1;
    if (payload != nullptr && payload_length > 0) {
        iov[1].iov_base = const_cast<void*>(payload);
        iov[1].iov_len = payload_length;
        iovcnt = 2;
    }

    // Thread-safe sequence assignment and zero-copy network transmission
    std::lock_guard<std::mutex> lock(tx_mutex_);
    prefix.header.sequence = tx_seq_++;
    udp_.send_iovec(iov, iovcnt);
}

// ---------------------------------------------------------------------
// Heartbeat Handshake
// ---------------------------------------------------------------------
void DeosRosBridge::send_heartbeat()
{
    deos_heartbeat_payload_t hb;
    hb.state = current_bridge_state_.load();

    // Priority = 5 (NETWORK), Class = 7 (NETWORK), Service = 0 (SYSTEM),
    // Destination = 0x03 (MAIN_STM32), Command = 0x07 (HEARTBEAT)
    send_deos_frame(DEOS_PRIO_NETWORK, DEOS_CLASS_NETWORK, DEOS_SERVICE_SYSTEM,
                    DEOS_NODE_MAIN_STM32, DEOS_CMD_SYSTEM_HEARTBEAT,
                    &hb, sizeof(hb));
}

// ---------------------------------------------------------------------
// Subscriber Callbacks
// ---------------------------------------------------------------------
void DeosRosBridge::cmd_vel_callback(const geometry_msgs::msg::Twist::SharedPtr msg)
{
    bool motion_allowed = true;
    {
        std::lock_guard<std::mutex> lock(safety_mutex_);
        last_cmd_vel_time_ = this->now();
        if (enable_safety_timeouts_ && (!is_autonomy_enabled_.load() || !is_motion_enabled_.load())) {
            motion_allowed = false;
        }
        vehicle_stopped_on_timeout_.store(false);
    }

    if (!motion_allowed) {
        return; // Motion disabled by safety supervisor
    }

    double v = msg->linear.x;
    double omega = msg->angular.z;
    double steering_angle_rad = 0.0;

    // Ackermann Kinematics: tan(delta) = (omega * L) / v
    if (std::abs(v) > 0.01) {
        steering_angle_rad = std::atan((omega * wheelbase_) / v);
    } else if (std::abs(omega) > 0.01) {
        // Turning while stationary
        steering_angle_rad = (omega > 0.0) ? max_steering_angle_rad_ : -max_steering_angle_rad_;
    }

    // Clamp steering angle
    steering_angle_rad = std::clamp(steering_angle_rad, -max_steering_angle_rad_, max_steering_angle_rad_);
    double steering_angle_deg = steering_angle_rad * 180.0 / M_PI;

    // Handle Gear & Speed
    uint8_t target_gear = DEOS_GEAR_DRIVE;
    if (v < 0.0) {
        target_gear = DEOS_GEAR_REVERSE;
        v = std::abs(v);
    }

    // 1. Send Gear Command
    deos_traction_gear_payload_t gear_pl;
    gear_pl.gear = target_gear;
    send_deos_frame(DEOS_PRIO_CRITICAL_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_TRACTION,
                    DEOS_NODE_TRACTION, DEOS_CMD_TRACTION_SET_GEAR,
                    &gear_pl, sizeof(gear_pl));

    // 2. Send Traction Target Speed (0.01 m/s)
    deos_traction_target_payload_t trac_pl;
    trac_pl.target_speed = static_cast<uint16_t>(v * 100.0);
    trac_pl.target_accel = 100; // Default acceleration: 1.00 m/s^2
    send_deos_frame(DEOS_PRIO_CRITICAL_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_TRACTION,
                    DEOS_NODE_TRACTION, DEOS_CMD_TRACTION_SET_TARGET_SPEED,
                    &trac_pl, sizeof(trac_pl));

    // 3. Send Steering Target Angle (0.01 deg)
    deos_steering_target_payload_t steer_pl;
    steer_pl.target_angle = static_cast<int16_t>(steering_angle_deg * 100.0);
    steer_pl.target_angular_velocity = 3000; // Default: 30.00 deg/s
    send_deos_frame(DEOS_PRIO_CRITICAL_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_STEERING,
                    DEOS_NODE_STEERING, DEOS_CMD_STEERING_SET_TARGET_ANGLE,
                    &steer_pl, sizeof(steer_pl));
}

void DeosRosBridge::autonomy_enable_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    std::lock_guard<std::mutex> lock(safety_mutex_);
    last_autonomy_enable_time_ = this->now();
    is_autonomy_enabled_.store(msg->data);
}

void DeosRosBridge::motion_enable_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    std::lock_guard<std::mutex> lock(safety_mutex_);
    last_motion_enable_time_ = this->now();
    is_motion_enabled_.store(msg->data);
}

void DeosRosBridge::set_state_callback(const std_msgs::msg::UInt8::SharedPtr msg)
{
    deos_set_state_payload_t pl;
    pl.req_state = msg->data;

    send_deos_frame(DEOS_PRIO_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_SYSTEM,
                    DEOS_NODE_MAIN_STM32, DEOS_CMD_SYSTEM_SET_STATE,
                    &pl, sizeof(pl));

    RCLCPP_INFO(this->get_logger(), "Sent SET_STATE command: 0x%02X to Main STM32", msg->data);
}

void DeosRosBridge::set_light_state(uint8_t light_id, bool enable)
{
    uint8_t cmd = enable ? DEOS_CMD_LIGHTING_TURN_ON : DEOS_CMD_LIGHTING_TURN_OFF;
    uint8_t payload = light_id; // 1 byte light_id

    send_deos_frame(DEOS_PRIO_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_LIGHTING,
                    DEOS_NODE_MAIN_STM32, cmd,
                    &payload, sizeof(payload));
}

void DeosRosBridge::signals_right_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    set_light_state(DEOS_LIGHT_TURN_RIGHT, msg->data);
}

void DeosRosBridge::signals_left_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    set_light_state(DEOS_LIGHT_TURN_LEFT, msg->data);
}

void DeosRosBridge::signals_hazard_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    set_light_state(DEOS_LIGHT_HAZARD, msg->data);
}

void DeosRosBridge::lights_brake_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    set_light_state(DEOS_LIGHT_BRAKE, msg->data);
}

void DeosRosBridge::lights_headlight_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    set_light_state(DEOS_LIGHT_HEADLIGHT, msg->data);
}

// ---------------------------------------------------------------------
// Safety Timeouts Watchdog
// ---------------------------------------------------------------------
void DeosRosBridge::check_timeouts()
{
    if (!enable_safety_timeouts_) return;

    bool should_stop = false;
    {
        std::lock_guard<std::mutex> lock(safety_mutex_);
        auto now = this->now();
        double cmd_vel_timeout = this->get_parameter("cmd_vel_timeout_ms").as_int() / 1000.0;
        double autonomy_timeout = this->get_parameter("autonomy_timeout_ms").as_int() / 1000.0;
        double motion_timeout = this->get_parameter("motion_timeout_ms").as_int() / 1000.0;

        bool cmd_vel_expired = (now - last_cmd_vel_time_).seconds() > cmd_vel_timeout;
        bool autonomy_expired = (now - last_autonomy_enable_time_).seconds() > autonomy_timeout;
        bool motion_expired = (now - last_motion_enable_time_).seconds() > motion_timeout;

        if (autonomy_expired) is_autonomy_enabled_.store(false);
        if (motion_expired) is_motion_enabled_.store(false);

        // If commands timed out or motion was disabled, issue a zero-speed stop to Traction ECU
        if ((cmd_vel_expired || !is_autonomy_enabled_.load() || !is_motion_enabled_.load()) && !vehicle_stopped_on_timeout_.load()) {
            should_stop = true;
            vehicle_stopped_on_timeout_.store(true);
        }
    }

    if (should_stop) {
        deos_traction_target_payload_t stop_pl;
        stop_pl.target_speed = 0;
        stop_pl.target_accel = 200; // 2.0 m/s^2 deceleration
        send_deos_frame(DEOS_PRIO_CRITICAL_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_TRACTION,
                        DEOS_NODE_TRACTION, DEOS_CMD_TRACTION_SET_TARGET_SPEED,
                        &stop_pl, sizeof(stop_pl));
    }
}

// ---------------------------------------------------------------------
// UDP RX Frame Handling
// ---------------------------------------------------------------------
void DeosRosBridge::handle_udp_rx(const uint8_t* data, size_t length)
{
    if (length < (1 + sizeof(deos_header_t))) {
        return; // Too short to even have a header
    }

    uint8_t frame_len = data[0];
    if (length != static_cast<size_t>(frame_len + 1)) {
        return; // Packet length mismatch
    }

    const deos_header_t* header = reinterpret_cast<const deos_header_t*>(&data[1]);
    if (header->version != DEOS_PROTOCOL_VERSION) {
        return; // Protocol version mismatch
    }

    uint8_t payload_len = frame_len - sizeof(deos_header_t);
    const uint8_t* payload = &data[1 + sizeof(deos_header_t)];

    process_deos_frame(*header, payload, payload_len);
}

void DeosRosBridge::process_deos_frame(const deos_header_t& header, const uint8_t* payload, uint8_t payload_len)
{
    // 1. PING Request -> Echo PONG back immediately
    if (header.message_class == DEOS_CLASS_NETWORK &&
        header.service == DEOS_SERVICE_SYSTEM &&
        header.command == DEOS_CMD_SYSTEM_PING &&
        header.destination == DEOS_NODE_MICRO_ROS)
    {
        handle_ping(header.source, payload, payload_len);
        return;
    }

    // 2. Heartbeat Telemetry
    if (header.service == DEOS_SERVICE_SYSTEM && header.command == DEOS_CMD_SYSTEM_HEARTBEAT) {
        if (payload_len >= sizeof(deos_heartbeat_payload_t)) {
            const auto* hb = reinterpret_cast<const deos_heartbeat_payload_t*>(payload);
            auto state_msg = std::make_unique<std_msgs::msg::UInt8>();
            state_msg->data = hb->state;
            system_state_pub_->publish(std::move(state_msg));
        }
        return;
    }

    // 3. SENSORS IMU Telemetry (10 Hz stream from STM32 ImuSensor.cpp)
    if (header.service == DEOS_SERVICE_SENSORS && header.command == DEOS_CMD_SENSOR_IMU_DATA) {
        if (payload_len == sizeof(deos_sensor_imu_payload_t)) {
            const auto* imu_raw = reinterpret_cast<const deos_sensor_imu_payload_t*>(payload);

            auto imu_msg = std::make_unique<sensor_msgs::msg::Imu>();
            imu_msg->header.stamp = this->now();
            imu_msg->header.frame_id = "imu_link";

            // Convert 0.01 m/s^2 to m/s^2
            imu_msg->linear_acceleration.x = imu_raw->accel_x * 0.01;
            imu_msg->linear_acceleration.y = imu_raw->accel_y * 0.01;
            imu_msg->linear_acceleration.z = imu_raw->accel_z * 0.01;

            // Convert 0.01 deg/s to rad/s
            constexpr double DEG_TO_RAD = M_PI / 180.0;
            imu_msg->angular_velocity.x = (imu_raw->gyro_x * 0.01) * DEG_TO_RAD;
            imu_msg->angular_velocity.y = (imu_raw->gyro_y * 0.01) * DEG_TO_RAD;
            imu_msg->angular_velocity.z = (imu_raw->gyro_z * 0.01) * DEG_TO_RAD;

            imu_pub_->publish(std::move(imu_msg));
        }
        return;
    }

    // 4. TRACTION Telemetry Status
    if (header.service == DEOS_SERVICE_TRACTION && header.command == DEOS_CMD_TRACTION_GET_STATUS) {
        if (payload_len == sizeof(deos_traction_status_t)) {
            const auto* trac_st = reinterpret_cast<const deos_traction_status_t*>(payload);
            float speed_ms = static_cast<float>(trac_st->current_speed) * 0.01f;
            if (trac_st->current_gear == DEOS_GEAR_REVERSE) {
                speed_ms = -speed_ms;
            }

            auto spd_msg = std::make_unique<std_msgs::msg::Float32>();
            spd_msg->data = speed_ms;
            speed_pub_->publish(std::move(spd_msg));
        }
        return;
    }

    // 5. STEERING Telemetry Status
    if (header.service == DEOS_SERVICE_STEERING && header.command == DEOS_CMD_STEERING_GET_STATUS) {
        if (payload_len == sizeof(deos_steering_status_t)) {
            const auto* steer_st = reinterpret_cast<const deos_steering_status_t*>(payload);
            float angle_deg = static_cast<float>(steer_st->current_angle) * 0.01f;
            float angle_rad = angle_deg * static_cast<float>(M_PI / 180.0);

            auto ang_msg = std::make_unique<std_msgs::msg::Float32>();
            ang_msg->data = angle_rad;
            steering_angle_pub_->publish(std::move(ang_msg));
        }
        return;
    }

    // 6. BMS Telemetry Status
    if (header.service == DEOS_SERVICE_BMS && header.command == DEOS_CMD_BMS_STATUS) {
        if (payload_len >= sizeof(deos_bms_status_payload_t)) {
            const auto* bms_st = reinterpret_cast<const deos_bms_status_payload_t*>(payload);
            
            auto volt_msg = std::make_unique<std_msgs::msg::Float32>();
            volt_msg->data = static_cast<float>(bms_st->pack_voltage) * 0.01f;
            bms_voltage_pub_->publish(std::move(volt_msg));

            auto soc_msg = std::make_unique<std_msgs::msg::Float32>();
            soc_msg->data = static_cast<float>(bms_st->state_of_charge) * 0.01f;
            bms_soc_pub_->publish(std::move(soc_msg));
        }
        return;
    }
}

void DeosRosBridge::handle_ping(uint8_t src_node, const uint8_t* payload, uint8_t payload_len)
{
    if (payload_len < sizeof(deos_ping_payload_t)) return;

    // Zero-copy: Pass the incoming ping payload directly into send_deos_frame as PONG response
    send_deos_frame(DEOS_PRIO_NETWORK, DEOS_CLASS_NETWORK, DEOS_SERVICE_SYSTEM,
                    src_node, DEOS_CMD_SYSTEM_PONG,
                    payload, sizeof(deos_ping_payload_t));

    const auto* ping = reinterpret_cast<const deos_ping_payload_t*>(payload);
    RCLCPP_DEBUG(this->get_logger(), "Replied PONG to Node 0x%02X (Ping ID: %u)",
                 src_node, ping->ping_id);
}
