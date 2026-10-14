#pragma once

#include "deos_ros_bridge/deos_protocol.hpp"

#include <unordered_map>
#include <functional>
#include <memory>

namespace deos {

/**
 * @brief Abstract DEOS Message Handler Interface (Command / Strategy Pattern)
 */
class IDeosMessageHandler {
public:
    virtual ~IDeosMessageHandler() = default;
    virtual void handle(const deos_header_t& header, const uint8_t* payload, uint8_t payload_len) = 0;
};

/**
 * @brief Dispatcher for incoming DEOS protocol messages (OCP - Open/Closed Principle)
 * Replaces monolithic if-else ladders with dynamic handler registration.
 */
class DeosMessageDispatcher {
public:
    using HandlerFunc = std::function<void(const deos_header_t&, const uint8_t*, uint8_t)>;

    DeosMessageDispatcher() = default;

    /**
     * @brief Register a polymorphic message handler
     */
    void register_handler(uint8_t service, uint8_t command, std::shared_ptr<IDeosMessageHandler> handler);

    /**
     * @brief Register a functional/lambda message handler
     */
    void register_handler(uint8_t service, uint8_t command, HandlerFunc handler);

    /**
     * @brief Dispatch an incoming frame to its registered handler
     * @return true if a registered handler handled the message, false otherwise
     */
    bool dispatch(const deos_header_t& header, const uint8_t* payload, uint8_t payload_len) const;

    /**
     * @brief Clear all registered handlers
     */
    void clear();

private:
    static uint16_t make_key(uint8_t service, uint8_t command) {
        return (static_cast<uint16_t>(service) << 8) | command;
    }

    std::unordered_map<uint16_t, HandlerFunc> handlers_;
};

} // namespace deos
