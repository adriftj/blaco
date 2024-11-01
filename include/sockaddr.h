#pragma once

#include <stdio.h>
#include <stdint.h>
#include "blconfig.h"

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

typedef union {
	struct sockaddr sa;
	struct sockaddr_in sa4;
	struct sockaddr_in6 sa6;
#ifdef SUPPORT_AF_UNIX
	struct sockaddr_un su;
#endif
	struct sockaddr_storage ss;
} BlSockAddr;

typedef union {
    struct sockaddr sa;
    struct sockaddr_in sa4;
    struct sockaddr_in6 sa6;
} BlSockAddr46;

INLINE socklen_t BlGetSockAddrLen(const struct sockaddr* addr) {
	switch (addr->sa_family) {
	case AF_INET: return sizeof(struct sockaddr_in);
	case AF_INET6: return sizeof(struct sockaddr_in6);
#ifdef SUPPORT_AF_UNIX
	case AF_UNIX: return sizeof(struct sockaddr_un);
#endif
	}
	return 0;
}

INLINE bool BlClearSockAddr(struct sockaddr* addr) {
	sa_family_t family = addr->sa_family;
	memset(addr, 0, BlGetSockAddrLen(addr));
	addr->sa_family = family;
	return family == AF_INET || family == AF_INET6
#ifdef SUPPORT_AF_UNIX
		|| family == AF_UNIX
#endif
		;
}

INLINE void BlCopySockAddr(struct sockaddr* dst, socklen_t dstLen, const struct sockaddr* src) {
	socklen_t srcLen = BlGetSockAddrLen(src);
	if(dstLen >= srcLen)
		memcpy(dst, src, srcLen);
}

#ifdef SUPPORT_AF_UNIX
INLINE bool BlGenUnixSockAddr(BlSockAddr* addr, const char* path) {
    memset(addr, 0, sizeof(struct sockaddr_un));
    addr->sa.sa_family = AF_UNIX;
    size_t n = strlen(path);
	if (n > sizeof(addr->su.sun_path) - 1)
		return false;
	strcpy(addr->su.sun_path, path);
	return true;
}
#endif

INLINE int BlParsePort(const char* p, int n) {
    if (n < 0)
        n = p? strlen(p): 0;
    int port = 0;
    for (int i = 0; i < n; ++i) {
        char c = p[i];
        if (c <= '0' || c >= '9')
            return -1;
        port = port * 10 + c - '0';
    }
    if (port >= 65536)
        return -1;
    return port;
}

INLINE void BlGenSockAddrPort(uint16_t port, bool is_ipv6, struct sockaddr* addr) {
    if (is_ipv6) {
        struct sockaddr_in6* sa6 = (struct sockaddr_in6*)addr;
        memset(addr, 0, sizeof(struct sockaddr_in6));
        sa6->sin6_family = AF_INET6;
        sa6->sin6_addr = in6addr_any;
        sa6->sin6_port = htons(port);
    }
    else {
        struct sockaddr_in* sa4 = (struct sockaddr_in*)addr;
        memset(addr, 0, sizeof(struct sockaddr_in));
        sa4->sin_family = AF_INET;
        sa4->sin_addr.s_addr = INADDR_ANY;
        sa4->sin_port = htons(port);
    }
}

INLINE bool BlParseSockAddr4(const char* str, int len, uint16_t port, struct sockaddr_in* sa4) {
    int res = 1;
    memset(sa4, 0, sizeof(*sa4));
    if (str == NULL)
        sa4->sin_addr.s_addr = INADDR_ANY;
    else if (len < 0)
        res = inet_pton(AF_INET, str, &sa4->sin_addr);
    else {
        if (len > 15) // max length ipv4 address is 15: xxx.xxx.xxx.xxx
            return false;
        char buf[16];
        strncpy(buf, str, len);
        buf[len] = 0;
        res = inet_pton(AF_INET, buf, &sa4->sin_addr);
    }
    if (res != 1)
        return false;

    sa4->sin_family = AF_INET;
    sa4->sin_port = htons(port);
    return true;
}

INLINE bool BlParseSockAddr4s(const char* str, int len,
    const char* sPort, int portLen, struct sockaddr_in* sa4) {
    int port = sPort ? BlParsePort(sPort, portLen) : 0;
    if (port < 0)
        return false;
    return BlParseSockAddr4(str, len, port, sa4);
}

INLINE bool BlParseSockAddr6(const char* str, int len, uint16_t port, struct sockaddr_in6* sa6) {
    int res = 1;
    memset(sa6, 0, sizeof(*sa6));
    if (str == NULL)
        sa6->sin6_addr = in6addr_any;
    else if (len < 0)
        res = inet_pton(AF_INET6, str, &sa6->sin6_addr);
    else {
        char buf[46]; // max length ipv6 address is 45: XXXX:(repeat 6 times)...:XXXX:xxx.xxx.xxx.xxx
        strncpy(buf, str, len);
        buf[len] = 0;
        res = inet_pton(AF_INET6, buf, &sa6->sin6_addr);
    }
    if (res != 1)
        return false;

    sa6->sin6_family = AF_INET6;
    sa6->sin6_port = htons(port);
    return true;
}

INLINE bool BlParseSockAddr6s(const char* str, int len,
    const char* sPort, int portLen, struct sockaddr_in6* sa6) {
    int port = sPort? BlParsePort(sPort, portLen): 0;
    if (port < 0)
        return false;
    return BlParseSockAddr6(str, len, port, sa6);
}

INLINE socklen_t BlParseSockAddr(const char* str, int nlen,
    struct sockaddr* addr, socklen_t addrLen) {
    int len = nlen;
    if (len < 0)
        len = str? strlen(str): 0;

#ifdef SUPPORT_AF_UNIX
    if(
#if defined(_WIN32)
        (len >= 3 && str[1] == ':' && str[2] == '/')
#else
        (len >= 1 && str[0] == '/')
#endif
        || (len >= 2 && str[0] == '.' && str[1] == '/')
        || (len >= 3 && str[0] == '.' && str[1] == '.' && str[2] == '/')
    )
    {
        // unix domain socket
        if (addrLen < sizeof(struct sockaddr_un))
            return 0;
        struct sockaddr_un* su = (struct sockaddr_un*)addr;
        if (len < (int)sizeof(su->sun_path) - 1) { // `str` is not too large
            memset(su, 0, sizeof(*su));
            su->sun_family = AF_UNIX;
            memcpy(su->sun_path, str, len);
        }
        return sizeof(*su);
    }
#endif

    int pos;
    for (pos = len - 1; pos >= 0; --pos)
        if (str[pos] == ':')
            break;
    if (pos > 0 && str[0] == '[' && str[pos - 1] == ']') { // format `[ipv6addr]:port`
        if (addrLen < sizeof(struct sockaddr_in6))
            return 0;
        struct sockaddr_in6* sa6 = (struct sockaddr_in6*)addr;
        return BlParseSockAddr6s(str + 1, pos - 2, str + pos + 1, len - pos - 1, sa6) ? sizeof(*sa6) : 0;
    }

    if (addrLen < sizeof(struct sockaddr_in))
        return 0;
    struct sockaddr_in* sa4 = (struct sockaddr_in*)addr;
    if (pos < 0) // No ':' found, it is just a ipv4 address
        return BlParseSockAddr4s(str, nlen, NULL, 0, sa4)? sizeof(*sa4): 0;
    return BlParseSockAddr4s(str, pos, str+pos+1, len-pos-1, sa4)? sizeof(*sa4): 0;
}

INLINE socklen_t BlParseSockAddr2(const char* sIp, int lenIp, uint16_t port,
    struct sockaddr* addr, socklen_t addrLen) {
    int len = lenIp;
    if (len < 0)
        len = sIp? strlen(sIp): 0;
    int i = 0;
    for (; i < len; ++i)
        if (sIp[i] == ':')
            break;
    if (i >= len) { // No ':' found, it is a ipv4 address
        if (addrLen < sizeof(struct sockaddr_in))
            return 0;
        struct sockaddr_in* sa4 = (struct sockaddr_in*)addr;
        return BlParseSockAddr4(sIp, lenIp, port, sa4)? sizeof(*sa4): 0;
    }

    if (addrLen < sizeof(struct sockaddr_in6))
        return 0;
    struct sockaddr_in6* sa6 = (struct sockaddr_in6*)addr;
    return BlParseSockAddr6(sIp, lenIp, port, sa6) ? sizeof(*sa6) : 0;
}

INLINE socklen_t BlParseSockAddr2s(const char* sIp, int len,
    const char* sPort, int portLen, struct sockaddr* addr, socklen_t addrLen) {
    int port = BlParsePort(sPort, portLen);
    if (port < 0)
        return 0;
    return BlParseSockAddr2(sIp, len, port, addr, addrLen);
}

INLINE int BlSockAddr2Str(const struct sockaddr* addr, char* buf, size_t bufLen, bool onlyIp) {
    if (bufLen < 1)
        return -1;
    sa_family_t family = addr->sa_family;
    if (family == AF_INET) {
        const char* sIp = inet_ntop(AF_INET, &((const struct sockaddr_in*)addr)->sin_addr, buf, bufLen);
        if (!sIp)
            return -1;
        if (!onlyIp) {
            char tmp[8];
            int portLen = sprintf(tmp, ":%u", ntohs(((const struct sockaddr_in*)addr)->sin_port));
            size_t ipLen = strlen(sIp);
            if (bufLen <= ipLen + portLen)
                return -1;
            strcpy(buf + ipLen, tmp);
        }
    }
    else if (family == AF_INET6) {
        const char* sIp;
        if (onlyIp) {
            sIp = inet_ntop(AF_INET6, &((const struct sockaddr_in6*)addr)->sin6_addr, buf, bufLen);
            if (!sIp)
                return -1;
        }
        else {
            sIp = inet_ntop(AF_INET6, &((const struct sockaddr_in6*)addr)->sin6_addr, buf+1, bufLen-1);
            if (!sIp)
                return -1;
            char tmp[8];
            int portLen = sprintf(tmp, "]:%u", ntohs(((const struct sockaddr_in*)addr)->sin_port));
            size_t ipLen = strlen(sIp)+1;
            if (bufLen <= ipLen + portLen)
                return -1;
            strcpy(buf + ipLen, tmp);
            buf[0] = '[';
        }
    }
#ifdef SUPPORT_AF_UNIX
    else if (family == AF_UNIX) {
        const char* path = ((const struct sockaddr_un*)addr)->sun_path;
        size_t pathLen = strlen(path);
        if (bufLen <= pathLen)
            return -1;
        strcpy(buf, path);
    }
#endif
    else // no ip address
        return -1;
    return 0;
}

#ifdef __cplusplus
}
#endif
