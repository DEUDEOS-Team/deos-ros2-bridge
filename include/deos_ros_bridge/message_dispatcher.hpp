#pragma once

#include "deos_ros_bridge/deos_protocol.hpp"

#include <functional>
#include <memory>

namespace deos {

class DeosMessageDispatcher {
public:
    using Handler = std::function<void(const deos_header_t&, const uint8_t*, uint8_t)>;

    explicit DeosMessageDispatcher(uint8_t local_node);
    ~DeosMessageDispatcher();

    void register_handler(uint8_t message_class, uint8_t service, uint8_t command, Handler handler);
    bool dispatch(const deos_header_t& header, const uint8_t* payload, uint8_t payload_len) const;

private:
    struct Registry;
    std::unique_ptr<Registry> registry_;
};

}
