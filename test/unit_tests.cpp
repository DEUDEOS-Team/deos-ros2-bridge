#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>

#include "deos_ros_bridge/transport_interface.hpp"
#include "deos_ros_bridge/kinematics.hpp"
#include "deos_ros_bridge/safety_supervisor.hpp"
#include "deos_ros_bridge/deos_codec.hpp"
#include "deos_ros_bridge/message_dispatcher.hpp"

// =====================================================================
// Mock Transport (Proves DIP & Mockability)
// =====================================================================
class MockTransport : public deos::ITransport {
public:
    std::vector<std::vector<uint8_t>> sent_packets;

    bool send(const void* data, size_t length) override {
        const auto* ptr = static_cast<const uint8_t*>(data);
        sent_packets.emplace_back(ptr, ptr + length);
        return true;
    }

    bool send_iovec(const struct iovec* iov, size_t iovcnt) override {
        std::vector<uint8_t> packet;
        for (size_t i = 0; i < iovcnt; ++i) {
            const auto* ptr = static_cast<const uint8_t*>(iov[i].iov_base);
            packet.insert(packet.end(), ptr, ptr + iov[i].iov_len);
        }
        sent_packets.push_back(std::move(packet));
        return true;
    }

    void poll(int /*timeout_ms*/) override {}
    void set_rx_callback(RxCallback cb) override { rx_cb_ = cb; }
    void close() override { open_ = false; }
    bool is_open() const override { return open_; }

    void simulate_rx(const uint8_t* data, size_t len) {
        if (rx_cb_) rx_cb_(data, len);
    }

private:
    bool open_{true};
    RxCallback rx_cb_;
};

// =====================================================================
// Tests
// =====================================================================
void test_kinematics() {
    std::cout << "[TEST] Running AckermannKinematics tests... ";
    deos::AckermannKinematics kin(2.0, 0.60);

    // Forward drive
    auto cmd1 = kin.calculate(2.0, 0.0);
    assert(cmd1.gear == DEOS_GEAR_DRIVE);
    assert(cmd1.speed_mps == 2.0);
    assert(std::abs(cmd1.steering_angle_rad) < 1e-4);

    // Reverse drive
    auto cmd2 = kin.calculate(-1.5, 0.0);
    assert(cmd2.gear == DEOS_GEAR_REVERSE);
    assert(cmd2.speed_mps == 1.5);

    // Stationary turning
    auto cmd3 = kin.calculate(0.0, 1.0);
    assert(cmd3.steering_angle_rad == 0.60);

    std::cout << "PASSED\n";
}

void test_safety_supervisor() {
    std::cout << "[TEST] Running SafetySupervisor tests... ";
    deos::SafetyConfig cfg;
    cfg.cmd_vel_timeout = std::chrono::milliseconds(50);
    cfg.autonomy_timeout = std::chrono::milliseconds(50);
    cfg.motion_timeout = std::chrono::milliseconds(50);
    cfg.enable_timeouts = true;

    deos::SafetySupervisor supervisor(cfg);

    // Initially disabled
    assert(!supervisor.is_motion_permitted());
    assert(!supervisor.notify_cmd_vel());

    // Enable both
    supervisor.set_autonomy_enabled(true);
    supervisor.set_motion_enabled(true);
    assert(supervisor.is_motion_permitted());
    assert(supervisor.notify_cmd_vel());

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    assert(supervisor.evaluate_timeouts()); // Should trigger emergency stop
    assert(!supervisor.evaluate_timeouts()); // Already stopped, shouldn't trigger again

    std::cout << "PASSED\n";
}

void test_codec_and_mock_transport() {
    std::cout << "[TEST] Running DeosCodec & MockTransport tests... ";
    MockTransport mock;
    deos::DeosCodec codec(DEOS_NODE_MICRO_ROS);

    deos_traction_gear_payload_t pl{DEOS_GEAR_DRIVE};
    bool ok = codec.send_frame(mock, DEOS_PRIO_CRITICAL_CONTROL, DEOS_CLASS_COMMAND,
                               DEOS_SERVICE_TRACTION, DEOS_NODE_TRACTION,
                               DEOS_CMD_TRACTION_SET_GEAR, &pl, sizeof(pl));

    assert(ok);
    assert(mock.sent_packets.size() == 1);
    const auto& pkt = mock.sent_packets[0];

    // Total length: 1 + 8 + 1 = 10 bytes
    assert(pkt.size() == 10);
    assert(pkt[0] == 9); // length byte: 8 + 1 = 9

    // Decode frame
    deos_header_t hdr;
    const uint8_t* payload_ptr = nullptr;
    uint8_t payload_len = 0;
    bool valid = codec.decode_frame(pkt.data(), pkt.size(), hdr, payload_ptr, payload_len);
    assert(valid);
    assert(hdr.service == DEOS_SERVICE_TRACTION);
    assert(hdr.command == DEOS_CMD_TRACTION_SET_GEAR);
    assert(hdr.source == DEOS_NODE_MICRO_ROS);
    assert(payload_len == 1);
    assert(payload_ptr[0] == DEOS_GEAR_DRIVE);

    std::cout << "PASSED\n";
}

void test_dispatcher() {
    std::cout << "[TEST] Running DeosMessageDispatcher tests... ";
    deos::DeosMessageDispatcher dispatcher;

    bool ping_called = false;
    dispatcher.register_handler(DEOS_SERVICE_SYSTEM, DEOS_CMD_SYSTEM_PING,
        [&ping_called](const deos_header_t&, const uint8_t*, uint8_t) {
            ping_called = true;
        });

    deos_header_t hdr{};
    hdr.service = DEOS_SERVICE_SYSTEM;
    hdr.command = DEOS_CMD_SYSTEM_PING;

    bool handled = dispatcher.dispatch(hdr, nullptr, 0);
    assert(handled);
    assert(ping_called);

    // Unhandled command
    hdr.command = 0xFE;
    assert(!dispatcher.dispatch(hdr, nullptr, 0));

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "   DEOS ADVANCED OOP UNIT TEST SUITE    \n";
    std::cout << "========================================\n";

    test_kinematics();
    test_safety_supervisor();
    test_codec_and_mock_transport();
    test_dispatcher();

    std::cout << "========================================\n";
    std::cout << "   ALL ADVANCED OOP TESTS PASSED (4/4)  \n";
    std::cout << "========================================\n\n";
    return 0;
}
