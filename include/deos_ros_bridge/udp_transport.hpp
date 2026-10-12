#pragma once

#include <string>
#include <vector>
#include <functional>
#include <netinet/in.h>

class UdpTransport {
public:
    UdpTransport();
    ~UdpTransport();

    bool init(const std::string& bind_ip, uint16_t bind_port, const std::string& remote_ip, uint16_t remote_port);
    bool send(const void* data, size_t length);
    void close();

    // Rx callback
    using RxCallback = std::function<void(const uint8_t* data, size_t length)>;
    void set_rx_callback(RxCallback cb) { rx_callback_ = cb; }

    // Call this in a loop or thread to receive data
    void poll();

private:
    int socket_fd_;
    struct sockaddr_in remote_addr_;
    RxCallback rx_callback_;
};
