#include "deos_ros_bridge/udp_transport.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <poll.h>

UdpTransport::UdpTransport() : socket_fd_(-1) {}

UdpTransport::~UdpTransport() {
    close();
}

bool UdpTransport::init(const std::string& bind_ip, uint16_t bind_port, const std::string& remote_ip, uint16_t remote_port) {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) return false;

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(bind_port);
    inet_pton(AF_INET, bind_ip.c_str(), &bind_addr.sin_addr);

    if (bind(socket_fd_, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        close();
        return false;
    }

    memset(&remote_addr_, 0, sizeof(remote_addr_));
    remote_addr_.sin_family = AF_INET;
    remote_addr_.sin_port = htons(remote_port);
    inet_pton(AF_INET, remote_ip.c_str(), &remote_addr_.sin_addr);

    return true;
}

bool UdpTransport::send(const void* data, size_t length) {
    if (socket_fd_ < 0) return false;
    ssize_t sent = sendto(socket_fd_, data, length, 0, (struct sockaddr*)&remote_addr_, sizeof(remote_addr_));
    return sent == (ssize_t)length;
}

void UdpTransport::close() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

void UdpTransport::poll() {
    if (socket_fd_ < 0) return;

    struct pollfd pfd;
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, 100); // 100ms timeout
    if (ret > 0 && (pfd.revents & POLLIN)) {
        uint8_t buffer[2048];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t received = recvfrom(socket_fd_, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len);
        if (received > 0 && rx_callback_) {
            rx_callback_(buffer, received);
        }
    }
}
