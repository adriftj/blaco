#pragma once

#include <assert.h>
#include "sockaddr.h"
#include "blatomic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
* @brief Initialize coroutine I/O environment(thread pools, ...)
* @param options bit-ORs of:
*        BL_INIT_USE_MAIN_THREAD_AS_WORKER the calling thread will act as a worker
*        BL_INIT_DONT_WSASTARTUP call WSAStartup(only used in Windows)
* @param numIoWorkers 0-use default(normally 1*num of cpus), otherwise the number of io workers
* @param numOtherWorkers 0-use default, otherwise
* @note if options&BL_INIT_USE_MAIN_THREAD_AS_WORKER, you should call BlIoLoop() later in main thread.
*/
void BlInit(uint32_t options, size_t numIoWorkers, size_t numOtherWorkers);
static const uint32_t
	BL_INIT_USE_MAIN_THREAD_AS_WORKER = 0x01,
	BL_INIT_DONT_WSASTARTUP           = 0x02;

/*
* @brief Notify will exit
*/
void BlExitNotify();

/*
* @brief Wait for all workers to exit 
*/
void BlWaitExited();

extern size_t g_numCpus;

/*
* @brief if you call BlInit with options&BL_INIT_USE_MAIN_THREAD_AS_WORKER, you should call this function in main thread
* @warning DONT CALL this function out of main thread, normally this function is a dead loop
*/
void BlIoLoop();

typedef void (*BlTaskCallback)(void* parm);

/*
* @brief schedule a task to be done in thread pool
* @param[in] cb the task callback that will be executed in thread pool
* @param[in] parm the parameter to pass to the callback
* @param[in] isIoTask true-should be scheduled to run in io worker threads, false-other
*/
void BlPostTask(BlTaskCallback cb, void* parm, bool isIoTask);

/*
* @brief Add a run only once timer
* @param[in] startTime (unit:ms) >0-absolute time since Jan 1, 1970UTC, <=0-relate time to current
* @param[in] cb
* @param[in] parm
* @return true-ok, false-failed(not call inside the thread loop)
*/
bool BlAddOnceTimer(int64_t startTime, BlTaskCallback cb, void* parm);

/*
* @brief Add a timer(periodic or run only once)
* @param[in] in period 0-run only once, >0-the period
* @param[in] startTime (unit:ms) >0-absolute time since Jan 1, 1970UTC, <=0-relate time to current
* @param[in] cb
* @param[in] parm
* @param[in] isIoTask true-should be scheduled to run in io worker threads, false-other
* @return
*   @retval 0 failed(such as `cb` is NULL)
*   @retval other id of the created periodic timer
* @note run-once timer will be deleted automaticly after its callback return
*/
uint64_t BlAddTimer(uint64_t period, int64_t startTime, BlTaskCallback cb, void* parm, bool isIoTask);

/*
* @brief Delete a timer
* @param[in] id the timer id
* @param[in] cbDeleted 
*              NULL-no action,
*              otherwise-will call cbDeleted(parm) after running callback returns
* @param[in] parm valid only if cbDeleted!=NULL
* @return
*     @retval <0 timer faile(such as with `id` doesn't exist)
*     @retvak 0 deleted(won't call cbDeleted)
*     @retval 1 will be deleted in the future(will call `cbDeleted(parm)` then)
* @note normally cbDeleted will be called in different thread
*/
int BlDelTimer(uint64_t id, BlTaskCallback cbDeleted, void* parm);


/*
* @brief get error code after Socket I/O function returns fail
*/
int BlGetLastError();

/*
* @brief set error code
*/
void BlSetLastError(int err);


/*
* @brief create an event object
* @param[in] intial false-the event is reset initially, true-the event is set initially
* @return
*   @retval =0 failed, call BlGetLastError() for error code(ETIMEDOUT etc.)
*   @retval >0 handle of created event object
*/
BlEvent BlCreateEvent(bool initial);

/*
* @brief destroy event object
*/
void BlDestroyEvent(BlEvent ev);

/*
* @brief set the event
* @param[in] ev
* @return
*   @retval false failed, call BlGetLastError() for error code
*   @retval true ok
*/
bool BlSetEvent(BlEvent ev);

/*
* @brief wait event for a time
* @param[in] ev
* @param[in] t time(unit: ms), uint32_t(-1)-wait for infinite
* @return
*   @retval false failed, call BlGetLastError() for error code(ETIMEDOUT etc.)
*   @retval true ok
*/
bool BlWaitEventForTime(BlEvent ev, uint32_t t);


typedef struct tagBlFileContentWatcher BlFileContentWatcher;
typedef void (*BlFileContentWatcherCallback)(void* parm);

/*
* @brief Open a file content watcher
* @param[in] fileName name of the watched file
* @param[in] delay the callback will be called after `delay` milliseconds,
*                  for reducing the amount of events.
* @param[in] cb the event callback
* @param[in] parm will be passed to the event callback `cb`
* @return NULL if failed(call BlGetLastError() for error code), otherwise ok
*/
BlFileContentWatcher* BlFileContentWatcherOpen(const char* dir,
	int delayTime, BlFileContentWatcherCallback cb, void* parm);

/*
* @brief Close the filesystem event watcher
* @param[in] watcher to close
*/
void BlFileContentWatcherClose(BlFileContentWatcher* watcher);


/*
* @brief Free memory
*/
void BlFree(void* p);

typedef struct tagBlDnsClient BlDnsClient;

/*
* @brief Create a dns client
* @param[in] useDefaultServers true-set servers to default of system, false-dont set any server
* @return
*   @retval NULL failed
*   @retval other ok
*/
BlDnsClient* BlDnsClientNew(bool useDefaultServers);

/*
* @brief Release a dns client
*/
void BlDnsClientRelease(BlDnsClient* dns);

/*
* @brief set dns servers addresses and ports
* @param[in] dns
* @param[in] servers
*/
int BlDnsSetServers(BlDnsClient* dns, const char* servers);
int BlDnsAddServers(BlDnsClient* dns, const char* servers);
char* BlDnsGetServers(BlDnsClient* dns);
int BlDnsSetNic(BlDnsClient* dns, const struct sockaddr* nicAddr);
int BlDnsGetNic(BlDnsClient* dns, struct sockaddr* nicAddr, socklen_t nicAddrLen);
int BlDnsServiceName2Port(const char* name);
char* BlDnsServicePort2Name(int port);

typedef struct { // all BlDnsXxxRecord should have this struct as header
	uint16_t type;
	uint16_t reserved;
} BlDnsRecord;

typedef struct {
	uint16_t type;
	uint16_t reserved; // MUST be same as BlDnsRecord
	uint32_t ttl;
	struct in_addr addr;
} BlDnsARecord;

typedef struct {
	uint16_t type;
	uint16_t reserved;
	uint32_t ttl;
	struct in6_addr addr6;
} BlDnsAaaaRecord;

/*
* @brief Parsed DNS PTR record
* @note you SHOULD allocate adequate memory for this struct, including the memory `name` consumes.
*   For example, you can use:
*   @code
*     BlDnsPtrRecord* p = (BlDnsPtrRecord*)malloc(sizeof(BlDnsPtrRecord)+nameLen+1);
*     p->name = (char*)(p+1);
*   @endcode
*   In above code, `nameLen+1` is the total length of `name` and the trailing '\0'.
*/
typedef struct {
	uint16_t type;
	uint16_t reserved;
	uint32_t ttl;
	char* name;
} BlDnsPtrRecord;

typedef struct tagBlDnsAio BlDnsAio;
typedef void (*BlDnsOnCompleted)(BlDnsAio*);
struct tagBlDnsAio {
	BlDnsClient* dns;
	union {
		const struct sockaddr_in* sa4;
		const struct sockaddr_in6* sa6;
		const struct sockaddr* sa;
		const char* name;
	} u;
	const char* service;
	uint16_t type;
	uint32_t options;
	BlDnsOnCompleted onCompleted;

	int err;
	size_t nRecords;
	BlDnsRecord** records;
};

#define BLDNS_SKIP_CACHE          0x01
#define BLDNS_SKIP_HOSTS          0x02
#define BLDNS_SKIP_SERVER         0x04 ///< Don't send request to servers
#define BLDNS_IPV4_FIRST          0x08
#define BLDNS_IPV4_MAPPED         0x10
#define BLDNS_IPV4_ONLY           0x20
#define BLDNS_IPV6_ONLY           0x40
#define BLDNS_RETURN_FIRST_RESULT 0x80

#define BLDNS_T_A    1  ///< ipv4 address(BlDnsARecord)
#define BLDNS_T_PTR  12 ///< ipaddr2name(BlDnsPtrRecord)
#define BLDNS_T_AAAA 28 ///< ipv6 address(BlDnsAaaaRecord)
#define BLDNS_T_ANY  255

INLINE void BlDnsInitQuery(BlDnsAio* io, BlDnsClient* dns, const char* name,
	uint16_t type, uint32_t options, BlDnsOnCompleted onCompleted) {
	io->dns = dns;
	io->u.name = name;
	io->service = NULL;
	io->type = type;
	io->options = options;
	io->onCompleted = onCompleted;
}

INLINE void BlDnsInitName2Addr(BlDnsAio* io, BlDnsClient* dns, const char* name,
	const char* service, uint32_t options, BlDnsOnCompleted onCompleted) {
	io->dns = dns;
	io->u.name = name;
	io->service = service;
	io->type = (options & BLDNS_IPV4_ONLY)? BLDNS_T_A:
		(options & BLDNS_IPV6_ONLY)? BLDNS_T_AAAA: BLDNS_T_ANY;
	io->options = options;
	io->onCompleted = onCompleted;
}

INLINE void BlDnsInitAddr2Name(BlDnsAio* io, BlDnsClient* dns,
	const struct sockaddr* addr, uint16_t type, uint32_t options, BlDnsOnCompleted onCompleted) {
	io->dns = dns;
	io->u.sa = addr;
	io->service = NULL;
	io->type = BLDNS_T_PTR;
	io->options = options;
	io->onCompleted = onCompleted;
}

bool BlDnsDoQuery(BlDnsAio* io);


/*
* @brief Create a raw socket
* @param protocol
* @param ipv6 false for ipv4
* @return the created socket
*   @retval INVALID_SOCKET failed, call BlGetLastError() for error code
*   @retval other created ok
*/
SOCKET BlSockRaw(int protocol, bool ipv6);

/*
* @brief Create a TCP socket
* @param ipv6 false for ipv4
* @return the created socket
*   @retval INVALID_SOCKET failed, call BlGetLastError() for error code
*   @retval other created ok
* @note TODO: should setsockopt(SOL_SOCKET, SO_NOSIGPIPE) in linux
*/
SOCKET BlSockTcp(bool ipv6);

/*
* @brief Create a UDP socket
* @param ipv6 false for ipv4
* @return the created socket
*   @retval INVALID_SOCKET failed, call BlGetLastError() for error code
*   @retval other created ok
*/
SOCKET BlSockUdp(bool ipv6);

/*
* @brief Create a UDPLite socket
* @param ipv6 false for ipv4
* @return the created socket
*   @retval INVALID_SOCKET failed, call BlGetLastError() for error code
*   @retval other created ok
*/
SOCKET BlSockUdpLite(bool ipv6);

#ifdef SUPPORT_AF_UNIX

/*
* @brief Create an AF_UNIX unix domain socket
* @return the created socket
*   @retval INVALID_SOCKET failed, call BlGetLastError() for error code
*   @retval other created ok
*/
SOCKET BlSockUnix();

#endif

/*
* @brief Close socket
* @param[in] sock a socket
* @return
*   @retval <>0 failed, call BlGetLastError() for error code
*   @retval =0 ok
* @warning If you need to close a connected TCP socket, you should call co_await TcpClose rather than
*   this function, because it is a block I/O operation.
*/
int BlSockClose(SOCKET sock);

/*
* @brief Get local address part of a socket
* @param[in] sock
* @param[out] addr
* @param[out] pLen
* @return
*   @retval <>0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockGetLocalAddr(SOCKET sock, struct sockaddr* addr, socklen_t* pLen) {
	return getsockname(sock, addr, pLen);
}

/*
* @brief Get peer address part of a socket
* @param[in] sock
* @param[out] addr
* @param[out] pLen
* @return
*   @retval <>0 failed, call BlGetLastError() for error code
*   @retval =0 ok
* @note DON'T support unix domain socket
* @warning Some one report that sometimes this call will be blocked on Windows(normally >= 10ms)
*/
INLINE int BlSockGetPeerAddr(SOCKET sock, struct sockaddr* addr, socklen_t* pLen) {
	return getpeername(sock, addr, pLen);
}

/*
* @brief Set Socket to reuse addr(normally port)
* @param[in] sock the socket to set
* @param[in] onoff true-reuse, false-don't
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
* @note DON'T support unix domain socket
*/
INLINE int BlSockSetReuseAddr(SOCKET sock, bool onoff) {
    int opt = (onoff ? 1 : 0);
	return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (SOCKPARM)&opt, sizeof(opt));
}

/*
* @brief Set linger options of socket
* @param[in] sock the socket to set
* @param[in] onoff true-set linger, false-don't
* @param[in] timeoutMSELs linger timout in milliseconds
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockSetLinger(SOCKET sock, bool onoff, uint32_t timeoutMSELs) {
    struct linger l;
    l.l_onoff = (u_short)(onoff ? 1 : 0);
    l.l_linger = (u_short)(INFINITE == timeoutMSELs ? 0xffff : ((timeoutMSELs & 0x7FFFFFFF) / 1000));
    if (0 == l.l_linger && 0 != timeoutMSELs)
        l.l_linger = 1;
    return setsockopt(sock, SOL_SOCKET, SO_LINGER, (SOCKPARM)&l, sizeof(l));
}

/*
* @brief Set TCP socket with TCP_NODELAY option
* @param[in] sock the socket to set
* @param[in] onoff true-set, false-don't
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockSetTcpNoDelay(SOCKET sock, bool noDelay) {
    int val = (noDelay ? 1 : 0); return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (SOCKPARM)&val, sizeof(val));
}

/*
* @brief Set socket receive buffer size
* @param[in] sock the socket to set
* @param[in] len buffer size
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockSetRecvBufSize(SOCKET sock, socklen_t len) {
    return setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (SOCKPARM)&len, sizeof(len));
}

/*
* @brief Get socket receive buffer size
* @param[in] sock the socket to get
* @param[out] pLen ptr to buffer size
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockGetRecvBufSize(SOCKET sock, socklen_t* pLen) {
    socklen_t l = sizeof(*pLen); return getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (SOCKPARM)pLen, &l);
}

/*
* @brief Set socket send buffer size
* @param[in] sock the socket to set
* @param[in] len buffer size
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockSetSendBufSize(SOCKET sock, socklen_t len) {
    return setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (SOCKPARM)&len, sizeof(len));
}

/*
* @brief Get socket send buffer size
* @param[in] sock the socket to get
* @param[out] pLen ptr to buffer size
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockGetSendBufSize(SOCKET sock, socklen_t* pLen) {
    socklen_t l = sizeof(*pLen); return getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (SOCKPARM)pLen, &l);
}

/*
* @brief Set socket receive time out
* @param[in] sock the socket to set
* @param[in] timeoutMSELs timeout in millisecond
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
* @warning No effect in Windows, because timeout isn't be supported in IOCP mode.
*/
INLINE int BlSockSetRecvTimeout(SOCKET sock, uint32_t timeoutMSELs) {
    struct timeval timeo;
    if (INFINITE == timeoutMSELs) {
        timeo.tv_sec = 0x7FFFFFFF;
        timeo.tv_usec = 0;
    }
    else {
        timeo.tv_sec = (timeoutMSELs & 0x7FFFFFFF) / 1000;
        timeo.tv_usec = (timeoutMSELs & 0x7FFFFFFF) % 1000 * 1000;
    }
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (SOCKPARM)&timeo, sizeof(timeo));
}

/*
* @brief Set socket send time out
* @param[in] sock the socket to set
* @param[in] timeoutMSELs timeout in millisecond
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
* @warning No effect in Windows, because timeout isn't be supported in IOCP mode.
*/
INLINE int BlSockSetSendTimeout(SOCKET sock, uint32_t timeoutMSELs) {
    struct timeval timeo;
    if (INFINITE == timeoutMSELs) {
        timeo.tv_sec = 0x7FFFFFFF;
        timeo.tv_usec = 0;
    }
    else {
        timeo.tv_sec = (timeoutMSELs & 0x7FFFFFFF) / 1000;
        timeo.tv_usec = (timeoutMSELs & 0x7FFFFFFF) % 1000 * 1000;
    }
    return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (SOCKPARM)&timeo, sizeof(timeo));
}

/*
* @brief Set socket for broadcast
* @param[in] sock the socket to set
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockSetBroadcast(SOCKET sock) {
    long val = 1; return setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (SOCKPARM)&val, sizeof(val));
}

/*
* @brief Set TCP socket keepalive
* @param[in] sock the socket to set
* @param[in] onoff true-set, false-don't
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockSetKeepAlive(SOCKET sock, bool onoff) {
    return setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (SOCKPARM)&onoff, sizeof(onoff));
}

/*
* @brief Set TCP socket keepalive options
* @param[in] sock the socket to set
* @param[in] onoff true-set, false-don't
* @param[in] keepAliveTime
* @param[in] keepAliveInterval
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
int BlSockSetKeepAliveVals(SOCKET sock, bool onoff, uint32_t keepAliveTime, uint32_t keepAliveInterval);

/*
* @brief Bind the local address part of a socket
* @param sock
* @param addr internet address(v4 or v6)
* @param addrLen
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
* @note Support unix domain socket
*/
INLINE int BlSockBind(SOCKET sock, const struct sockaddr* addr) {
    return bind(sock, addr, BlGetSockAddrLen(addr));
}

/*
* @brief Set the length of listen queue of socket
* @param sock a TCP socket
* @param n the lenth of listen queue
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
* @note Support unix domain socket
*/
INLINE int BlTcpListen(SOCKET sock, int n) {
    return listen(sock, n);
}

/*
* @brief connect a UDP socket
* @param sock a UDP socket
* @param[in] addr internet address(v4 or v6) to connect
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
* @note DON'T support unix domain socket
*/
INLINE int BlUdpConnect(SOCKET sock, const struct sockaddr* addr) {
    return connect(sock, addr, BlGetSockAddrLen(addr));
}

/*
* @brief check if socket is readable or writable
* @param[in] sock
* @param[in] events 
* @param[in] timeout <0-wait indefinitely, =0-return immediately, >0-the time(in milliseconds) to wait
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval >=0 bit ORs of POLLxx(see linux system call poll)
*/
int BlSockPoll(SOCKET sock, short events, int timeout);

#ifdef __cplusplus
}
#endif

#ifdef _WIN32
#include "os/windows/blaio_impl1.h"
#else
#include "os/linux/blaio_impl1.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
* @brief Create a TCP server socket
* @param[in] addr internet address(v4 or v6) to bind
* @param[in] reuseAddr true-reuse, false-don't
* @param[in] nListen the lenth of listen queue
* @return the created server socket
*   @retval INVALID_SOCKET failed, call BlGetLastError() for error code
*   @retval other created ok
* @note DON'T support unix domain socket, @see BlUnixNewServer.
*/
INLINE SOCKET BlTcpNewServer(const struct sockaddr* addr, bool reuseAddr, int nListen) {
	SOCKET sock = BlSockTcp(addr->sa_family == AF_INET6);
	if (sock != INVALID_SOCKET) {
		int r = BlSockSetReuseAddr(sock, reuseAddr);
		if (r == 0) {
			r = BlSockBind(sock, addr);
			if (r == 0) {
				r = BlTcpListen(sock, nListen);
				if (r == 0)
					return sock;
			}
		}
		int err = BlGetLastError();
		BlSockClose(sock);
		BlSetLastError(err);
	}
	return INVALID_SOCKET;
}

/*
* @brief Create a UDP socket and bind to an local port
* @param[in] addr internet address(v4 or v6) to bind
* @return the created socket
*   @retval INVALID_SOCKET failed, call BlGetLastError() for error code
*   @retval other created ok
* @note DON'T support unix domain socket, @see BlUnixNewServer.
*/
INLINE SOCKET BlUdpNewPeerReceiver(const struct sockaddr* addr) {
	SOCKET sock = BlSockUdp(addr->sa_family == AF_INET6);
	if (sock == INVALID_SOCKET)
		return sock;
	int r = BlSockBind(sock, addr);
	if (r == 0)
		return sock;
	int err = BlGetLastError();
	BlSockClose(sock);
	BlSetLastError(err);
	return INVALID_SOCKET;
}

/*
* @brief Add a socket into a multicast group
* @param[in] sock
* @param[in] addr multicast group address(ipv4 or ipv6)
* @param[in] nicAddr NULL-use default NIC, else-use that NIC
* @note DON'T support unix domain socket
*/
INLINE int BlSockAddMemberShip(SOCKET sock, const struct sockaddr* addr,
	const struct sockaddr* nicAddr) {
	int level, option_name;
	void const* option_value;
	socklen_t option_len;
	struct ip_mreq imr4;
	struct ipv6_mreq imr6;

	if (!addr)
		return -1;
	if (addr->sa_family == AF_INET) {
		imr4.imr_multiaddr.s_addr = ((struct sockaddr_in*)addr)->sin_addr.s_addr;
		imr4.imr_interface.s_addr = nicAddr ? ((struct sockaddr_in*)nicAddr)->sin_addr.s_addr : INADDR_ANY;
		level = IPPROTO_IP;
		option_name = IP_ADD_MEMBERSHIP;
		option_value = &imr4;
		option_len = sizeof imr4;
	}
	else if (addr->sa_family == AF_INET6) {
		imr6.ipv6mr_multiaddr = ((struct sockaddr_in6*)addr)->sin6_addr;
		assert(nicAddr == NULL); // TODO: get nic index by ipv6 addr
		imr6.ipv6mr_interface = 0; // ???
		level = IPPROTO_IPV6;
		option_name = IPV6_JOIN_GROUP;
		option_value = &imr6;
		option_len = sizeof imr6;
	}
	else
		return -1;

	if (setsockopt(sock, level, option_name, (SOCKPARM)option_value, option_len) < 0) {
#if defined(_WIN32)
		if (BlGetLastError() == 0) // Windows sometimes lies about setsockopt() failing!
			return 0;
#endif
		return -1;
	}

#ifdef __linux__
	// This option is defined in modern versions of Linux to overcome a bug in the Linux kernel's default behavior.
	// When set to 0, it ensures that we receive only packets that were sent to the specified IP multicast address,
	// even if some other process on the same system has joined a different multicast group with the same port number.
	int multicastAll = 0;
	setsockopt(sock, level, IP_MULTICAST_ALL, // TODO: is this the same for IPv6?
		(SOCKPARM)&multicastAll, sizeof multicastAll);
	// Ignore the call's result.  Should it fail, we'll still receive packets (just perhaps more than intended)
#endif

	return 0;
}

/*
* @brief Set multicast ttl and loop of a socket
* @param[in] sock
* @param[in] ttl_or_hops -1-don't set, use default, else-the multicast hop limit for the socket
* @param[in] loop -1-don't set, use default, 1-the socket sees multicast packets that it has send itself, 0-no
* @note DON'T support unix domain socket
*/
INLINE int BlSockSetMulticastTtlLoop(SOCKET sock, sa_family_t family, int ttl_or_hops, int loop) {
	int level, option_name_ttl, option_name_loop;

	if (family == AF_INET) {
		level = IPPROTO_IP;
		option_name_ttl = IP_MULTICAST_TTL;
		option_name_loop = IP_MULTICAST_LOOP;
	}
	else if (family == AF_INET6) {
		level = IPPROTO_IPV6;
		option_name_ttl = IPV6_MULTICAST_HOPS;
		option_name_loop = IPV6_MULTICAST_LOOP;
	}
	else
		return -1;

	if (ttl_or_hops >= 0)
		setsockopt(sock, level, option_name_ttl, (SOCKPARM)&ttl_or_hops, sizeof(ttl_or_hops));
	if (loop >= 0)
		setsockopt(sock, level, option_name_loop, (SOCKPARM)&loop, sizeof(loop));
	return 0;
}

/*
* @brief quit a multicast group
* @param[in] sock
* @param[in] addr multicast group address(ipv4 or ipv6)
* @param[in] nicAddr NULL-use default NIC, else-use that NIC
* @note DON'T support unix domain socket
*/
INLINE int BlSockDropMemberShip(SOCKET sock, const struct sockaddr* addr,
	const struct sockaddr* nicAddr) {
	int level, option_name;
	void const* option_value;
	socklen_t option_len;
	struct ip_mreq imr4;
	struct ipv6_mreq imr6;

	if (!addr)
		return -1;
	if (addr->sa_family == AF_INET) {
		imr4.imr_multiaddr.s_addr = ((struct sockaddr_in*)addr)->sin_addr.s_addr;
		imr4.imr_interface.s_addr = nicAddr ? ((struct sockaddr_in*)nicAddr)->sin_addr.s_addr : INADDR_ANY;
		level = IPPROTO_IP;
		option_name = IP_DROP_MEMBERSHIP;
		option_value = &imr4;
		option_len = sizeof imr4;
	}
	else if (addr->sa_family == AF_INET6) {
		imr6.ipv6mr_multiaddr = ((struct sockaddr_in6*)addr)->sin6_addr;
		assert(nicAddr == NULL); // TODO: get nic index by ipv6 addr
		imr6.ipv6mr_interface = 0; // ???
		level = IPPROTO_IPV6;
		option_name = IPV6_LEAVE_GROUP;
		option_value = &imr6;
		option_len = sizeof imr6;
	}
	else
		return -1;
	return setsockopt(sock, level, option_name, (SOCKPARM)option_value, option_len);
}

/*
* @brief Create a UDP socket and connect to a remote address
* @param[in] addr internet address(v4 or v6) to connect
* @return the created socket
*   @retval INVALID_SOCKET failed, call BlGetLastError() for error code
*   @retval other created ok
* @note DON'T support unix domain socket
*/
INLINE SOCKET BlUdpNewPeerSender(const struct sockaddr* addr) {
	SOCKET sock = BlSockUdp(addr->sa_family == AF_INET6);
	if (sock == INVALID_SOCKET)
		return sock;
	int r = BlUdpConnect(sock, addr);
	if (r == 0)
		return sock;
	int err = BlGetLastError();
	BlSockClose(sock);
	BlSetLastError(err);
	return INVALID_SOCKET;
}

#ifdef SUPPORT_AF_UNIX

/*
* @brief Bind a unix domain socket to a path
* @param[in] sock
* @param[in] path
* @return
*   @retval 0 ok
*   @retval <>0 failed, call BlGetLastError() for error code
*/
INLINE int BlUnixBind(SOCKET sock, const char* path) {
	BlSockAddr addr;
	if (!BlGenUnixSockAddr(&addr, path)) {
		BlSetLastError(EINVAL);
		return -1;
	}
	return BlSockBind(sock, &addr.sa);
}

/*
* @brief Create a unix domain server
* @param[in] path the address to bind
* @param[in] unlinkPath true-unlink path, false-don't
* @param[in] nListen the lenth of listen queue
* @return
*   @retval INVALID_SOCKET failed, call BlGetLastError() for error code
*   @retval otherwise ok
*/
INLINE SOCKET BlUnixNewServer(const char* path, bool unlinkPath, int nListen) {
	if (unlinkPath)
		unlink(path);
	SOCKET sock = BlSockUnix();
	if (sock != INVALID_SOCKET) {
		int r = BlUnixBind(sock, path);
		if (r == 0) {
			r = BlTcpListen(sock, nListen);
			if (r == 0)
				return sock;
		}
		int err = BlGetLastError();
		BlSockClose(sock);
		BlSetLastError(err);
	}
	return INVALID_SOCKET;
}

#endif

// TODO: asynchronous DNS resolver

/*
* @brief Calc the total size of iovec
*/
INLINE size_t IoVecSum(const struct iovec* bufVec, size_t bufCount) {
	size_t total = 0;
	for (size_t i = 0; i < bufCount; ++i)
		total += bufVec[i].iov_len;
	return total;
}

/*
* @brief Adjust iovec after IO
* @param[in,out] bufVec
* @param[in,out] bufCnt
* @param[in] nXfer transfered size of IO
* @return the first iovec of remaining buffers
* @warning bufVec/bufCnt will be changed.
*/
INLINE void IoVecAdjustAfterIo(struct iovec** pbufVec, size_t* pbufCnt, size_t nXfer) {
	int n = nXfer;
	size_t i = 0;
	size_t bufCnt1 = *pbufCnt;
	struct iovec* bufVec1 = *pbufVec;
	for (; i < bufCnt1; ++i) {
		n -= bufVec1[i].iov_len;
		if (n < 0)
			break;
	}
	if (i < bufCnt1) { // bufVec[i] remain (-n) bytes not used
		bufVec1[i].iov_base = ((char*)bufVec1[i].iov_base) + bufVec1[i].iov_len + n;
		bufVec1[i].iov_len = -n;
	}
	else
		assert(n == 0);
	*pbufCnt = bufCnt1 - i;
	*pbufVec = bufVec1 + i;
}

/*
* @brief Accept a connection from client
*
* @note Support unix domain socket
*/
INLINE bool BlTcpAccept(BlTcpAccept_t* io, SOCKET sock, sa_family_t family,
	struct sockaddr* peer, socklen_t* peerLen, BlOnCompletedAio onCompleted) {
	BlInitTcpAccept(io, sock, family, peer, peerLen, onCompleted);
	return BlDoTcpAccept(io);
}

/*
* @brief Connect to server
* 
* @note Support unix domain socket
*/
INLINE bool BlTcpConnect(BlTcpConnect_t* io, SOCKET sock,
		const struct sockaddr* addr, BlOnCompletedAio onCompleted) {
	BlInitTcpConnect(io, sock, addr, onCompleted);
	return BlDoTcpConnect(io);
}

/*
* @brief Send data using a socket
*
* @note Support unix domain socket
*/
INLINE bool BlSockSend(BlSockSend_t* io, SOCKET sock, const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockSend(io, sock, buf, len, flags, onCompleted);
	return BlDoSockSend(io);
}

/*
* @brief Send data vector using a socket
*
* @note Support unix domain socket
*/
INLINE bool BlSockSendVec(BlSockSendVec_t* io, SOCKET sock,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockSendVec(io, sock, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockSendVec(io);
}

/*
* @brief Send data to an address using a socket
*
* @note DON'T support unix domain socket
*/
INLINE bool BlSockSendTo(BlSockSendTo_t* io, SOCKET sock, const struct sockaddr* addr,
	const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockSendTo(io, sock, addr, buf, len, flags, onCompleted);
	return BlDoSockSendTo(io);
}

/*
* @brief
* @note DON'T support unix domain socket
*/
INLINE bool BlSockSendVecTo(BlSockSendVecTo_t* io, SOCKET sock, const struct sockaddr* addr,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockSendVecTo(io, sock, addr, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockSendVecTo(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockMustSend(BlSockMustSend_t* io, SOCKET sock,
	const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockMustSend(io, sock, buf, len, flags, onCompleted);
	return BlDoSockMustSend(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockMustSendVec(BlSockMustSendVec_t* io, SOCKET sock,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockMustSendVec(io, sock, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockMustSendVec(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockRecv(BlSockRecv_t* io, SOCKET sock, void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockRecv(io, sock, buf, len, flags, onCompleted);
	return BlDoSockRecv(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockRecvVec(BlSockRecvVec_t* io, SOCKET sock,
	struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockRecvVec(io, sock, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockRecvVec(io);
}

/*
* @brief
* @note DON'T support unix domain socket
*/
INLINE bool BlSockRecvFrom(BlSockRecvFrom_t* io, SOCKET sock, struct sockaddr* addr,
	socklen_t* addrLen, void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockRecvFrom(io, sock, addr, addrLen, buf, len, flags, onCompleted);
	return BlDoSockRecvFrom(io);
}

/*
* @brief
* @note DON'T support unix domain socket
*/
INLINE bool BlSockRecvVecFrom(BlSockRecvVecFrom_t* io, SOCKET sock, struct sockaddr* addr,
	socklen_t* addrLen, struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockRecvVecFrom(io, sock, addr, addrLen, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockRecvVecFrom(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockMustRecv(BlSockMustRecv_t* io, SOCKET sock,
	void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockMustRecv(io, sock, buf, len, flags, onCompleted);
	return BlDoSockMustRecv(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockMustRecvVec(BlSockMustRecvVec_t* io, SOCKET sock,
	struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockMustRecvVec(io, sock, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockMustRecvVec(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlTcpClose(BlTcpClose_t* io, SOCKET sock, BlOnCompletedAio onCompleted) {
	BlInitTcpClose(io, sock, onCompleted);
	return BlDoTcpClose(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlTcpShutdown(BlTcpShutdown_t* io, SOCKET sock, int how, BlOnCompletedAio onCompleted) {
	BlInitTcpShutdown(io, sock, how, onCompleted);
	return BlDoTcpShutdown(io);
}


/*
* @brief Open file
* @param[in] fileName utf-8 encoded, use '/' or '\' as path splitor
* @param[in] flags standard unix flags, see unix manual page of open
* @param[in] mode standard unix mode, see unix manual page of open
* @return file handle
*   @retval -1 error, call BlGetLastError() for error code
*   @retval other ok 
*/
int BlFileOpen(const char* fileName, int flags, int mode);

/*
* @brief Close file
* @return
*   @retval -1 error, call BlGetLastError() for error code
*   @retval other ok
*/
int BlFileClose(int f);

INLINE bool BlFileRead(BlFileRead_t* io, int f, uint64_t offset,
	void* buf, uint32_t len, BlOnCompletedAio onCompleted) {
	BlInitFileRead(io, f, offset, buf, len, onCompleted);
	return BlDoFileRead(io);
}

INLINE bool BlFileWrite(BlFileWrite_t* io, int f, uint64_t offset,
	const void* buf, uint32_t len, BlOnCompletedAio onCompleted) {
	BlInitFileWrite(io, f, offset, buf, len, onCompleted);
	return BlDoFileWrite(io);
}


/*
* @brief Cancel all I/O of (HANDLE,SOCKET,...) `fd`
* @param[in] fd file handle or socket or ...
*/
int BlCancelIo(int fd);

#if 0
[[BLACO_AWAIT_HINT]]
int UdpBindEx(SOCKET sock, const SockAddr& addr, const SockAddr* nicAddr = NULL);

INLINE int UdpConnectEx(SOCKET sock, const SockAddr& addr, const SockAddr* nicAddr = NULL) {

}

INLINE SOCKET UdpNewPeerReceiverEx(const SockAddr& addr, const SockAddr* nicAddr = NULL) {
}

SOCKET UdpNewPeerSenderEx(const SockAddr& addr, const SockAddr* nicAddr = NULL);
#endif


typedef struct tagBlCutexCoro BlCutexCoro;
typedef void (*BlCutexOnLocked)(void* coro);
struct tagBlCutexCoro {
	void* coro;
	BlCutexCoro* next; ///< next coro waiting for same cutex
	BlCutexOnLocked onLocked; ///< callback after acquired the cutex
};

/// @brief Mutex for coroutines
typedef struct tagBlCutex BlCutex;
struct tagBlCutex {
	/// atomic locking state
	///  NULL - locked without coroutines are waiting
	///     1 - not locked
	/// other - locked, points to the first coroutine which is waiting(all waiting coroutine
	///                     are linked, forming a single linked list).
	void* volatile state;
	BlCutexCoro* _firstWaiter; ///< only used in unlock function
};

#define _BLCUTEX_NOT_LOCKED (void* const)1

INLINE void BlCutexInit(BlCutex* cutex) {
	cutex->state = _BLCUTEX_NOT_LOCKED;
	cutex->_firstWaiter = NULL;
}

INLINE bool BlCutexTryLock(BlCutex* cutex) {
	void* p = BlCasPtrV(&cutex->state, _BLCUTEX_NOT_LOCKED, NULL);
	return p == _BLCUTEX_NOT_LOCKED;
}

INLINE bool BlCutexLock(BlCutex* cutex, BlCutexCoro* currentCoro) {
	assert(currentCoro != NULL);
	void* current = BlLoadPtr((void* volatile*)cutex->state);
	void* newVal;
	void* currentNew;
	for(;; current = currentNew) {
		if (current == _BLCUTEX_NOT_LOCKED) // if not locked, attempt to lock it
			newVal = NULL;
		else { // otherwise attempt to add it to the waiting single linked list
			currentCoro->next = (BlCutexCoro*)current;
			newVal = currentCoro;
		}
		currentNew = BlCasPtrV(&cutex->state, current, newVal);
		if (currentNew == current)
			break;
	}
	return current == _BLCUTEX_NOT_LOCKED; // return ok if previous state is not locked
}

INLINE void BlCutexUnlock(BlCutex* cutex) {
	void* firstWaiter = cutex->_firstWaiter;
	if (firstWaiter == NULL) {
		void* current = BlLoadPtr((void* volatile*)cutex->state);
		if (current == NULL) { // no waiters
			void* currentNew = BlCasPtrV(&cutex->state, current, _BLCUTEX_NOT_LOCKED);
			if (currentNew == current) // no waiters
				return;
			// CAS failed, so some one add themself as a waiter
		}
		firstWaiter = BlFasPtr(&cutex->state, NULL);
		// Should internal waiters be reversed to allow for true FIFO, or should they be resumed
		// in this reverse order to maximum throuhgput?  If this list ever gets 'long' the reversal
		// will take some time, but it might guarantee better latency across waiters.  This LIFO
		// middle ground on the atomic waiters means the best throughput at the cost of the first
		// waiter possibly having added latency based on the queue length of waiters.  Either way
		// incurs a cost but this way for short lists will most likely be faster even though it
		// isn't completely fair.
	}
	assert(firstWaiter != NULL);
	BlCutexCoro* coro = (BlCutexCoro*)firstWaiter; // let first waiter own cutex
	cutex->_firstWaiter = coro->next;
	coro->onLocked(coro->coro);
}

#ifdef __cplusplus
}
#endif
