#include "blconfig.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include "utility/defer.hpp"
#include "sockaddr.hpp"

namespace bl {

SockAddr::SockAddr(std::string_view ip, uint16_t port) noexcept {
    if(0 == BlParseSockAddr2(ip.data(), ip.size(), port, &addr.sa, sizeof(addr)))
        addr.sa.sa_family = AF_UNSPEC;
}

SockAddr::SockAddr(std::string_view str) noexcept {
    if (0 == BlParseSockAddr(str.data(), str.size(), &addr.sa, sizeof(addr)))
        addr.sa.sa_family = AF_UNSPEC;
}

SockAddr::SockAddr(uint16_t port, bool is_ipv6) noexcept {
    static_assert(offsetof(SockAddr, addr.sa6) == 0, "sa6 offset 0");
    static_assert(offsetof(SockAddr, addr.sa4) == 0, "sa4 offset 0");
    BlGenSockAddrPort(port, is_ipv6, &addr.sa);
}

SockAddr::SockAddr(const struct sockaddr& saddr) noexcept {
    static_assert(offsetof(SockAddr, addr.sa6) == 0, "sa6 offset 0");
    static_assert(offsetof(SockAddr, addr.sa4) == 0, "sa4 offset 0");
#ifdef SUPPORT_AF_UNIX
    static_assert(offsetof(SockAddr, addr.su) == 0, "su offset 0");
#endif
    BlCopySockAddr(&addr.sa, sizeof(addr), &saddr);
}

std::string SockAddr::to_ip() const {
    char buf[BL_MAX_SOCKADDR_STR];
    if (0 != BlSockAddr2Str(&addr.sa, buf, sizeof(buf), true))
        return "";
    return buf;
}

std::string SockAddr::to_str() const {
    char buf[BL_MAX_SOCKADDR_STR];
    if(0!=BlSockAddr2Str(&addr.sa, buf, sizeof(buf), false))
        return "";
    return buf;
}

bool SockAddr::operator==(const SockAddr &rhs) const noexcept {
    if (family() != rhs.family())
        return false;
    if (family() == AF_INET) {
        return addr.sa4.sin_port == rhs.addr.sa4.sin_port
               && addr.sa4.sin_addr.s_addr == rhs.addr.sa4.sin_addr.s_addr;
    }
    if (family() == AF_INET6) {
        return addr.sa6.sin6_port == rhs.addr.sa6.sin6_port
            && memcmp(
                &addr.sa6.sin6_addr, &rhs.addr.sa6.sin6_addr,
                sizeof(addr.sa6.sin6_addr)
            ) == 0;
    }
#ifdef SUPPORT_AF_UNIX
    if (family() == AF_UNIX)
        return 0 == strcmp(addr.su.sun_path, rhs.addr.su.sun_path);
#endif
    return true;
}

} // namespace bl
