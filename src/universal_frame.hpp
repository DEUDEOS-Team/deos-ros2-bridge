#pragma once

#include <cstddef>
#include <cstdint>

namespace deos {

constexpr std::size_t kUniversalHeaderSize = 9;

struct DatagramHeader {
    uint8_t destination;
    uint8_t source;
    uint8_t priority;
    uint8_t version;
    uint8_t message_class;
    uint8_t service;
    uint8_t command;
    uint8_t sequence;
};

std::size_t encode_datagram(uint8_t* data, std::size_t capacity,
                            const DatagramHeader& header,
                            const void* payload, uint8_t length);
bool decode_datagram(const uint8_t* data, std::size_t size,
                     DatagramHeader& header, uint8_t& length);

}
