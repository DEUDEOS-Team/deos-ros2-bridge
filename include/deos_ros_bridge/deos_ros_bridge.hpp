#pragma once

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/u_int8.hpp>
#include <sensor_msgs/msg/imu.hpp>

#include "deos_ros_bridge/deos_protocol.hpp"
#include "deos_ros_bridge/transport_interface.hpp"
#include "deos_ros_bridge/udp_transport.hpp"
#include "deos_ros_bridge/kinematics.hpp"
#include "deos_ros_bridge/safety_supervisor.hpp"
#include "deos_ros_bridge/deos_codec.hpp"
#include "deos_ros_bridge/message_dispatcher.hpp"

#include <thread>
#include <atomic>
#include <memory>

class DeosRosBridge : public rclcpp::Node
{
public:
    /**
     * @brief Default Constructor: standard ROS 2 node initialization
     */
    DeosRosBridge();

    /**
     * @brief Dependency-Injection Constructor (DIP): allows injecting custom/mock transport
     */
    explicit DeosRosBridge(std::shared_ptr<deos::ITransport> transport);

    ~DeosRosBridge() override;

private:
    // ROS 2 Subscribers Callbacks (ROS 2 -> Vehicle)
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

    // ROS 2 Publishers (Vehicle -> ROS 2)
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

    // Core Domain Objects (OOP Components)
    std::shared_ptr<deos::ITransport> transport_;
    std::unique_ptr<deos::AckermannKinematics> kinematics_;
    std::unique_ptr<deos::SafetySupervisor> safety_;
    std::unique_ptr<deos::DeosCodec> codec_;
    std::unique_ptr<deos::DeosMessageDispatcher> dispatcher_;

    // Background Thread
    std::thread rx_thread_;
    std::atomic<bool> running_{true};
    std::atomic<uint8_t> current_bridge_state_{DEOS_STATE_STANDBY};

    // Helper Methods
    void init_node();
    void init_handlers();
    void send_heartbeat();
    void check_timeouts();
    void handle_incoming_data(const uint8_t* data, size_t length);
    void set_light_state(uint8_t light_id, bool enable);
};
