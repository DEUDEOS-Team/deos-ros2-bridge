#include "deos_ros_bridge/udp_transport.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <poll.h>
#include <iostream>

UdpTransport::UdpTransport() : socket_fd_(-1) {
    std::memset(&remote_addr_, 0, sizeof(remote_addr_));
}

UdpTransport::~UdpTransport() {
    close();
}

bool UdpTransport::init(const std::string& bind_ip, uint16_t bind_port,
                        const std::string& remote_ip, uint16_t remote_port) 
{
    close();

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::cerr << "[UdpTransport] Failed to create socket: " << std::strerror(errno) << std::endl;
        return false;
    }

    // Allow quick reuse of the address/port
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind local address
    struct sockaddr_in bind_addr;
    std::memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(bind_port);
    if (bind_ip.empty() || bind_ip == "0.0.0.0") {
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        inet_pton(AF_INET, bind_ip.c_str(), &bind_addr.sin_addr);
    }

    if (bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        std::cerr << "[UdpTransport] Failed to bind to " << bind_ip << ":" << bind_port 
                  << " - " << std::strerror(errno) << std::endl;
        ::close(fd);
        return false;
    }

    // Set remote address
    std::memset(&remote_addr_, 0, sizeof(remote_addr_));
    remote_addr_.sin_family = AF_INET;
    remote_addr_.sin_port = htons(remote_port);
    if (inet_pton(AF_INET, remote_ip.c_str(), &remote_addr_.sin_addr) <= 0) {
        std::cerr << "[UdpTransport] Invalid remote IP: " << remote_ip << std::endl;
        ::close(fd);
        return false;
    }

    socket_fd_ = fd;
    std::cout << "[UdpTransport] Bound to " << bind_ip << ":" << bind_port 
              << ", target STM32 at " << remote_ip << ":" << remote_port << std::endl;
    return true;
}

bool UdpTransport::send(const void* data, size_t length) {
    int fd = socket_fd_.load();
    if (fd < 0 || data == nullptr || length == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    ssize_t sent = sendto(fd, data, length, 0,
                          (struct sockaddr*)&remote_addr_, sizeof(remote_addr_));
    return sent == static_cast<ssize_t>(length);
}

void UdpTransport::close() {
    int fd = socket_fd_.exchange(-1);
    if (fd >= 0) {
        ::close(fd);
    }
}

void UdpTransport::poll(int timeout_ms) {
    int fd = socket_fd_.load();
    if (fd < 0) return;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        uint8_t buffer[2048];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t received = recvfrom(fd, buffer, sizeof(buffer), 0,
                                    (struct sockaddr*)&from_addr, &from_len);
        if (received > 0 && rx_callback_) {
            rx_callback_(buffer, static_cast<size_t>(received));
        }
    }
}
