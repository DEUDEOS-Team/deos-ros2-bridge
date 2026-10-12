#include "deos_ros_bridge/deos_ros_bridge.hpp"
#include <rclcpp/rclcpp.hpp>

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<DeosRosBridge>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
