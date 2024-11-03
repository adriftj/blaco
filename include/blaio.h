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
* @param numIoWorkers 0-use default(normally 1*num of cpus), otherwise the number of io workers
* @param numOtherWorkers 0-use default, otherwise
* @note if options&BL_INIT_USE_MAIN_THREAD_AS_WORKER, you should call BlIoLoop() later in main thread.
*/
void BlInit(uint32_t options, size_t numIoWorkers, size_t numOtherWorkers);
static const uint32_t
	BL_INIT_USE_MAIN_THREAD_AS_WORKER = 0x01;

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
*     @retval <0 failed(such as with `id` doesn't exist)
*     @retvak 0 deleted(won't call cbDeleted)
*     @retval 1 will be deleted in the future(will call `cbDeleted(parm)` then)
* @note normally cbDeleted will be called in different thread
*/
int BlDelTimer(uint64_t id, BlTaskCallback cbDeleted, void* parm);

/*
* @brief Change a timer
* @param[in] in period 0-run only once, >0-the period
* @param[in] startTime (unit:ms) >0-absolute time since Jan 1, 1970UTC, <=0-relate time to current
* @param[in] cbChanged
*              NULL-no action,
*              otherwise-will call cbChanged(parm) after running callback returns
* @param[in] parm valid only if cbChanged!=NULL
* @return
*     @retval <0 failed(such as with `id` doesn't exist)
*     @retvak 0 changed(won't call cbChanged)
*     @retval 1 will be changed in the future(will call `cbChanged(parm)` then)
*/
int BlChangeTimer(uint64_t id, uint64_t period, int64_t startTime, BlTaskCallback cbChanged, void* parm);

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

/*
* @brief Callback function when file content changed
* @param[in] path full path (absoluted) of content changed file
* @param[in] relativeStart substring(`path`, `relativeStart`) is the path related to the watched directory.
* @param[in] parm
*/
typedef void (*BlFileContentWatcherCallback)(const BlPathChar* path, int relativeStart, void* parm);

/*
* @brief Open a file content watcher
* @param[in] dir the watched directory(utf8)
* @param[in] fileNames name array of the watched files(utf8).
*            All these file names are relatived to `dir`.
* @param[in] nFileNames count of name array `fileNames`
* @param[in] delay the callback will be called after `delay` milliseconds,
*                  for reducing the amount of events.
* @param[in] cb the event callback(call in the thread pool)
* @param[in] parm will be passed to the event callback `cb`
* @return NULL if failed(call BlGetLastError() for error code), otherwise ok
*/
BlFileContentWatcher* BlFileContentWatcherOpen(const char* dir, const char** fileNames,
	int nFileNames, int delay, BlFileContentWatcherCallback cb, void* parm);

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
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval other created ok
*/
int BlSockRaw(int protocol, bool ipv6);

/*
* @brief Create a TCP socket
* @param ipv6 false for ipv4
* @return the created socket
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval other created ok
* @note TODO: should setsockopt(SOL_SOCKET, SO_NOSIGPIPE) in linux
*/
int BlSockTcp(bool ipv6);

/*
* @brief Create a UDP socket
* @param ipv6 false for ipv4
* @return the created socket
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval other created ok
*/
int BlSockUdp(bool ipv6);

/*
* @brief Create a UDPLite socket
* @param ipv6 false for ipv4
* @return the created socket
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval other created ok
*/
int BlSockUdpLite(bool ipv6);

#ifdef SUPPORT_AF_UNIX

/*
* @brief Create an AF_UNIX unix domain socket
* @return the created socket
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval other created ok
*/
int BlSockUnix();

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
int BlSockClose(int sock);

/*
* @brief Get local address part of a socket
* @param[in] sock
* @param[out] addr
* @param[out] pLen
* @return
*   @retval <>0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockGetLocalAddr(int sock, struct sockaddr* addr, socklen_t* pLen) {
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
INLINE int BlSockGetPeerAddr(int sock, struct sockaddr* addr, socklen_t* pLen) {
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
INLINE int BlSockSetReuseAddr(int sock, bool onoff) {
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
INLINE int BlSockSetLinger(int sock, bool onoff, uint32_t timeoutMSELs) {
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
INLINE int BlSockSetTcpNoDelay(int sock, bool noDelay) {
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
INLINE int BlSockSetRecvBufSize(int sock, socklen_t len) {
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
INLINE int BlSockGetRecvBufSize(int sock, socklen_t* pLen) {
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
INLINE int BlSockSetSendBufSize(int sock, socklen_t len) {
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
INLINE int BlSockGetSendBufSize(int sock, socklen_t* pLen) {
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
INLINE int BlSockSetRecvTimeout(int sock, uint32_t timeoutMSELs) {
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
INLINE int BlSockSetSendTimeout(int sock, uint32_t timeoutMSELs) {
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
INLINE int BlSockSetBroadcast(int sock) {
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
INLINE int BlSockSetKeepAlive(int sock, bool onoff) {
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
int BlSockSetKeepAliveVals(int sock, bool onoff, uint32_t keepAliveTime, uint32_t keepAliveInterval);

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
INLINE int BlSockBind(int sock, const struct sockaddr* addr) {
    return bind(sock, addr, BlGetSockAddrLen(addr));
}

/*
* @brief Set the length of listen queue of socket
* @param sock a socket, generally a TCP socket
* @param n the lenth of listen queue
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
* @note Support unix domain socket
*/
INLINE int BlSockListen(int sock, int n) {
    return listen(sock, n);
}

/*
* @brief connect a socket
* @param sock a socket
* @param[in] addr internet address(v4 or v6) to connect
* @return
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval =0 ok
*/
INLINE int BlSockConnect(int sock, const struct sockaddr* addr) {
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
int BlSockPoll(int sock, short events, int timeout);

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
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval other created ok
* @note DON'T support unix domain socket, @see BlUnixNewServer.
*/
INLINE int BlTcpNewServer(const struct sockaddr* addr, bool reuseAddr, int nListen) {
	int sock = BlSockTcp(addr->sa_family == AF_INET6);
	if (sock >= 0) {
		int r = BlSockSetReuseAddr(sock, reuseAddr);
		if (r == 0) {
			r = BlSockBind(sock, addr);
			if (r == 0) {
				r = BlSockListen(sock, nListen);
				if (r == 0)
					return sock;
			}
		}
		int err = BlGetLastError();
		BlSockClose(sock);
		BlSetLastError(err);
	}
	return -1;
}

/*
* @brief Create a UDP socket and bind to an local port
* @param[in] addr internet address(v4 or v6) to bind
* @return the created socket
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval other created ok
* @note DON'T support unix domain socket, @see BlUnixNewServer.
*/
INLINE int BlUdpNewPeerReceiver(const struct sockaddr* addr, bool reuseAddr) {
	int sock = BlSockUdp(addr->sa_family == AF_INET6);
	if (sock < 0)
		return sock;
	int r = BlSockSetReuseAddr(sock, reuseAddr);
	if (r == 0) {
		r = BlSockBind(sock, addr);
		if (r == 0)
			return sock;
	}
	int err = BlGetLastError();
	BlSockClose(sock);
	BlSetLastError(err);
	return -1;
}

/*
* @brief Create a UDP socket and connect to a remote address
* @param[in] addr internet address(v4 or v6) to connect
* @return the created socket
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval other created ok
* @note DON'T support unix domain socket
*/
INLINE int BlUdpNewPeerSender(const struct sockaddr* addr) {
	int sock = BlSockUdp(addr->sa_family == AF_INET6);
	if (sock < 0)
		return sock;
	int r = BlSockConnect(sock, addr);
	if (r == 0)
		return sock;
	int err = BlGetLastError();
	BlSockClose(sock);
	BlSetLastError(err);
	return -1;
}

INLINE int _BlSockAddDropMemberShip(int sock, const struct sockaddr* addr,
	const struct sockaddr* nicAddr, int addOrDrop) {
	int level, option_name;
	void const* option_value;
	socklen_t option_len;
	struct ip_mreq imr4;
	struct ipv6_mreq imr6;

	if (!addr) {
		BlSetLastError(EINVAL);
		return -1;
	}

	if (addr->sa_family == AF_INET) {
		imr4.imr_multiaddr.s_addr = ((struct sockaddr_in*)addr)->sin_addr.s_addr;
		imr4.imr_interface.s_addr = nicAddr ? ((struct sockaddr_in*)nicAddr)->sin_addr.s_addr : htonl(INADDR_ANY);
		level = IPPROTO_IP;
		option_name = addOrDrop? IP_ADD_MEMBERSHIP: IP_DROP_MEMBERSHIP;
		option_value = &imr4;
		option_len = sizeof imr4;
	}
	else if (addr->sa_family == AF_INET6) {
		imr6.ipv6mr_multiaddr = ((struct sockaddr_in6*)addr)->sin6_addr;
		imr6.ipv6mr_interface = nicAddr ? ((struct sockaddr_in6*)nicAddr)->sin6_scope_id : 0;
		level = IPPROTO_IPV6;
		option_name = addOrDrop? IPV6_ADD_MEMBERSHIP: IPV6_DROP_MEMBERSHIP;
		option_value = &imr6;
		option_len = sizeof imr6;
	}
	else {
		BlSetLastError(EINVAL);
		return -1;
	}

	if (setsockopt(sock, level, option_name, (SOCKPARM)option_value, option_len) < 0) {
#if defined(_WIN32)
		if (BlGetLastError() == 0) // Windows sometimes lies about setsockopt() failing!
			return 0;
#endif
		return -1;
	}

#ifdef __linux__
	if (addOrDrop) {
		// This option is defined in modern versions of Linux to overcome a bug in the Linux kernel's default behavior.
		// When set to 0, it ensures that we receive only packets that were sent to the specified IP multicast address,
		// even if some other process on the same system has joined a different multicast group with the same port number.
		int multicastAll = 0;
		setsockopt(sock, level, IP_MULTICAST_ALL, // TODO: is this the same for IPv6?
			(SOCKPARM)&multicastAll, sizeof multicastAll);
		// Ignore the call's result. Should it fail, we'll still receive packets (just perhaps more than intended)
	}
#endif

	return 0;
}

/*
* @brief Add a socket into a multicast group
* @param[in] sock
* @param[in] addr multicast group address(ipv4 or ipv6)
* @param[in] nicAddr NULL-use default NIC, else-use that NIC
* @note DON'T support unix domain socket
*/
INLINE int BlSockAddMemberShip(int sock, const struct sockaddr* addr, const struct sockaddr* nicAddr) {
	return _BlSockAddDropMemberShip(sock, addr, nicAddr, 1);
}

/*
* @brief quit a multicast group
* @param[in] sock
* @param[in] addr multicast group address(ipv4 or ipv6)
* @param[in] nicAddr NULL-use default NIC, else-use that NIC
* @note DON'T support unix domain socket
*/
INLINE int BlSockDropMemberShip(int sock, const struct sockaddr* addr, const struct sockaddr* nicAddr) {
	return _BlSockAddDropMemberShip(sock, addr, nicAddr, 0);
}

INLINE int BlSockSetMulticastIf(int sock, const struct sockaddr* addr) {
	if (!addr) {
		BlSetLastError(EINVAL);
		return -1;
	}
	if (addr->sa_family == AF_INET)
		return setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (SOCKPARM)addr, sizeof(addr));
	return setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF, (SOCKPARM)addr, sizeof(addr));
}

/*
* @brief Set multicast ttl of a socket
* @param[in] sock
* @param[in] family
* @param[in] ttl_or_hops the multicast hop limit for the socket
* @note DON'T support unix domain socket
*/
INLINE int BlSockSetMulticastTtl(int sock, bool ipv6, int ttl_or_hops) {
	int level = ipv6 ? IPPROTO_IPV6 : IPPROTO_IP;
	int option_name = ipv6? IPV6_MULTICAST_HOPS : IP_MULTICAST_TTL;
	return setsockopt(sock, level, option_name, (SOCKPARM)&ttl_or_hops, sizeof(ttl_or_hops));
}

/*
* @brief Set multicast loop of a socket
* @param[in] sock
* @param[in] family
* @param[in] loop 1-the socket sees multicast packets that it has send itself, 0-no
* @note DON'T support unix domain socket
*/
INLINE int BlSockSetMulticastLoop(int sock, bool ipv6, int loop) {
	int level = ipv6 ? IPPROTO_IPV6 : IPPROTO_IP;
	int option_name = ipv6 ? IPV6_MULTICAST_LOOP : IP_MULTICAST_LOOP;
	return setsockopt(sock, level, option_name, (SOCKPARM)&loop, sizeof(loop));
}

INLINE int BlSockBindEx(int sock, const struct sockaddr* addr, const struct sockaddr* nicAddr) {
	BlSockAddr46 localAddr;
	sa_family_t family = addr->sa_family;
	bool ipv6 = (family == AF_INET6);
	if ((nicAddr && nicAddr->sa_family!=family) || (!ipv6 && family!=AF_INET)) {
		BlSetLastError(EINVAL);
		return -1;
	}
	socklen_t addrLen = BlGetSockAddrLen(addr);
	BlCopySockAddr(&localAddr.sa, addrLen, addr);
	if (ipv6)
		localAddr.sa6.sin6_addr = nicAddr? ((struct sockaddr_in6*)nicAddr)->sin6_addr: in6addr_any;
	else
		localAddr.sa4.sin_addr.s_addr = nicAddr? ((struct sockaddr_in*)nicAddr)->sin_addr.s_addr: htonl(INADDR_ANY);
	int r = bind(sock, &localAddr.sa, addrLen);
	if (r < 0)
		return r;
	return BlSockAddMemberShip(sock, addr, nicAddr);
}

INLINE int BlSockConnectEx(int sock, const struct sockaddr* addr, const struct sockaddr* nicAddr, int ttl_or_hops, int loop) {
	int r = connect(sock, addr, BlGetSockAddrLen(addr));
	if (r < 0)
		return r;
	if (nicAddr) {
		r = BlSockSetMulticastIf(sock, nicAddr);
		if (r < 0)
			return r;
	}
	bool ipv6 = (addr->sa_family == AF_INET6);
	r = BlSockSetMulticastTtl(sock, ipv6, ttl_or_hops);
	if (r < 0)
		return r;
	return BlSockSetMulticastLoop(sock, ipv6, loop);
}

INLINE int BlUdpNewPeerReceiverEx(const struct sockaddr* addr, const struct sockaddr* nicAddr, bool reuseAddr) {
	int sock = BlSockUdp(addr->sa_family == AF_INET6);
	if (sock < 0)
		return sock;
	int r = BlSockSetReuseAddr(sock, reuseAddr);
	if (r == 0) {
		r = BlSockBindEx(sock, addr, nicAddr);
		if (r == 0)
			return sock;
	}
	int err = BlGetLastError();
	BlSockClose(sock);
	BlSetLastError(err);
	return -1;
}

INLINE int BlUdpNewPeerSenderEx(const struct sockaddr* addr, const struct sockaddr* nicAddr, int ttl_or_hops, int loop) {
	int sock = BlSockUdp(addr->sa_family == AF_INET6);
	if (sock < 0)
		return sock;
	int r = BlSockConnectEx(sock, addr, nicAddr, ttl_or_hops, loop);
	if (r == 0)
		return sock;
	int err = BlGetLastError();
	BlSockClose(sock);
	BlSetLastError(err);
	return -1;
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
INLINE int BlUnixBind(int sock, const char* path) {
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
*   @retval <0 failed, call BlGetLastError() for error code
*   @retval otherwise ok
*/
INLINE int BlUnixNewServer(const char* path, bool unlinkPath, int nListen) {
	if (unlinkPath)
		unlink(path);
	int sock = BlSockUnix();
	if (sock >= 0) {
		int r = BlUnixBind(sock, path);
		if (r == 0) {
			r = BlSockListen(sock, nListen);
			if (r == 0)
				return sock;
		}
		int err = BlGetLastError();
		BlSockClose(sock);
		BlSetLastError(err);
	}
	return -1;
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
INLINE bool BlTcpAccept(BlTcpAccept_t* io, int sock, sa_family_t family,
	struct sockaddr* peer, socklen_t* peerLen, BlOnCompletedAio onCompleted) {
	BlInitTcpAccept(io, sock, family, peer, peerLen, onCompleted);
	return BlDoTcpAccept(io);
}

/*
* @brief Connect to server
* 
* @note Support unix domain socket
*/
INLINE bool BlTcpConnect(BlTcpConnect_t* io, int sock,
		const struct sockaddr* addr, BlOnCompletedAio onCompleted) {
	BlInitTcpConnect(io, sock, addr, onCompleted);
	return BlDoTcpConnect(io);
}

/*
* @brief Send data using a socket
*
* @note Support unix domain socket
*/
INLINE bool BlSockSend(BlSockSend_t* io, int sock, const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockSend(io, sock, buf, len, flags, onCompleted);
	return BlDoSockSend(io);
}

/*
* @brief Send data vector using a socket
*
* @note Support unix domain socket
*/
INLINE bool BlSockSendVec(BlSockSendVec_t* io, int sock,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockSendVec(io, sock, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockSendVec(io);
}

/*
* @brief Send data to an address using a socket
*
* @note DON'T support unix domain socket
*/
INLINE bool BlSockSendTo(BlSockSendTo_t* io, int sock, const struct sockaddr* addr,
	const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockSendTo(io, sock, addr, buf, len, flags, onCompleted);
	return BlDoSockSendTo(io);
}

/*
* @brief
* @note DON'T support unix domain socket
*/
INLINE bool BlSockSendVecTo(BlSockSendVecTo_t* io, int sock, const struct sockaddr* addr,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockSendVecTo(io, sock, addr, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockSendVecTo(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockMustSend(BlSockMustSend_t* io, int sock,
	const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockMustSend(io, sock, buf, len, flags, onCompleted);
	return BlDoSockMustSend(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockMustSendVec(BlSockMustSendVec_t* io, int sock,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockMustSendVec(io, sock, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockMustSendVec(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockRecv(BlSockRecv_t* io, int sock, void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockRecv(io, sock, buf, len, flags, onCompleted);
	return BlDoSockRecv(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockRecvVec(BlSockRecvVec_t* io, int sock,
	struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockRecvVec(io, sock, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockRecvVec(io);
}

/*
* @brief
* @note DON'T support unix domain socket
*/
INLINE bool BlSockRecvFrom(BlSockRecvFrom_t* io, int sock, struct sockaddr* addr,
	socklen_t* addrLen, void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockRecvFrom(io, sock, addr, addrLen, buf, len, flags, onCompleted);
	return BlDoSockRecvFrom(io);
}

/*
* @brief
* @note DON'T support unix domain socket
*/
INLINE bool BlSockRecvVecFrom(BlSockRecvVecFrom_t* io, int sock, struct sockaddr* addr,
	socklen_t* addrLen, struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockRecvVecFrom(io, sock, addr, addrLen, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockRecvVecFrom(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockMustRecv(BlSockMustRecv_t* io, int sock,
	void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockMustRecv(io, sock, buf, len, flags, onCompleted);
	return BlDoSockMustRecv(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlSockMustRecvVec(BlSockMustRecvVec_t* io, int sock,
	struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	BlInitSockMustRecvVec(io, sock, bufVec, bufCnt, flags, onCompleted);
	return BlDoSockMustRecvVec(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlTcpClose(BlTcpClose_t* io, int sock, BlOnCompletedAio onCompleted) {
	BlInitTcpClose(io, sock, onCompleted);
	return BlDoTcpClose(io);
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE bool BlTcpShutdown(BlTcpShutdown_t* io, int sock, int how, BlOnCompletedAio onCompleted) {
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

#ifdef WIN32
int BlFileOpenN(const WCHAR* fileName, int flags, int mode);
#else
#define BlFileOpenN BlFileOpen
#endif

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
* @brief Cancel all I/O of (HANDLE,int,...) `fd`
* @param[in] fd file handle or socket or ...
* @param[in] io NULL-cancel all I/O of `fd`, other-cancel the specific I/O related to `io`
* @return
*   @retval -1 error, call BlGetLastError() for error code
*   @retval other ok
*/
int BlCancelIo(int fd, BlAioBase* io);


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
