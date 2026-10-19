#pragma once

#include <sys/socket.h>
#include <sys/types.h>

#include <cstddef>

namespace cyberether_tci::detail {

// macOS suppresses SIGPIPE per socket; Linux exposes MSG_NOSIGNAL per send.
inline bool configureSocketForSend(int fd) noexcept {
#if defined(SO_NOSIGPIPE)
    int enabled = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) return false;
#else
    (void)fd;
#endif
    return true;
}

inline ssize_t sendWithoutSigPipe(int fd, const void* data, std::size_t size) noexcept {
#if defined(MSG_NOSIGNAL)
    return ::send(fd, data, size, MSG_NOSIGNAL);
#else
    return ::send(fd, data, size, 0);
#endif
}

}  // namespace cyberether_tci::detail
