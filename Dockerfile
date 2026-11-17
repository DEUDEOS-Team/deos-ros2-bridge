FROM ros:jazzy-ros-base AS build
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
WORKDIR /ws
COPY package.xml CMakeLists.txt src/deos_ros_bridge/
COPY vendor/deos_protocol/ src/deos_ros_bridge/vendor/deos_protocol/
COPY include/ src/deos_ros_bridge/include/
COPY src/ src/deos_ros_bridge/src/
COPY launch/ src/deos_ros_bridge/launch/
COPY config/ src/deos_ros_bridge/config/
RUN source /opt/ros/jazzy/setup.bash && \
    CMAKE_BUILD_PARALLEL_LEVEL=2 colcon build --merge-install --install-base /opt/deos \
        --packages-select deos_ros_bridge --cmake-args -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release

FROM ros:jazzy-ros-base
COPY --from=build /opt/deos /opt/deos
COPY docker/entrypoint.sh /deos_entrypoint.sh
ENTRYPOINT ["/bin/bash", "/deos_entrypoint.sh"]
CMD ["ros2", "launch", "deos_ros_bridge", "bridge.launch.py"]
