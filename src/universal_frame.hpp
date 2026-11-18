#pragma once

#include "deos_ros_bridge/deos_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace deos {

constexpr std::size_t kUniversalHeaderSize = DEOS_UNIVERSAL_HEADER_SIZE;

using DatagramHeader = ::deos_header_t;

class DatagramRegistry {
public:
    using Handler = std::function<void(const DatagramHeader&, const uint8_t*, uint8_t)>;
    explicit DatagramRegistry(uint8_t node);
    ~DatagramRegistry();

    void add(uint8_t message_class, uint8_t service, uint8_t command, Handler handler);
    bool dispatch(const DatagramHeader& header, const uint8_t* payload, uint8_t length) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

struct ImuSubscription {
    DatagramHeader header;
    std::array<uint8_t, 6> payload;
};

ImuSubscription imu_subscription(uint16_t interval_ms);

std::size_t encode_datagram(uint8_t* data, std::size_t capacity,
                            const DatagramHeader& header,
                            const void* payload, uint8_t length);
bool decode_datagram(const uint8_t* data, std::size_t size,
                     DatagramHeader& header, uint8_t& length);

}
