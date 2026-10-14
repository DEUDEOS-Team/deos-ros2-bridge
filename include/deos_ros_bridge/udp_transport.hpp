#pragma once

#include "deos_ros_bridge/transport_interface.hpp"

#include <string>
#include <mutex>
#include <atomic>
#include <netinet/in.h>

class UdpTransport : public deos::ITransport {
public:
    /**
     * @brief Default constructor (requires subsequent init() call)
     */
    UdpTransport();

    /**
     * @brief RAII Constructor: initializes socket targeting remote endpoint immediately
     */
    UdpTransport(const std::string& remote_ip, uint16_t remote_port);

    ~UdpTransport() override;

    // Prevent copying (strict resource ownership)
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    // Move semantics (Rule of 5)
    UdpTransport(UdpTransport&& other) noexcept;
    UdpTransport& operator=(UdpTransport&& other) noexcept;

    /**
     * @brief Initialize or reconfigure UDP socket targeting the STM32
     */
    bool init(const std::string& remote_ip, uint16_t remote_port);

    /**
     * @brief Send raw buffer over UDP (implements ITransport)
     */
    bool send(const void* data, size_t length) override;

    /**
     * @brief Send scatter-gather buffers over UDP to remote address (Zero-Copy)
     */
    bool send_iovec(const struct iovec* iov, size_t iovcnt) override;

    /**
     * @brief Check for incoming UDP datagrams (blocks up to timeout_ms)
     */
    void poll(int timeout_ms = 50) override;

    /**
     * @brief Close socket and release resources
     */
    void close() override;

    /**
     * @brief Check if socket is open and ready
     */
    bool is_open() const override { return socket_fd_.load() >= 0; }

    /**
     * @brief Set incoming data callback (implements ITransport)
     */
    void set_rx_callback(RxCallback cb) override { rx_callback_ = std::move(cb); }

    /**
     * @brief Get remote IP
     */
    const std::string& remote_ip() const { return remote_ip_; }

    /**
     * @brief Get remote Port
     */
    uint16_t remote_port() const { return remote_port_; }

private:
    std::string remote_ip_;
    uint16_t remote_port_{0};
    std::atomic<int> socket_fd_{-1};
    struct sockaddr_in remote_addr_{};
    std::mutex send_mutex_;
    RxCallback rx_callback_;
};
