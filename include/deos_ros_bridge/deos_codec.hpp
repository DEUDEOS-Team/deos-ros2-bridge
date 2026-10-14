#pragma once

#include "deos_ros_bridge/deos_protocol.hpp"
#include "deos_ros_bridge/transport_interface.hpp"

#include <mutex>
#include <cstdint>

namespace deos {

/**
 * @brief DEOS Protocol Frame Codec (SRP - Single Responsibility Principle)
 * Handles wire serialization, sequence numbers, zero-copy transmission, and frame validation.
 */
class DeosCodec {
public:
    explicit DeosCodec(uint8_t source_node_id = DEOS_NODE_MICRO_ROS);

    /**
     * @brief Encode and send a frame via ITransport using Zero-Copy scatter-gather
     */
    bool send_frame(ITransport& transport,
                    uint8_t priority, uint8_t msg_class, uint8_t service,
                    uint8_t destination, uint8_t command,
                    const void* payload, uint8_t payload_length);

    /**
     * @brief Validate and unpack an incoming raw UDP buffer into header and payload pointers
     * @return true if valid DEOS frame, false if corrupted or version mismatch
     */
    bool decode_frame(const uint8_t* data, size_t length,
                      deos_header_t& out_header,
                      const uint8_t*& out_payload,
                      uint8_t& out_payload_len) const;

    uint8_t current_sequence() const { return tx_seq_; }
    uint8_t source_node_id() const { return source_node_id_; }
    void set_source_node_id(uint8_t id) { source_node_id_ = id; }

private:
    uint8_t source_node_id_;
    std::mutex tx_mutex_;
    uint8_t tx_seq_{0};
};

} // namespace deos
