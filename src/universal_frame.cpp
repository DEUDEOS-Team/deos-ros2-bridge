#include "universal_frame.hpp"

#include <deos/deos_frame.h>
#include <cstring>

namespace deos {

static_assert(kUniversalHeaderSize == DEOS_UNIVERSAL_HEADER_SIZE, "Unexpected DEOS header size");

std::size_t encode_datagram(uint8_t* data, std::size_t capacity,
                            const DatagramHeader& header,
                            const void* payload, uint8_t length)
{
    if (capacity < 1 || length > DEOS_MAX_PAYLOAD_LEN || (length && !payload)) {
        return 0;
    }
    deos_message_t message{};
    message.destination = static_cast<deos_node_id_t>(header.destination);
    message.source = static_cast<deos_node_id_t>(header.source);
    message.priority = static_cast<deos_priority_t>(header.priority);
    message.version = header.version;
    message.message_class = static_cast<deos_message_class_t>(header.message_class);
    message.service = static_cast<deos_service_id_t>(header.service);
    message.command = header.command;
    message.sequence = header.sequence;
    message.payload_len = length;
    if (length) {
        std::memcpy(message.payload, payload, length);
    }
    const int size = deos_frame_encode(&message, data + 1, capacity - 1);
    if (size < 0) {
        return 0;
    }
    data[0] = static_cast<uint8_t>(size);
    return 1 + static_cast<std::size_t>(size);
}

bool decode_datagram(const uint8_t* data, std::size_t size,
                     DatagramHeader& header, uint8_t& length)
{
    if (!data || size < 1 + DEOS_UNIVERSAL_HEADER_SIZE || size != 1u + data[0]) {
        return false;
    }
    deos_message_t message{};
    if (deos_frame_decode(data + 1, size - 1, &message) != 0 ||
        message.version != DEOS_PROTOCOL_VERSION ||
        size != 1u + DEOS_UNIVERSAL_HEADER_SIZE + message.payload_len) {
        return false;
    }
    header = {static_cast<uint8_t>(message.destination), static_cast<uint8_t>(message.source),
              static_cast<uint8_t>(message.priority), message.version,
              static_cast<uint8_t>(message.message_class), static_cast<uint8_t>(message.service),
              message.command, message.sequence};
    length = message.payload_len;
    return true;
}

}
