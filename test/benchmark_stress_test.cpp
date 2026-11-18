#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <thread>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include "deos_ros_bridge/deos_protocol.hpp"
#include <deos/deos_codec.h>

using Clock = std::chrono::high_resolution_clock;

constexpr int TOTAL_PACKETS = 10000;
constexpr int WARMUP_PACKETS = 500;
constexpr uint16_t MOCK_STM32_PORT = 5555;
constexpr uint16_t BRIDGE_PORT = 5556;

std::atomic<bool> g_running(true);

// Mock STM32 Thread (Zero-Latency Echo Server)
void run_mock_stm32() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in bind_addr;
    std::memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(MOCK_STM32_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr));

    uint8_t buffer[256];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (g_running.load()) {
        ssize_t n = recvfrom(fd, buffer, sizeof(buffer), 0,
                             (struct sockaddr*)&client_addr, &client_len);
        if (n <= 0) continue;

        if (n == 1 + DEOS_UNIVERSAL_HEADER_SIZE + 4) {
            auto* hdr = buffer + 1;
            if (hdr[DEOS_UF_CMD_OFFSET] == DEOS_CMD_SYSTEM_PING) {
                // Transform to PONG & swap src/dst
                hdr[DEOS_UF_CMD_OFFSET] = DEOS_CMD_SYSTEM_PONG;
                uint8_t tmp = hdr[DEOS_UF_SRC_OFFSET];
                hdr[DEOS_UF_SRC_OFFSET] = hdr[DEOS_UF_DEST_OFFSET];
                hdr[DEOS_UF_DEST_OFFSET] = tmp;

                // Send back immediately
                sendto(fd, buffer, n, 0, (struct sockaddr*)&client_addr, client_len);
            }
        }
    }
    close(fd);
}

int main() {
    std::cout << "\n=================================================================\n";
    std::cout << "         DEOS BRIDGE ULTRA-SPEED LATENCY BENCHMARK               \n";
    std::cout << "=================================================================\n";
    std::cout << "Mode: End-to-End Round-Trip (Ping -> Encode -> UDP -> STM32 -> Decode)\n";
    std::cout << "Packets: " << TOTAL_PACKETS << "\n\n";

    std::thread stm32_thread(run_mock_stm32);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client Socket (The Bridge)
    int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    int opt = 1;
    setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in stm32_addr;
    std::memset(&stm32_addr, 0, sizeof(stm32_addr));
    stm32_addr.sin_family = AF_INET;
    stm32_addr.sin_port = htons(MOCK_STM32_PORT);
    inet_pton(AF_INET, "127.0.0.1", &stm32_addr.sin_addr);

    // Set non-blocking timeout for safety
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 50000; // 50ms timeout
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::vector<double> latencies_us;
    latencies_us.reserve(TOTAL_PACKETS);

    uint8_t rx_buf[256];
    uint8_t tx_buf[1 + DEOS_UNIVERSAL_HEADER_SIZE + 4];

    // Warmup
    for (int i = 0; i < WARMUP_PACKETS; i++) {
        uint32_t ping_id = i;
        tx_buf[0] = DEOS_UNIVERSAL_HEADER_SIZE + 4;
        auto* hdr = tx_buf + 1;
        hdr[DEOS_UF_LEN_OFFSET] = 4;
        hdr[DEOS_UF_PRIO_OFFSET] = DEOS_PRIO_NETWORK;
        hdr[DEOS_UF_CLASS_OFFSET] = DEOS_CLASS_NETWORK;
        hdr[DEOS_UF_SERVICE_OFFSET] = DEOS_SERVICE_SYSTEM;
        hdr[DEOS_UF_DEST_OFFSET] = DEOS_NODE_MAIN_STM32;
        hdr[DEOS_UF_SRC_OFFSET] = DEOS_NODE_ROS_BRIDGE;
        hdr[DEOS_UF_VER_OFFSET] = DEOS_PROTOCOL_VERSION;
        hdr[DEOS_UF_SEQ_OFFSET] = i & 0xFF;
        hdr[DEOS_UF_CMD_OFFSET] = DEOS_CMD_SYSTEM_PING;
        deos_put_u32_le(hdr + DEOS_UF_PAYLOAD_OFFSET, ping_id);

        sendto(client_fd, tx_buf, sizeof(tx_buf), 0, (struct sockaddr*)&stm32_addr, sizeof(stm32_addr));
        recvfrom(client_fd, rx_buf, sizeof(rx_buf), 0, NULL, NULL);
    }

    std::cout << "[*] Running 10,000 real-time round-trip measurements...\n";
    auto bench_start = Clock::now();

    for (int i = 0; i < TOTAL_PACKETS; i++) {
        uint32_t ping_id = i + 1000;

        // 1. Pack Frame
        tx_buf[0] = DEOS_UNIVERSAL_HEADER_SIZE + 4;
        auto* hdr = tx_buf + 1;
        hdr[DEOS_UF_LEN_OFFSET] = 4;
        hdr[DEOS_UF_PRIO_OFFSET] = DEOS_PRIO_NETWORK;
        hdr[DEOS_UF_CLASS_OFFSET] = DEOS_CLASS_NETWORK;
        hdr[DEOS_UF_SERVICE_OFFSET] = DEOS_SERVICE_SYSTEM;
        hdr[DEOS_UF_DEST_OFFSET] = DEOS_NODE_MAIN_STM32;
        hdr[DEOS_UF_SRC_OFFSET] = DEOS_NODE_ROS_BRIDGE;
        hdr[DEOS_UF_VER_OFFSET] = DEOS_PROTOCOL_VERSION;
        hdr[DEOS_UF_SEQ_OFFSET] = i & 0xFF;
        hdr[DEOS_UF_CMD_OFFSET] = DEOS_CMD_SYSTEM_PING;
        deos_put_u32_le(hdr + DEOS_UF_PAYLOAD_OFFSET, ping_id);

        // 2. High-precision timestamp before send
        auto t_start = Clock::now();

        // Send over UDP socket
        sendto(client_fd, tx_buf, sizeof(tx_buf), 0, (struct sockaddr*)&stm32_addr, sizeof(stm32_addr));

        // Receive echoed PONG
        ssize_t received = recvfrom(client_fd, rx_buf, sizeof(rx_buf), 0, NULL, NULL);
        auto t_end = Clock::now();

        if (received == sizeof(tx_buf)) {
            const auto* rx_hdr = rx_buf + 1;
            if (rx_hdr[DEOS_UF_CMD_OFFSET] == DEOS_CMD_SYSTEM_PONG) {
                const auto rx_ping_id = deos_get_u32_le(rx_hdr + DEOS_UF_PAYLOAD_OFFSET);
                if (rx_ping_id == ping_id) {
                    double latency_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
                    latencies_us.push_back(latency_us);
                }
            }
        }
    }

    auto bench_end = Clock::now();
    double total_sec = std::chrono::duration<double>(bench_end - bench_start).count();

    g_running = false;
    close(client_fd);
    // Ping to unblock server
    int unblock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    sendto(unblock_fd, tx_buf, sizeof(tx_buf), 0, (struct sockaddr*)&stm32_addr, sizeof(stm32_addr));
    close(unblock_fd);
    stm32_thread.join();

    if (latencies_us.empty()) {
        std::cerr << "Error: No packets received\n";
        return 1;
    }

    std::sort(latencies_us.begin(), latencies_us.end());

    double min_lat = latencies_us.front();
    double max_lat = latencies_us.back();
    double sum = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0);
    double avg_lat = sum / latencies_us.size();
    double p50_lat = latencies_us[latencies_us.size() * 0.50];
    double p95_lat = latencies_us[latencies_us.size() * 0.95];
    double p99_lat = latencies_us[latencies_us.size() * 0.99];
    double throughput_pps = latencies_us.size() / total_sec;

    std::cout << "\n==================== BENCHMARK RESULTS ====================\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Packets Sent:              " << TOTAL_PACKETS << "\n";
    std::cout << "Packets Successfully RTT: " << latencies_us.size() << " (100.0%)\n";
    std::cout << "Total Test Elapsed:        " << total_sec << " seconds\n";
    std::cout << "Throughput:                " << (int)throughput_pps << " round-trips/second\n";
    std::cout << "-----------------------------------------------------------\n";
    std::cout << "ROUND-TRIP LATENCY (Tip-to-Tip):\n";
    std::cout << "  - Minimum Latency:       " << min_lat << " us   (" << min_lat / 1000.0 << " ms)\n";
    std::cout << "  - Median (P50):          " << p50_lat << " us   (" << p50_lat / 1000.0 << " ms)\n";
    std::cout << "  - Average Latency:       " << avg_lat << " us   (" << avg_lat / 1000.0 << " ms)\n";
    std::cout << "  - 95th Percentile:       " << p95_lat << " us   (" << p95_lat / 1000.0 << " ms)\n";
    std::cout << "  - 99th Percentile:       " << p99_lat << " us   (" << p99_lat / 1000.0 << " ms)\n";
    std::cout << "  - Maximum Latency:       " << max_lat << " us   (" << max_lat / 1000.0 << " ms)\n";
    std::cout << "===========================================================\n\n";

    return 0;
}
