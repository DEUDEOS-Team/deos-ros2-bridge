#pragma once

#include <cstdint>
#include <cmath>

namespace deos {

/**
 * @brief Output command resulting from Ackermann Kinematics computation
 */
struct DriveCommand {
    double speed_mps{0.0};              ///< Positive linear speed in m/s
    double steering_angle_rad{0.0};     ///< Front wheel steering angle in radians
    double steering_angle_deg{0.0};     ///< Front wheel steering angle in degrees
    uint8_t gear{1};                    ///< Traction Gear (0:N, 1:D, 2:R, 3:P)
};

/**
 * @brief Ackermann Vehicle Kinematics Calculator (SRP - Single Responsibility Principle)
 * Converts Twist (linear velocity v, angular velocity omega) to front wheel steering angle & traction gear.
 */
class AckermannKinematics {
public:
    /**
     * @brief Constructor with vehicle physical parameters
     * @param wheelbase Distance between front and rear axle (L) in meters
     * @param max_steering_angle_rad Maximum allowable front wheel steering angle in radians
     */
    AckermannKinematics(double wheelbase = 2.0, double max_steering_angle_rad = 0.60);

    /**
     * @brief Compute drive command from linear and angular velocities
     * @param linear_x Forward/backward velocity (m/s)
     * @param angular_z Yaw rate / steering demand (rad/s)
     * @return DriveCommand with resolved speed, gear, and clamped steering angle
     */
    DriveCommand calculate(double linear_x, double angular_z) const;

    // Getters and Setters
    double wheelbase() const { return wheelbase_; }
    void set_wheelbase(double wb) { wheelbase_ = wb; }

    double max_steering_angle_rad() const { return max_steering_angle_rad_; }
    void set_max_steering_angle_rad(double max_rad) { max_steering_angle_rad_ = max_rad; }

private:
    double wheelbase_;
    double max_steering_angle_rad_;
};

} // namespace deos
