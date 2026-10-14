#include "deos_ros_bridge/deos_ros_bridge.hpp"

using namespace std::chrono_literals;

DeosRosBridge::DeosRosBridge()
    : Node("deos_ros_bridge"),
      transport_(nullptr)
{
    init_node();
}

DeosRosBridge::DeosRosBridge(std::shared_ptr<deos::ITransport> transport)
    : Node("deos_ros_bridge"),
      transport_(std::move(transport))
{
    init_node();
}

DeosRosBridge::~DeosRosBridge()
{
    running_.store(false);
    if (transport_) {
        transport_->close();
    }
    if (rx_thread_.joinable()) {
        rx_thread_.join();
    }
    RCLCPP_INFO(this->get_logger(), "DEOS ROS 2 Bridge Node shut down cleanly.");
}

void DeosRosBridge::init_node()
{
    // -----------------------------------------------------------------
    // 1. Declare and Load ROS 2 Parameters
    // -----------------------------------------------------------------
    this->declare_parameter<double>("wheelbase", 2.0);
    this->declare_parameter<double>("max_steering_angle_rad", 0.60);
    this->declare_parameter<std::string>("udp_remote_ip", "192.168.1.10");
    this->declare_parameter<int>("udp_remote_port", 5001);
    this->declare_parameter<int>("heartbeat_period_ms", 1000);
    this->declare_parameter<int>("cmd_vel_timeout_ms", 200);
    this->declare_parameter<int>("autonomy_timeout_ms", 500);
    this->declare_parameter<int>("motion_timeout_ms", 250);
    this->declare_parameter<bool>("enable_safety_timeouts", true);

    double wheelbase = this->get_parameter("wheelbase").as_double();
    double max_steering_angle = this->get_parameter("max_steering_angle_rad").as_double();
    std::string remote_ip = this->get_parameter("udp_remote_ip").as_string();
    int remote_port = this->get_parameter("udp_remote_port").as_int();
    int heartbeat_ms = this->get_parameter("heartbeat_period_ms").as_int();

    // -----------------------------------------------------------------
    // 2. Instantiate OOP Domain Components
    // -----------------------------------------------------------------
    kinematics_ = std::make_unique<deos::AckermannKinematics>(wheelbase, max_steering_angle);

    deos::SafetyConfig safety_cfg;
    safety_cfg.cmd_vel_timeout = std::chrono::milliseconds(this->get_parameter("cmd_vel_timeout_ms").as_int());
    safety_cfg.autonomy_timeout = std::chrono::milliseconds(this->get_parameter("autonomy_timeout_ms").as_int());
    safety_cfg.motion_timeout = std::chrono::milliseconds(this->get_parameter("motion_timeout_ms").as_int());
    safety_cfg.enable_timeouts = this->get_parameter("enable_safety_timeouts").as_bool();
    safety_ = std::make_unique<deos::SafetySupervisor>(safety_cfg);

    codec_ = std::make_unique<deos::DeosCodec>(DEOS_NODE_MICRO_ROS);
    dispatcher_ = std::make_unique<deos::DeosMessageDispatcher>();

    // -----------------------------------------------------------------
    // 3. Configure Transport (DIP - Dependency Inversion Principle)
    // -----------------------------------------------------------------
    if (!transport_) {
        auto udp = std::make_shared<UdpTransport>();
        if (!udp->init(remote_ip, static_cast<uint16_t>(remote_port))) {
            RCLCPP_ERROR(this->get_logger(), "Failed to initialize UDP transport to %s:%d",
                         remote_ip.c_str(), remote_port);
        } else {
            RCLCPP_INFO(this->get_logger(), "UDP Transport ready, target STM32: %s:%d",
                        remote_ip.c_str(), remote_port);
        }
        transport_ = udp;
    }

    // -----------------------------------------------------------------
    // 4. Configure QoS Profiles
    // -----------------------------------------------------------------
    auto reliable_qos = rclcpp::QoS(10).reliable();
    auto transient_local_qos = rclcpp::QoS(1).reliable().transient_local();
    auto telemetry_qos = rclcpp::QoS(10).best_effort();
    auto imu_qos = rclcpp::QoS(20).best_effort();

    // -----------------------------------------------------------------
    // 5. Create Subscribers (ROS 2 -> Vehicle)
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
    // 6. Create Publishers (Vehicle -> ROS 2)
    // -----------------------------------------------------------------
    speed_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/state/speed", telemetry_qos);
    steering_angle_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/state/steering_angle", telemetry_qos);
    imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>("/sensors/imu/data", imu_qos);
    system_state_pub_ = this->create_publisher<std_msgs::msg::UInt8>("/vehicle/state_machine/state", telemetry_qos);
    bms_voltage_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/bms/voltage", telemetry_qos);
    bms_soc_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vehicle/bms/soc", telemetry_qos);

    // -----------------------------------------------------------------
    // 7. Register Message Handlers (Dispatcher Pattern)
    // -----------------------------------------------------------------
    init_handlers();

    transport_->set_rx_callback([this](const uint8_t* data, size_t length) {
        this->handle_incoming_data(data, length);
    });

    // Start background receiver worker thread
    rx_thread_ = std::thread([this]() {
        while (running_.load() && rclcpp::ok()) {
            transport_->poll(50);
        }
    });

    // -----------------------------------------------------------------
    // 8. Timers
    // -----------------------------------------------------------------
    heartbeat_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(heartbeat_ms),
        std::bind(&DeosRosBridge::send_heartbeat, this));

    timeout_timer_ = this->create_wall_timer(
        50ms, std::bind(&DeosRosBridge::check_timeouts, this));

    RCLCPP_INFO(this->get_logger(), "DEOS ROS 2 Bridge Node initialized with Advanced OOP architecture (Node ID: 0x%02X).",
                codec_->source_node_id());
}

void DeosRosBridge::init_handlers()
{
    // 1. PING Request Handler
    dispatcher_->register_handler(DEOS_SERVICE_SYSTEM, DEOS_CMD_SYSTEM_PING,
        [this](const deos_header_t& header, const uint8_t* payload, uint8_t len) {
            if (len >= sizeof(deos_ping_payload_t)) {
                // Echo PONG response back with zero copy
                codec_->send_frame(*transport_, DEOS_PRIO_NETWORK, DEOS_CLASS_NETWORK, DEOS_SERVICE_SYSTEM,
                                   header.source, DEOS_CMD_SYSTEM_PONG,
                                   payload, sizeof(deos_ping_payload_t));
            }
        });

    // 2. Heartbeat Telemetry Handler
    dispatcher_->register_handler(DEOS_SERVICE_SYSTEM, DEOS_CMD_SYSTEM_HEARTBEAT,
        [this](const deos_header_t&, const uint8_t* payload, uint8_t len) {
            if (len >= sizeof(deos_heartbeat_payload_t)) {
                const auto* hb = reinterpret_cast<const deos_heartbeat_payload_t*>(payload);
                auto state_msg = std::make_unique<std_msgs::msg::UInt8>();
                state_msg->data = hb->state;
                system_state_pub_->publish(std::move(state_msg));
            }
        });

    // 3. IMU Telemetry Handler
    dispatcher_->register_handler(DEOS_SERVICE_SENSORS, DEOS_CMD_SENSOR_IMU_DATA,
        [this](const deos_header_t&, const uint8_t* payload, uint8_t len) {
            if (len == sizeof(deos_sensor_imu_payload_t)) {
                const auto* imu_raw = reinterpret_cast<const deos_sensor_imu_payload_t*>(payload);
                auto imu_msg = std::make_unique<sensor_msgs::msg::Imu>();
                imu_msg->header.stamp = this->now();
                imu_msg->header.frame_id = "imu_link";

                imu_msg->linear_acceleration.x = imu_raw->accel_x * 0.01;
                imu_msg->linear_acceleration.y = imu_raw->accel_y * 0.01;
                imu_msg->linear_acceleration.z = imu_raw->accel_z * 0.01;

                constexpr double DEG_TO_RAD = M_PI / 180.0;
                imu_msg->angular_velocity.x = (imu_raw->gyro_x * 0.01) * DEG_TO_RAD;
                imu_msg->angular_velocity.y = (imu_raw->gyro_y * 0.01) * DEG_TO_RAD;
                imu_msg->angular_velocity.z = (imu_raw->gyro_z * 0.01) * DEG_TO_RAD;

                imu_pub_->publish(std::move(imu_msg));
            }
        });

    // 4. Traction Telemetry Handler
    dispatcher_->register_handler(DEOS_SERVICE_TRACTION, DEOS_CMD_TRACTION_GET_STATUS,
        [this](const deos_header_t&, const uint8_t* payload, uint8_t len) {
            if (len == sizeof(deos_traction_status_t)) {
                const auto* trac_st = reinterpret_cast<const deos_traction_status_t*>(payload);
                float speed_ms = static_cast<float>(trac_st->current_speed) * 0.01f;
                if (trac_st->current_gear == DEOS_GEAR_REVERSE) {
                    speed_ms = -speed_ms;
                }
                auto spd_msg = std::make_unique<std_msgs::msg::Float32>();
                spd_msg->data = speed_ms;
                speed_pub_->publish(std::move(spd_msg));
            }
        });

    // 5. Steering Telemetry Handler
    dispatcher_->register_handler(DEOS_SERVICE_STEERING, DEOS_CMD_STEERING_GET_STATUS,
        [this](const deos_header_t&, const uint8_t* payload, uint8_t len) {
            if (len == sizeof(deos_steering_status_t)) {
                const auto* steer_st = reinterpret_cast<const deos_steering_status_t*>(payload);
                float angle_deg = static_cast<float>(steer_st->current_angle) * 0.01f;
                float angle_rad = angle_deg * static_cast<float>(M_PI / 180.0);

                auto ang_msg = std::make_unique<std_msgs::msg::Float32>();
                ang_msg->data = angle_rad;
                steering_angle_pub_->publish(std::move(ang_msg));
            }
        });

    // 6. BMS Telemetry Handler
    dispatcher_->register_handler(DEOS_SERVICE_BMS, DEOS_CMD_BMS_STATUS,
        [this](const deos_header_t&, const uint8_t* payload, uint8_t len) {
            if (len >= sizeof(deos_bms_status_payload_t)) {
                const auto* bms_st = reinterpret_cast<const deos_bms_status_payload_t*>(payload);
                auto volt_msg = std::make_unique<std_msgs::msg::Float32>();
                volt_msg->data = static_cast<float>(bms_st->pack_voltage) * 0.01f;
                bms_voltage_pub_->publish(std::move(volt_msg));

                auto soc_msg = std::make_unique<std_msgs::msg::Float32>();
                soc_msg->data = static_cast<float>(bms_st->state_of_charge) * 0.01f;
                bms_soc_pub_->publish(std::move(soc_msg));
            }
        });
}

void DeosRosBridge::handle_incoming_data(const uint8_t* data, size_t length)
{
    deos_header_t header;
    const uint8_t* payload = nullptr;
    uint8_t payload_len = 0;

    if (codec_->decode_frame(data, length, header, payload, payload_len)) {
        dispatcher_->dispatch(header, payload, payload_len);
    }
}

void DeosRosBridge::cmd_vel_callback(const geometry_msgs::msg::Twist::SharedPtr msg)
{
    if (!safety_->notify_cmd_vel()) {
        return; // Motion inhibited by safety supervisor
    }

    auto cmd = kinematics_->calculate(msg->linear.x, msg->angular.z);

    // 1. Send Gear Command
    deos_traction_gear_payload_t gear_pl;
    gear_pl.gear = cmd.gear;
    codec_->send_frame(*transport_, DEOS_PRIO_CRITICAL_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_TRACTION,
                       DEOS_NODE_TRACTION, DEOS_CMD_TRACTION_SET_GEAR,
                       &gear_pl, sizeof(gear_pl));

    // 2. Send Traction Target Speed (0.01 m/s)
    deos_traction_target_payload_t trac_pl;
    trac_pl.target_speed = static_cast<uint16_t>(cmd.speed_mps * 100.0);
    trac_pl.target_accel = 100; // 1.00 m/s^2
    codec_->send_frame(*transport_, DEOS_PRIO_CRITICAL_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_TRACTION,
                       DEOS_NODE_TRACTION, DEOS_CMD_TRACTION_SET_TARGET_SPEED,
                       &trac_pl, sizeof(trac_pl));

    // 3. Send Steering Target Angle (0.01 deg)
    deos_steering_target_payload_t steer_pl;
    steer_pl.target_angle = static_cast<int16_t>(cmd.steering_angle_deg * 100.0);
    steer_pl.target_angular_velocity = 3000; // 30.00 deg/s
    codec_->send_frame(*transport_, DEOS_PRIO_CRITICAL_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_STEERING,
                       DEOS_NODE_STEERING, DEOS_CMD_STEERING_SET_TARGET_ANGLE,
                       &steer_pl, sizeof(steer_pl));
}

void DeosRosBridge::autonomy_enable_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    safety_->set_autonomy_enabled(msg->data);
}

void DeosRosBridge::motion_enable_callback(const std_msgs::msg::Bool::SharedPtr msg)
{
    safety_->set_motion_enabled(msg->data);
}

void DeosRosBridge::set_state_callback(const std_msgs::msg::UInt8::SharedPtr msg)
{
    deos_set_state_payload_t pl;
    pl.req_state = msg->data;

    codec_->send_frame(*transport_, DEOS_PRIO_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_SYSTEM,
                       DEOS_NODE_MAIN_STM32, DEOS_CMD_SYSTEM_SET_STATE,
                       &pl, sizeof(pl));

    RCLCPP_INFO(this->get_logger(), "Sent SET_STATE command: 0x%02X to Main STM32", msg->data);
}

void DeosRosBridge::set_light_state(uint8_t light_id, bool enable)
{
    uint8_t cmd = enable ? DEOS_CMD_LIGHTING_TURN_ON : DEOS_CMD_LIGHTING_TURN_OFF;
    uint8_t payload = light_id;

    codec_->send_frame(*transport_, DEOS_PRIO_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_LIGHTING,
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

void DeosRosBridge::send_heartbeat()
{
    deos_heartbeat_payload_t hb;
    hb.state = current_bridge_state_.load();

    codec_->send_frame(*transport_, DEOS_PRIO_NETWORK, DEOS_CLASS_NETWORK, DEOS_SERVICE_SYSTEM,
                       DEOS_NODE_MAIN_STM32, DEOS_CMD_SYSTEM_HEARTBEAT,
                       &hb, sizeof(hb));
}

void DeosRosBridge::check_timeouts()
{
    if (safety_->evaluate_timeouts()) {
        deos_traction_target_payload_t stop_pl;
        stop_pl.target_speed = 0;
        stop_pl.target_accel = 200; // 2.0 m/s^2 deceleration
        codec_->send_frame(*transport_, DEOS_PRIO_CRITICAL_CONTROL, DEOS_CLASS_COMMAND, DEOS_SERVICE_TRACTION,
                           DEOS_NODE_TRACTION, DEOS_CMD_TRACTION_SET_TARGET_SPEED,
                           &stop_pl, sizeof(stop_pl));
    }
}
