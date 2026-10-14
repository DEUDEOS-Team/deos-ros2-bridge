#pragma once

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/u_int8.hpp>
#include <sensor_msgs/msg/imu.hpp>

#include "deos_ros_bridge/deos_protocol.hpp"
#include "deos_ros_bridge/udp_transport.hpp"
#include <thread>
#include <atomic>
#include <mutex>

class DeosRosBridge : public rclcpp::Node
{
public:
    DeosRosBridge();
    ~DeosRosBridge();

private:
    // ROS 2 Subscribers Callbacks (ROS 2 -> STM32)
    void cmd_vel_callback(const geometry_msgs::msg::Twist::SharedPtr msg);
    void autonomy_enable_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void motion_enable_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void set_state_callback(const std_msgs::msg::UInt8::SharedPtr msg);
    
    // Lighting Subscriptions
    void signals_right_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void signals_left_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void signals_hazard_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void lights_brake_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void lights_headlight_callback(const std_msgs::msg::Bool::SharedPtr msg);

    // ROS 2 Publishers (STM32 -> ROS 2)
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr speed_pub_;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr steering_angle_pub_;
    rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
    rclcpp::Publisher<std_msgs::msg::UInt8>::SharedPtr system_state_pub_;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr bms_voltage_pub_;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr bms_soc_pub_;

    // ROS 2 Subscribers
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr autonomy_enable_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr motion_enable_sub_;
    rclcpp::Subscription<std_msgs::msg::UInt8>::SharedPtr set_state_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr signals_right_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr signals_left_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr signals_hazard_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr lights_brake_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr lights_headlight_sub_;

    // Timers
    rclcpp::TimerBase::SharedPtr heartbeat_timer_;
    rclcpp::TimerBase::SharedPtr timeout_timer_;

    // UDP Transport & Worker Thread
    UdpTransport udp_;
    std::thread rx_thread_;
    std::atomic<bool> running_;

    // Protocol State
    uint8_t tx_seq_;
    uint8_t current_bridge_state_;
    
    // Safety & Timeouts
    rclcpp::Time last_cmd_vel_time_;
    rclcpp::Time last_autonomy_enable_time_;
    rclcpp::Time last_motion_enable_time_;
    
    bool is_autonomy_enabled_;
    bool is_motion_enabled_;
    bool enable_safety_timeouts_;
    bool vehicle_stopped_on_timeout_;

    // Vehicle Parameters
    double wheelbase_;
    double max_steering_angle_rad_;

    // Internal Protocol Handlers
    void send_heartbeat();
    void check_timeouts();
    void handle_udp_rx(const uint8_t* data, size_t length);
    void process_deos_frame(const deos_header_t& header, const uint8_t* payload, uint8_t payload_len);
    void send_deos_frame(uint8_t priority, uint8_t msg_class, uint8_t service,
                         uint8_t destination, uint8_t command,
                         const void* payload, uint8_t payload_length);
    void handle_ping(uint8_t src_node, const uint8_t* payload, uint8_t payload_len);
    void set_light_state(uint8_t light_id, bool enable, bool blink = false);
};
