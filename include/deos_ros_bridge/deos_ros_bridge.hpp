#pragma once

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/float32.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/range.hpp>

#include "deos_ros_bridge/deos_protocol.hpp"
#include "deos_ros_bridge/udp_transport.hpp"
#include <thread>

class DeosRosBridge : public rclcpp::Node
{
public:
    DeosRosBridge();
    ~DeosRosBridge();

private:
    // Callbacks
    void cmd_vel_callback(const geometry_msgs::msg::Twist::SharedPtr msg);
    void autonomy_enable_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void motion_enable_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void signals_right_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void signals_left_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void signals_hazard_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void lights_brake_callback(const std_msgs::msg::Bool::SharedPtr msg);
    void lights_headlight_callback(const std_msgs::msg::Bool::SharedPtr msg);

    // Publishers
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr speed_pub_;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr steering_angle_pub_;
    rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;

    // Subscribers
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr autonomy_enable_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr motion_enable_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr signals_right_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr signals_left_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr signals_hazard_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr lights_brake_sub_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr lights_headlight_sub_;

    // Timers
    rclcpp::TimerBase::SharedPtr heartbeat_timer_;
    rclcpp::TimerBase::SharedPtr timeout_timer_;

    // UDP Transport
    UdpTransport udp_;
    std::thread rx_thread_;
    bool running_;

    // Protocol State
    uint8_t tx_seq_;
    deos_state_t current_state_;
    
    // Timeouts
    rclcpp::Time last_cmd_vel_time_;
    rclcpp::Time last_autonomy_enable_time_;
    rclcpp::Time last_motion_enable_time_;
    
    // Variables
    bool is_autonomy_enabled_;
    bool is_motion_enabled_;
    double wheelbase_;

    // Internal methods
    void send_heartbeat();
    void check_timeouts();
    void handle_udp_rx(const uint8_t* data, size_t length);
    void process_deos_message(const deos_message_t& msg);
    void send_deos_message(uint32_t can_id, uint8_t command, const uint8_t* payload, uint8_t length);
    void handle_ping(const deos_message_t& msg);
};
