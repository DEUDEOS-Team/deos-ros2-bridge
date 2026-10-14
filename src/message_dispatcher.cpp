#include "deos_ros_bridge/message_dispatcher.hpp"

namespace deos {

void DeosMessageDispatcher::register_handler(uint8_t service, uint8_t command, std::shared_ptr<IDeosMessageHandler> handler) {
    if (handler) {
        handlers_[make_key(service, command)] = [handler](const deos_header_t& hdr, const uint8_t* pl, uint8_t len) {
            handler->handle(hdr, pl, len);
        };
    }
}

void DeosMessageDispatcher::register_handler(uint8_t service, uint8_t command, HandlerFunc handler) {
    if (handler) {
        handlers_[make_key(service, command)] = std::move(handler);
    }
}

bool DeosMessageDispatcher::dispatch(const deos_header_t& header, const uint8_t* payload, uint8_t payload_len) const {
    auto it = handlers_.find(make_key(header.service, header.command));
    if (it != handlers_.end()) {
        it->second(header, payload, payload_len);
        return true;
    }
    return false;
}

void DeosMessageDispatcher::clear() {
    handlers_.clear();
}

} // namespace deos
