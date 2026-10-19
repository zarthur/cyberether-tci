#include "cyberether_tci/client.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return 1;

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    endpoint.sin_port = 0;
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) != 0 ||
        ::listen(listener, 1) != 0) {
        (void)::close(listener);
        return 2;
    }
    socklen_t endpointLength = sizeof(endpoint);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&endpoint), &endpointLength) != 0) {
        (void)::close(listener);
        return 3;
    }

    std::promise<void> requestReceived;
    auto received = requestReceived.get_future();
    std::thread server([&] {
        const int connection = ::accept(listener, nullptr, nullptr);
        if (connection < 0) return;
        std::string request;
        std::array<char, 1> byte{};
        while (!request.ends_with("\r\n\r\n")) {
            const auto count = ::recv(connection, byte.data(), byte.size(), 0);
            if (count <= 0) {
                (void)::close(connection);
                return;
            }
            request.push_back(byte[0]);
        }
        requestReceived.set_value();
        (void)::recv(connection, byte.data(), byte.size(), 0); // wait for client shutdown during handshake
        (void)::close(connection);
    });

    cyberether_tci::TciClient client;
    cyberether_tci::ClientConfig config;
    config.port = ntohs(endpoint.sin_port);
    if (!client.start(config)) {
        (void)::close(listener);
        server.join();
        return 4;
    }
    if (received.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        client.stop();
        (void)::close(listener);
        server.join();
        return 5;
    }

    const auto start = std::chrono::steady_clock::now();
    client.stop();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    server.join();
    (void)::close(listener);
    if (elapsed > std::chrono::seconds(1)) return 6;
    std::cout << "client stop interrupted a stalled loopback handshake\n";
    return 0;
}
