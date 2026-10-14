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

bool UdpTransport::init(const std::string& remote_ip, uint16_t remote_port) 
{
    close();

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::cerr << "[UdpTransport] Failed to create socket: " << std::strerror(errno) << std::endl;
        return false;
    }

    // Set remote address (STM32)
    std::memset(&remote_addr_, 0, sizeof(remote_addr_));
    remote_addr_.sin_family = AF_INET;
    remote_addr_.sin_port = htons(remote_port);
    if (inet_pton(AF_INET, remote_ip.c_str(), &remote_addr_.sin_addr) <= 0) {
        std::cerr << "[UdpTransport] Invalid remote IP: " << remote_ip << std::endl;
        ::close(fd);
        return false;
    }

    socket_fd_ = fd;
    std::cout << "[UdpTransport] Socket ready, remote target: " << remote_ip << ":" << remote_port << std::endl;
    return true;
}

bool UdpTransport::send_iovec(const struct iovec* iov, size_t iovcnt) {
    int fd = socket_fd_.load();
    if (fd < 0 || iov == nullptr || iovcnt == 0) {
        return false;
    }

    struct msghdr msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.msg_name = &remote_addr_;
    msg.msg_namelen = sizeof(remote_addr_);
    msg.msg_iov = const_cast<struct iovec*>(iov);
    msg.msg_iovlen = iovcnt;

    size_t total_expected = 0;
    for (size_t i = 0; i < iovcnt; ++i) {
        total_expected += iov[i].iov_len;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    ssize_t sent = sendmsg(fd, &msg, 0);
    return sent == static_cast<ssize_t>(total_expected);
}

bool UdpTransport::send(const void* data, size_t length) {
    if (data == nullptr || length == 0) {
        return false;
    }
    struct iovec iov;
    iov.iov_base = const_cast<void*>(data);
    iov.iov_len = length;
    return send_iovec(&iov, 1);
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
