#include "tci/socket_io.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <iostream>

#include <sys/socket.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition << '\n'; \
        std::exit(EXIT_FAILURE); \
    } \
} while (false)

int main() {
    std::array<int, 2> sockets{-1, -1};
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()) == 0);
    CHECK(cyberether_tci::detail::configureSocketForSend(sockets[0]));
    CHECK(::close(sockets[1]) == 0);

    const std::array<char, 1> payload{'x'};
    errno = 0;
    const auto result = cyberether_tci::detail::sendWithoutSigPipe(
        sockets[0], payload.data(), payload.size());
    const auto sendError = errno;
    (void)::close(sockets[0]);

    CHECK(result == -1);
    CHECK(sendError == EPIPE || sendError == ECONNRESET);
    std::cout << "disconnect-during-send SIGPIPE regression passed\n";
    return 0;
}
