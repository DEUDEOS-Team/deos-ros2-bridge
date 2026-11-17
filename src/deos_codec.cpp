#include "deos_ros_bridge/deos_codec.hpp"

#include <cstring>
#include "universal_frame.hpp"

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
    uint8_t datagram[1 + kUniversalHeaderSize + DEOS_MAX_PAYLOAD_LEN];
    std::lock_guard<std::mutex> lock(tx_mutex_);
    const auto size = encode_datagram(datagram, sizeof(datagram),
        {destination, source_node_id_, priority, DEOS_PROTOCOL_VERSION,
         msg_class, service, command, tx_seq_}, payload, payload_length);
    if (size == 0) {
        return false;
    }
    ++tx_seq_;
    return transport.send(datagram, size);
}

bool DeosCodec::decode_frame(const uint8_t* data, size_t length,
                             deos_header_t& out_header,
                             const uint8_t*& out_payload,
                             uint8_t& out_payload_len) const
{
    DatagramHeader header{};
    if (!decode_datagram(data, length, header, out_payload_len)) {
        return false;
    }
    out_header.destination = header.destination;
    out_header.source = header.source;
    out_header.priority = header.priority;
    out_header.version = header.version;
    out_header.message_class = header.message_class;
    out_header.service = header.service;
    out_header.command = header.command;
    out_header.sequence = header.sequence;
    out_payload = data + 1 + kUniversalHeaderSize;
    return true;
}

} // namespace deos
