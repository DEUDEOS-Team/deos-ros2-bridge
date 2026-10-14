#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <sys/uio.h>

namespace deos {

/**
 * @brief Abstract Transport Interface (DIP - Dependency Inversion Principle)
 * Allows substituting UDP with Mock, Serial, CAN, or Shared Memory transport.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    // Rx callback type: (const uint8_t* buffer, size_t length)
    using RxCallback = std::function<void(const uint8_t* data, size_t length)>;

    /**
     * @brief Send raw buffer over transport
     */
    virtual bool send(const void* data, size_t length) = 0;

    /**
     * @brief Send scatter-gather buffers (Zero-Copy)
     */
    virtual bool send_iovec(const struct iovec* iov, size_t iovcnt) = 0;

    /**
     * @brief Check/poll for incoming datagrams (blocks up to timeout_ms)
     */
    virtual void poll(int timeout_ms = 50) = 0;

    /**
     * @brief Register incoming data callback
     */
    virtual void set_rx_callback(RxCallback cb) = 0;

    /**
     * @brief Close transport and release underlying resources (RAII)
     */
    virtual void close() = 0;

    /**
     * @brief Check if transport is open and ready
     */
    virtual bool is_open() const = 0;
};

} // namespace deos
