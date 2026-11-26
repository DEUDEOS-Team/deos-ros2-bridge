#include "deos_ros_bridge/udp_transport.hpp"
#include <cassert>
#include <unistd.h>

int main() {
    // Reserve a free port, then release it for the transport's fixed-port bind.
    int probe = socket(AF_INET, SOCK_DGRAM, 0);
    assert(probe >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(probe, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    socklen_t length = sizeof(addr);
    assert(getsockname(probe, reinterpret_cast<sockaddr*>(&addr), &length) == 0);
    const auto port = ntohs(addr.sin_port);
    ::close(probe);

    UdpTransport transport;
    assert(transport.init("127.0.0.1", port, "lo"));
    unsigned received = 0;
    transport.set_rx_callback([&](const uint8_t* data, size_t size) {
        assert(size == 1 && data[0] == 42);
        ++received;
    });
    uint8_t payload = 42;
    assert(transport.send(&payload, 1));
    transport.poll(1000);
    assert(received == 1);

    // An unrelated sender must not be accepted as the configured STM32 peer.
    int rogue = socket(AF_INET, SOCK_DGRAM, 0);
    assert(rogue >= 0);
    assert(sendto(rogue, &payload, 1, 0,
                  reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 1);
    transport.poll(1000);
    assert(received == 1);
    ::close(rogue);
    transport.close();
    assert(!transport.init("127.0.0.1", port, "deos-missing"));
    assert(!transport.is_open());
}
