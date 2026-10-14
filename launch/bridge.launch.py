import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

def generate_launch_description():
    # Locate package share directory
    pkg_share = get_package_share_directory('deos_ros_bridge')
    default_params_file = os.path.join(pkg_share, 'config', 'params.yaml')

    # Allow overriding parameter file at launch time:
    # ros2 launch deos_ros_bridge bridge.launch.py params_file:=/path/to/params.yaml
    params_file_arg = DeclareLaunchArgument(
        'params_file',
        default_value=default_params_file,
        description='Path to the ROS 2 parameters YAML configuration file'
    )

    # Bridge Node
    bridge_node = Node(
        package='deos_ros_bridge',
        executable='deos_ros_bridge_node',
        name='deos_ros_bridge',
        output='screen',
        emulate_tty=True,
        parameters=[LaunchConfiguration('params_file')]
    )

    return LaunchDescription([
        params_file_arg,
        bridge_node
    ])
