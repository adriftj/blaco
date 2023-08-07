#pragma once

#include <string_view>
#include "blconfig.hpp"
#include "sockaddr.h"

namespace bl {

class SockAddr {
  public:
    /**
     * @brief Invalid address
     */
    SockAddr() noexcept { addr.sa.sa_family = AF_UNSPEC; }

    /**
     * @brief for connecting
     */
    SockAddr(std::string_view ip, uint16_t port) noexcept;

    /**
     * @brief for listening
     */
    explicit SockAddr(uint16_t port, bool is_ipv6 = false) noexcept;

    /*
    * @brief for unix domain socket or ipv4:port or [ipv6]:port
    */
    explicit SockAddr(std::string_view str) noexcept;

    /**
     * @brief for Sockets API
     */
    explicit SockAddr(const struct sockaddr& saddr) noexcept;

    bool is_invalid() const noexcept {
        return family() == AF_UNSPEC;
    }

    sa_family_t family() const noexcept {
        return addr.sa.sa_family;
    }

    bool is_ipv4() const noexcept {
        return family() == AF_INET;
    }

    bool is_ipv6() const noexcept {
        return family() == AF_INET6;
    }

#ifdef SUPPORT_AF_UNIX
    bool is_unix() const noexcept {
        return family() == AF_UNIX;
    }
#endif

    uint16_t port() const noexcept {
        return ntohs(addr.sa4.sin_port);
    }

    SockAddr& reset_port(uint16_t port) noexcept {
        addr.sa4.sin_port = htons(port);
        return *this;
    }

    bool is_multicast() const noexcept {
        if (is_ipv4()) {
            //in_addr_t stored in network order
            unsigned char c = *(unsigned char*)&(addr.sa4.sin_addr);
            return (c & 0xF0) == 0xE0;
        }
        if (is_ipv6())
            return addr.sa6.sin6_addr.s6_addr[0] == 0xFF;
        return false;
    }

    std::string to_ip() const;
    std::string to_str() const;

    const struct sockaddr *get_sockaddr(socklen_t* len=nullptr) const noexcept {
        if (len)
            *len = length();
        return &addr.sa;
    }

    struct sockaddr* get_sockaddr(socklen_t* len = nullptr) noexcept {
        if (len)
            *len = length();
        return &addr.sa;
    }

    operator const sockaddr* () {
        return &addr.sa;
    }

    operator sockaddr* () {
        return &addr.sa;
    }

    socklen_t length() const noexcept {
        return BlGetSockAddrLen(&addr.sa);
    }

    bool operator==(const SockAddr &rhs) const noexcept;

    BlSockAddr addr;
};

} // namespace bl
