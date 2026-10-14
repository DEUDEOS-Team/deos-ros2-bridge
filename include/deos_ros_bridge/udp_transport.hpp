#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <netinet/in.h>

class UdpTransport {
public:
    UdpTransport();
    ~UdpTransport();

    // Prevent copying
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    /**
     * @brief Initialize UDP socket targeting the STM32 (no local bind needed)
     */
    bool init(const std::string& remote_ip, uint16_t remote_port);

    /**
     * @brief Send raw buffer over UDP to the remote address
     */
    bool send(const void* data, size_t length);

    /**
     * @brief Check for incoming UDP datagrams (blocks up to timeout_ms)
     */
    void poll(int timeout_ms = 50);

    /**
     * @brief Close socket and release resources
     */
    void close();

    /**
     * @brief Check if socket is open and ready
     */
    bool is_open() const { return socket_fd_ >= 0; }

    // Rx callback type: (const uint8_t* buffer, size_t length)
    using RxCallback = std::function<void(const uint8_t* data, size_t length)>;
    void set_rx_callback(RxCallback cb) { rx_callback_ = cb; }

private:
    std::atomic<int> socket_fd_;
    struct sockaddr_in remote_addr_;
    std::mutex send_mutex_;
    RxCallback rx_callback_;
};
