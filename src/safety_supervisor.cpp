#include "deos_ros_bridge/safety_supervisor.hpp"

namespace deos {

SafetySupervisor::SafetySupervisor(const SafetyConfig& config)
    : config_(config),
      last_cmd_vel_time_(Clock::now()),
      last_autonomy_enable_time_(Clock::now()),
      last_motion_enable_time_(Clock::now()),
      is_autonomy_enabled_(false),
      is_motion_enabled_(false),
      vehicle_stopped_on_timeout_(false)
{
}

bool SafetySupervisor::notify_cmd_vel() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_cmd_vel_time_ = Clock::now();
    vehicle_stopped_on_timeout_.store(false);

    if (config_.enable_timeouts && (!is_autonomy_enabled_.load() || !is_motion_enabled_.load())) {
        return false;
    }
    return true;
}

void SafetySupervisor::set_autonomy_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_autonomy_enable_time_ = Clock::now();
    is_autonomy_enabled_.store(enabled);
}

void SafetySupervisor::set_motion_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_motion_enable_time_ = Clock::now();
    is_motion_enabled_.store(enabled);
}

bool SafetySupervisor::is_motion_permitted() const {
    if (!config_.enable_timeouts) return true;
    return is_autonomy_enabled_.load() && is_motion_enabled_.load();
}

bool SafetySupervisor::evaluate_timeouts() {
    if (!config_.enable_timeouts) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    auto now = Clock::now();

    bool cmd_vel_expired = (now - last_cmd_vel_time_) > config_.cmd_vel_timeout;
    bool autonomy_expired = (now - last_autonomy_enable_time_) > config_.autonomy_timeout;
    bool motion_expired = (now - last_motion_enable_time_) > config_.motion_timeout;

    if (autonomy_expired) is_autonomy_enabled_.store(false);
    if (motion_expired) is_motion_enabled_.store(false);

    // If commands expired or enables dropped and stop hasn't been signaled yet
    if ((cmd_vel_expired || !is_autonomy_enabled_.load() || !is_motion_enabled_.load()) && 
        !vehicle_stopped_on_timeout_.load()) 
    {
        vehicle_stopped_on_timeout_.store(true);
        return true; // Command emergency stop
    }

    return false;
}

void SafetySupervisor::reset_stopped_flag() {
    vehicle_stopped_on_timeout_.store(false);
}

void SafetySupervisor::set_config(const SafetyConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

SafetyConfig SafetySupervisor::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

} // namespace deos
