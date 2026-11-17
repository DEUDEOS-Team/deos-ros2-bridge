#include "deos_ros_bridge/message_dispatcher.hpp"
#include "universal_frame.hpp"

namespace deos {

struct DeosMessageDispatcher::Registry {
    explicit Registry(uint8_t node) : callbacks(node) {}
    DatagramRegistry callbacks;
};

DeosMessageDispatcher::DeosMessageDispatcher(uint8_t local_node)
    : registry_(std::make_unique<Registry>(local_node))
{
}

DeosMessageDispatcher::~DeosMessageDispatcher() = default;

void DeosMessageDispatcher::register_handler(uint8_t message_class, uint8_t service,
                                             uint8_t command, Handler handler)
{
    registry_->callbacks.add(message_class, service, command,
        [handler = std::move(handler)](const DatagramHeader& header, const uint8_t* payload, uint8_t length) {
            deos_header_t legacy{};
            legacy.destination = header.destination;
            legacy.source = header.source;
            legacy.priority = header.priority;
            legacy.version = header.version;
            legacy.message_class = header.message_class;
            legacy.service = header.service;
            legacy.command = header.command;
            legacy.sequence = header.sequence;
            handler(legacy, payload, length);
        });
}

bool DeosMessageDispatcher::dispatch(const deos_header_t& header, const uint8_t* payload,
                                     uint8_t payload_len) const
{
    return registry_->callbacks.dispatch(
        {header.destination, header.source, header.priority, header.version, header.message_class,
         header.service, header.command, header.sequence}, payload, payload_len);
}

}
