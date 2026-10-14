cmake_minimum_required(VERSION 3.8)
project(deos_ros_bridge)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
# find_package(deos_msgs REQUIRED)

include_directories(include)

add_executable(deos_ros_bridge_node
  src/main.cpp
  src/deos_ros_bridge.cpp
  src/udp_transport.cpp
  src/kinematics.cpp
  src/safety_supervisor.cpp
  src/deos_codec.cpp
  src/message_dispatcher.cpp
)

target_include_directories(deos_ros_bridge_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

ament_target_dependencies(deos_ros_bridge_node
  rclcpp
  std_msgs
  geometry_msgs
  sensor_msgs
)

install(TARGETS deos_ros_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
)

ament_package()
