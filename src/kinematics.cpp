#include "deos_ros_bridge/kinematics.hpp"
#include "deos_ros_bridge/deos_protocol.hpp"

#include <algorithm>

namespace deos {

AckermannKinematics::AckermannKinematics(double wheelbase, double max_steering_angle_rad)
    : wheelbase_(wheelbase), max_steering_angle_rad_(max_steering_angle_rad)
{
}

DriveCommand AckermannKinematics::calculate(double linear_x, double angular_z) const {
    DriveCommand cmd;

    double v = linear_x;
    double omega = angular_z;
    double steering_angle_rad = 0.0;

    // Ackermann Kinematics: tan(delta) = (omega * L) / v
    if (std::abs(v) > 0.01) {
        steering_angle_rad = std::atan((omega * wheelbase_) / v);
    } else if (std::abs(omega) > 0.01) {
        // Turning while stationary
        steering_angle_rad = (omega > 0.0) ? max_steering_angle_rad_ : -max_steering_angle_rad_;
    }

    // Clamp steering angle within mechanical limits
    steering_angle_rad = std::clamp(steering_angle_rad, -max_steering_angle_rad_, max_steering_angle_rad_);
    double steering_angle_deg = steering_angle_rad * 180.0 / M_PI;

    // Handle Gear & Speed
    uint8_t target_gear = DEOS_GEAR_DRIVE;
    if (v < 0.0) {
        target_gear = DEOS_GEAR_REVERSE;
        v = std::abs(v);
    }

    cmd.speed_mps = v;
    cmd.steering_angle_rad = steering_angle_rad;
    cmd.steering_angle_deg = steering_angle_deg;
    cmd.gear = target_gear;

    return cmd;
}

} // namespace deos
