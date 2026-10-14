#pragma once

#include <chrono>
#include <mutex>
#include <atomic>

namespace deos {

/**
 * @brief Safety Watchdog Configuration
 */
struct SafetyConfig {
    std::chrono::milliseconds cmd_vel_timeout{200};
    std::chrono::milliseconds autonomy_timeout{500};
    std::chrono::milliseconds motion_timeout{250};
    bool enable_timeouts{true};
};

/**
 * @brief Safety Supervisor and Watchdog Monitor (SRP - Single Responsibility Principle)
 * Manages safety timestamps, heartbeat/motion enables, and decides emergency failsafe stops.
 */
class SafetySupervisor {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit SafetySupervisor(const SafetyConfig& config = SafetyConfig{});

    /**
     * @brief Notify supervisor of a new cmd_vel command
     * @return true if vehicle motion is currently permitted, false if inhibited by safety
     */
    bool notify_cmd_vel();

    /**
     * @brief Update autonomy enable state
     */
    void set_autonomy_enabled(bool enabled);

    /**
     * @brief Update motion enable state
     */
    void set_motion_enabled(bool enabled);

    /**
     * @brief Check whether motion is currently allowed
     */
    bool is_motion_permitted() const;

    /**
     * @brief Periodically called watchdog evaluator
     * @return true if a zero-speed emergency stop should be commanded to the powertrain
     */
    bool evaluate_timeouts();

    /**
     * @brief Clear stop state after vehicle resumes active motion
     */
    void reset_stopped_flag();

    // Configuration getters & setters
    void set_config(const SafetyConfig& config);
    SafetyConfig config() const;

    bool is_autonomy_enabled() const { return is_autonomy_enabled_.load(); }
    bool is_motion_enabled() const { return is_motion_enabled_.load(); }
    bool is_stopped_on_timeout() const { return vehicle_stopped_on_timeout_.load(); }

private:
    SafetyConfig config_;
    mutable std::mutex mutex_;

    TimePoint last_cmd_vel_time_;
    TimePoint last_autonomy_enable_time_;
    TimePoint last_motion_enable_time_;

    std::atomic<bool> is_autonomy_enabled_{false};
    std::atomic<bool> is_motion_enabled_{false};
    std::atomic<bool> vehicle_stopped_on_timeout_{false};
};

} // namespace deos
