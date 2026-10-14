#include "deos_ros_bridge/deos_codec.hpp"

#include <cstring>
#include <iostream>

namespace deos {

DeosCodec::DeosCodec(uint8_t source_node_id)
    : source_node_id_(source_node_id), tx_seq_(0)
{
}

bool DeosCodec::send_frame(ITransport& transport,
                           uint8_t priority, uint8_t msg_class, uint8_t service,
                           uint8_t destination, uint8_t command,
                           const void* payload, uint8_t payload_length)
{
    if (payload_length > DEOS_MAX_PAYLOAD_LEN) {
        payload_length = DEOS_MAX_PAYLOAD_LEN;
    }

    deos_wire_prefix_t prefix;
    prefix.length = sizeof(deos_header_t) + payload_length;
    prefix.header.priority = priority;
    prefix.header.message_class = msg_class;
    prefix.header.service = service;
    prefix.header.destination = destination;
    prefix.header.source = source_node_id_;
    prefix.header.version = DEOS_PROTOCOL_VERSION;
    prefix.header.command = command;

    struct iovec iov[2];
    iov[0].iov_base = &prefix;
    iov[0].iov_len = sizeof(prefix);

    size_t iovcnt = 1;
    if (payload != nullptr && payload_length > 0) {
        iov[1].iov_base = const_cast<void*>(payload);
        iov[1].iov_len = payload_length;
        iovcnt = 2;
    }

    // Atomic sequence increment and socket write
    std::lock_guard<std::mutex> lock(tx_mutex_);
    prefix.header.sequence = tx_seq_++;
    return transport.send_iovec(iov, iovcnt);
}

bool DeosCodec::decode_frame(const uint8_t* data, size_t length,
                             deos_header_t& out_header,
                             const uint8_t*& out_payload,
                             uint8_t& out_payload_len) const
{
    if (data == nullptr || length < (1 + sizeof(deos_header_t))) {
        return false;
    }

    uint8_t frame_len = data[0];
    if (length != static_cast<size_t>(frame_len + 1)) {
        return false; // Packet length mismatch
    }

    const auto* header = reinterpret_cast<const deos_header_t*>(&data[1]);
    if (header->version != DEOS_PROTOCOL_VERSION) {
        return false; // Protocol version mismatch
    }

    out_header = *header;
    out_payload_len = frame_len - sizeof(deos_header_t);
    out_payload = &data[1 + sizeof(deos_header_t)];
    return true;
}

} // namespace deos
