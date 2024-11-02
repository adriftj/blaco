#pragma once

#include <assert.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <liburing.h>
#include "fileflags.h"

#include "blaio.h"

#ifdef __cplusplus
extern "C" {
#endif

extern size_t g_numCpus;

/* BlFileOpen() flags supported on this platform: */
static const int
	BLFILE_O_APPEND       = UV_FS_O_APPEND,
	BLFILE_O_CREAT        = UV_FS_O_CREAT,
	BLFILE_O_EXCL         = UV_FS_O_EXCL,
	BLFILE_O_RANDOM       = UV_FS_O_RANDOM,
	BLFILE_O_RDONLY       = UV_FS_O_RDONLY,
	BLFILE_O_RDWR         = UV_FS_O_RDWR,
	BLFILE_O_SEQUENTIAL   = UV_FS_O_SEQUENTIAL,
	BLFILE_O_SHORT_LIVED  = UV_FS_O_SHORT_LIVED,
	BLFILE_O_TEMPORARY    = UV_FS_O_TEMPORARY,
	BLFILE_O_TRUNC        = UV_FS_O_TRUNC,
	BLFILE_O_WRONLY       = UV_FS_O_WRONLY,
	BLFILE_O_DIRECT       = UV_FS_O_DIRECT,
	BLFILE_O_DIRECTORY    = UV_FS_O_DIRECTORY,
	BLFILE_O_DSYNC        = UV_FS_O_DSYNC,
	BLFILE_O_EXLOCK       = UV_FS_O_EXLOCK,
	BLFILE_O_NOATIME      = UV_FS_O_NOATIME,
	BLFILE_O_NOCTTY       = UV_FS_O_NOCTTY,
	BLFILE_O_NOFOLLOW     = UV_FS_O_NOFOLLOW,
	BLFILE_O_NONBLOCK     = UV_FS_O_NONBLOCK,
	BLFILE_O_SYMLINK      = UV_FS_O_SYMLINK,
	BLFILE_O_SYNC         = UV_FS_O_SYNC;

INLINE int BlFileOpen(const char* fileName, int flags, int mode) {
	return open(fileName, flags, mode);
}

INLINE int BlFileClose(int f) {
	return close(f);
}

INLINE int BlSockPoll(SOCKET sock, short events, int timeout) {
	struct pollfd fds = { sock, events, 0 };
	int r = poll(&fds, 1, timeout);
	return r > 0 ? fds.revents : r;
}

INLINE BlEvent BlCreateEvent(bool initial) {
	return eventfd(initial ? 1 : 0, 0);
}

INLINE void BlDestroyEvent(BlEvent ev) {
	close(ev);
}

INLINE bool BlSetEvent(BlEvent ev) {
	uint64_t cnt = 1;
	return write(ev, &cnt, sizeof(cnt)) == sizeof(cnt);
}

INLINE bool BlWaitEventForTime(BlEvent ev, uint32_t t) {
	uint64_t cnt;
	if(t==INFINITE)
		return read(ev, &cnt, sizeof(cnt)) == sizeof(cnt);
	int r = BlSockPoll(ev, POLLRDNORM, t);
	if (r == POLLRDNORM)
		return read(ev, &cnt, sizeof(cnt)) == sizeof(cnt);
	return false;
}

INLINE SOCKET BlSockCreate(sa_family_t family, int sockType, int protocol) {
	return socket(family, sockType, protocol);
}

INLINE SOCKET BlSockRaw(int protocol, bool ipv6) {
	return BlSockCreate(ipv6? AF_INET6: AF_INET, SOCK_RAW, protocol);
}

INLINE SOCKET BlSockTcp(bool ipv6) {
	return BlSockCreate(ipv6? AF_INET6: AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

INLINE SOCKET BlSockUdp(bool ipv6) {
	return BlSockCreate(ipv6? AF_INET6: AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

INLINE SOCKET BlSockUdpLite(bool ipv6) {
	return BlSockCreate(ipv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 136);
}

#ifdef SUPPORT_AF_UNIX
INLINE SOCKET BlSockUnix() {
	return BlSockCreate(AF_UNIX, SOCK_STREAM, 0);
}
#endif

INLINE int BlSockClose(SOCKET sock) {
	return close(sock);
}

INLINE int BlGetLastError() {
	return errno;
}

INLINE void BlSetLastError(int err) {
	errno = err;
}

typedef struct tagBlAioBase BlAioBase;
typedef void (*_BlOnSqe)(BlAioBase* base, struct io_uring_sqe* sqe);
typedef void (*_BlOnCqe)(BlAioBase* base, int r);
typedef void (*BlOnCompletedAio)(void* coro);
struct tagBlAioBase {
	BlAioBase* next;
	_BlOnSqe onSqe;
	_BlOnCqe onCqe;
	BlOnCompletedAio onCompleted;
	void* coro;
	int ret;
};
#define _BLAIOBASE_INITX(fSqe, fCqe) \
	io->base.onSqe = (_BlOnSqe)fSqe; \
	io->base.onCqe = (_BlOnCqe)fCqe; \
	io->base.onCompleted = onCompleted;
#define _BLAIOBASE_INIT(fSqe, fCqe) _BLAIOBASE_INITX(fSqe, fCqe)
#define _BLAIOBASE_INITMSG(fSqe, fCqe) _BLAIOBASE_INITX(fSqe, fCqe)

void _BlOnCqeAio(BlAioBase* io, int r);
bool _BlDoAio(BlAioBase* io);

typedef struct {
	BlAioBase base;

	SOCKET listenSock;
	struct sockaddr* peer;
	socklen_t* peerLen;
} BlTcpAccept_t;

void _BlOnSqeTcpAccept(BlTcpAccept_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitTcpAccept(BlTcpAccept_t* io, SOCKET sock, sa_family_t unused,
		struct sockaddr* peer, socklen_t* peerLen, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeTcpAccept, _BlOnCqeAio)
	io->listenSock = sock;
	io->peer = peer;
	io->peerLen = peerLen;
}

INLINE bool BlDoTcpAccept(BlTcpAccept_t* io) { return _BlDoAio((BlAioBase*)io); }


typedef struct {
	BlAioBase base;

	SOCKET sock;
	const struct sockaddr* addr;
} BlTcpConnect_t;

void _BlOnSqeTcpConnect(BlTcpConnect_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitTcpConnect(BlTcpConnect_t* io, SOCKET sock, const struct sockaddr* addr, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeTcpConnect, _BlOnCqeAio)
	io->sock = sock;
	io->addr = addr;
}

INLINE bool BlDoTcpConnect(BlTcpConnect_t* io) { return _BlDoAio((BlAioBase*)io); }


typedef struct {
	BlAioBase base;

	SOCKET sock;
	const void* buf;
	uint32_t len;
	int flags;
} BlSockSend_t;

void _BlOnSqeSockSend(BlSockSend_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitSockSend(BlSockSend_t* io, SOCKET sock, const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeSockSend, _BlOnCqeAio)
	io->sock = sock;
	io->buf = buf;
	io->len = len;
	io->flags = flags;
}

INLINE bool BlDoSockSend(BlSockSend_t* io) { return _BlDoAio((BlAioBase*)io); }

#define _BlSockMsgMembers \
	BlAioBase base;      \
	SOCKET sock;          \
	struct msghdr msghdr; \
	int flags;

typedef struct {
	_BlSockMsgMembers
} _BlSockSendRecvMsg;

#define _BLAIOMSG_INIT(onSqe, onCqe, vec, vecCnt) \
	_BLAIOBASE_INITMSG(onSqe, onCqe)            \
	io->sock = sock;                            \
	memset(&io->msghdr, 0, sizeof(io->msghdr)); \
	io->flags = flags;                          \
	io->msghdr.msg_iov = (struct iovec*)(vec);  \
	io->msghdr.msg_iovlen = (vecCnt);

void _BlOnSqeSockSendXMsg(_BlSockSendRecvMsg* io, struct io_uring_sqe* sqe);
void _BlOnSqeSockRecvXMsg(_BlSockSendRecvMsg* io, struct io_uring_sqe* sqe);

typedef struct {
	_BlSockMsgMembers
} BlSockSendVec_t;

INLINE void BlInitSockSendVec(BlSockSendVec_t* io, SOCKET sock,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOMSG_INIT(_BlOnSqeSockSendXMsg, _BlOnCqeAio, bufVec, bufCnt)
}

INLINE bool BlDoSockSendVec(BlSockSendVec_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	_BlSockMsgMembers
	struct iovec buf;
} BlSockSendTo_t;

INLINE void BlInitSockSendTo(BlSockSendTo_t* io, SOCKET sock, const struct sockaddr* addr,
	const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOMSG_INIT(_BlOnSqeSockSendXMsg, _BlOnCqeAio, &io->buf, 1)
	io->buf.iov_base = (void*)buf;
	io->buf.iov_len = len;
	io->msghdr.msg_name = (struct sockaddr*)addr;
	io->msghdr.msg_namelen = BlGetSockAddrLen(addr);
}

INLINE bool BlDoSockSendTo(BlSockSendTo_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	_BlSockMsgMembers
} BlSockSendVecTo_t;

INLINE void BlInitSockSendVecTo(BlSockSendVecTo_t* io, SOCKET sock, const struct sockaddr* addr,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOMSG_INIT(_BlOnSqeSockSendXMsg, _BlOnCqeAio, bufVec, bufCnt)
	io->msghdr.msg_name = (struct sockaddr*)addr;
	io->msghdr.msg_namelen = BlGetSockAddrLen(addr);
}

INLINE bool BlDoSockSendVecTo(BlSockSendVecTo_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	BlAioBase base;

	SOCKET sock;
	const void* buf;
	uint32_t len;
	int flags;
} BlSockMustSend_t;

void _BlOnSqeSockMustSend(BlSockMustSend_t* io, struct io_uring_sqe* sqe);
void _BlOnCqeSockMustSend(BlSockMustSend_t* io, int r);

INLINE void BlInitSockMustSend(BlSockMustSend_t* io, SOCKET sock,
	const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeSockMustSend, _BlOnCqeSockMustSend)
	io->sock = sock;
	io->buf = buf;
	io->len = len;
	io->flags = flags;
}

INLINE bool BlDoSockMustSend(BlSockMustSend_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	_BlSockMsgMembers
} BlSockMustSendVec_t;

void _BlOnCqeSockMustSendVec(BlSockMustSendVec_t* io, int r);

INLINE void BlInitSockMustSendVec(BlSockMustSendVec_t* io, SOCKET sock,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOMSG_INIT(_BlOnSqeSockSendXMsg, _BlOnCqeSockMustSendVec, bufVec, bufCnt)
}

INLINE bool BlDoSockMustSendVec(BlSockMustSendVec_t* io) { return _BlDoAio((BlAioBase*)io); }


typedef struct {
	BlAioBase base;

	SOCKET sock;
	void* buf;
	uint32_t len;
	int flags;
} BlSockRecv_t;

void _BlOnSqeSockRecv(BlSockRecv_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitSockRecv(BlSockRecv_t* io, SOCKET sock, void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeSockRecv, _BlOnCqeAio)
	io->sock = sock;
	io->buf = buf;
	io->len = len;
	io->flags = flags;
}

INLINE bool BlDoSockRecv(BlSockRecv_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	_BlSockMsgMembers
} BlSockRecvVec_t;

INLINE void BlInitSockRecvVec(BlSockRecvVec_t* io, SOCKET sock,
	struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOMSG_INIT(_BlOnSqeSockRecvXMsg, _BlOnCqeAio, bufVec, bufCnt)
}

INLINE bool BlDoSockRecvVec(BlSockRecvVec_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	_BlSockMsgMembers
	struct iovec buf;
	socklen_t* addrLen;
} BlSockRecvFrom_t;

void _BlOnCqeSockRecvFrom(BlSockRecvFrom_t* io, int r);

INLINE void BlInitSockRecvFrom(BlSockRecvFrom_t* io, SOCKET sock, struct sockaddr* addr, socklen_t* addrLen,
	void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOMSG_INIT(_BlOnSqeSockRecvXMsg, _BlOnCqeSockRecvFrom, &io->buf, 1)
	io->buf.iov_base = buf;
	io->buf.iov_len = len;
	io->msghdr.msg_name = addr;
	if (addrLen)
		io->msghdr.msg_namelen = *addrLen;
	io->addrLen = addrLen;
}

INLINE bool BlDoSockRecvFrom(BlSockRecvFrom_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	_BlSockMsgMembers
	socklen_t* addrLen;
} BlSockRecvVecFrom_t;

void _BlOnCqeSockRecvVecFrom(BlSockRecvVecFrom_t* io, int r);

INLINE void BlInitSockRecvVecFrom(BlSockRecvVecFrom_t* io, SOCKET sock, struct sockaddr* addr, socklen_t* addrLen,
	struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOMSG_INIT(_BlOnSqeSockRecvXMsg, _BlOnCqeSockRecvVecFrom, bufVec, bufCnt)
	io->msghdr.msg_name = addr;
	if (addrLen)
		io->msghdr.msg_namelen = *addrLen;
	io->addrLen = addrLen;
}

INLINE bool BlDoSockRecvVecFrom(BlSockRecvVecFrom_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	BlAioBase base;

	SOCKET sock;
	void* buf;
	uint32_t len;
	int flags;
} BlSockMustRecv_t;

void _BlOnSqeSockMustRecv(BlSockMustRecv_t* io, struct io_uring_sqe* sqe);
void _BlOnCqeSockMustRecv(BlSockMustRecv_t* io, int r);

INLINE void BlInitSockMustRecv(BlSockMustRecv_t* io, SOCKET sock,
	void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeSockMustRecv, _BlOnCqeSockMustRecv)
	io->sock = sock;
	io->buf = buf;
	io->len = len;
	io->flags = flags;
}

INLINE bool BlDoSockMustRecv(BlSockMustRecv_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	_BlSockMsgMembers
} BlSockMustRecvVec_t;

void _BlOnCqeSockMustRecvVec(BlSockMustRecvVec_t* io, int r);

INLINE void BlInitSockMustRecvVec(BlSockMustRecvVec_t* io, SOCKET sock,
	struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOMSG_INIT(_BlOnSqeSockRecvXMsg, _BlOnCqeSockMustRecvVec, bufVec, bufCnt)
}

INLINE bool BlDoSockMustRecvVec(BlSockMustRecvVec_t* io) { return _BlDoAio((BlAioBase*)io); }


typedef struct {
	BlAioBase base;

	SOCKET sock;
} BlTcpClose_t;

void _BlOnSqeTcpClose(BlTcpClose_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitTcpClose(BlTcpClose_t* io, SOCKET sock, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeTcpClose, _BlOnCqeAio)
	io->sock = sock;
}

INLINE bool BlDoTcpClose(BlTcpClose_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	BlAioBase base;

	SOCKET sock;
	int how;
} BlTcpShutdown_t;

void _BlOnSqeTcpShutdown(BlTcpShutdown_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitTcpShutdown(BlTcpShutdown_t* io, SOCKET sock, int how, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeTcpShutdown, _BlOnCqeAio)
	io->sock = sock;
	io->how = how;
}

INLINE bool BlDoTcpShutdown(BlTcpShutdown_t* io) { return _BlDoAio((BlAioBase*)io); }


typedef struct {
	BlAioBase base;

	uint64_t offset;
	int f;
	void* buf;
	uint32_t bufLen;
} BlFileRead_t;

void _BlOnSqeFileRead(BlFileRead_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitFileRead(BlFileRead_t* io, int f, uint64_t offset,
	void* buf, uint32_t len, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeFileRead, _BlOnCqeAio)
	io->f = f;
	io->offset = offset;
	io->buf = buf;
	io->bufLen = len;
}

INLINE bool BlDoFileRead(BlFileRead_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	BlAioBase base;

	uint64_t offset;
	int f;
	struct iovec* bufs;
	unsigned bufCnt;
} BlFileReadVec_t;

void _BlOnSqeFileReadVec(BlFileReadVec_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitFileReadVec(BlFileReadVec_t* io, int f, uint64_t offset,
	struct iovec* bufs, size_t bufCnt, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeFileReadVec, _BlOnCqeAio)
	io->f = f;
	io->offset = offset;
	io->bufs = bufs;
	io->bufCnt = bufCnt;
}

INLINE bool BlDoFileReadVec(BlFileReadVec_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	BlAioBase base;

	uint64_t offset;
	int f;
	const void* buf;
	uint32_t bufLen;
} BlFileWrite_t;

void _BlOnSqeFileWrite(BlFileWrite_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitFileWrite(BlFileWrite_t* io, int f, uint64_t offset,
	const void* buf, uint32_t len, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeFileWrite, _BlOnCqeAio)
	io->f = f;
	io->offset = offset;
	io->buf = buf;
	io->bufLen = len;
}

INLINE bool BlDoFileWrite(BlFileWrite_t* io) { return _BlDoAio((BlAioBase*)io); }

typedef struct {
	BlAioBase base;

	uint64_t offset;
	int f;
	const struct iovec* bufs;
	unsigned bufCnt;
} BlFileWriteVec_t;

void _BlOnSqeFileWriteVec(BlFileWriteVec_t* io, struct io_uring_sqe* sqe);

INLINE void BlInitFileWriteVec(BlFileWriteVec_t* io, int f, uint64_t offset,
	const struct iovec* bufs, size_t bufCnt, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlOnSqeFileWriteVec, _BlOnCqeAio)
	io->f = f;
	io->offset = offset;
	io->bufs = bufs;
	io->bufCnt = bufCnt;
}

INLINE bool BlDoFileWriteVec(BlFileWriteVec_t* io) { return _BlDoAio((BlAioBase*)io); }

#ifdef __cplusplus
}
#endif
