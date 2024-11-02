#pragma once

#include <assert.h>
#include <fcntl.h>

#include "blaio.h"

#ifdef __cplusplus
extern "C" {
#endif

#if BL_BITNESS == 64
#define BL_KSHIFT 24
#elif BL_BITNESS == 32
#define BL_KSHIFT 0
#else
#error "Unsupported CPU bits"
#endif

#define BL_kSocket       (1<<BL_KSHIFT)
#define BL_kTaskCallback (2<<BL_KSHIFT)

extern HANDLE g_hIocp;
extern LPFN_ACCEPTEX g_AcceptEx;
extern LPFN_CONNECTEX g_ConnectEx;
extern LPFN_DISCONNECTEX g_DisconnectEx;

INLINE void BlPostTask(BlTaskCallback cb, void* parm, bool isIoTask_unused) {
#if BL_BITNESS == 64
	DWORD lo = (size_t)parm;
	DWORD hi = ((size_t)parm) >> 32;
	// because in 64bit os, just lower 47bits of pointer(address) used 
	PostQueuedCompletionStatus(g_hIocp, lo, (BL_kTaskCallback | hi), (LPOVERLAPPED)cb);
#elif BL_BITNESS == 32
	PostQueuedCompletionStatus(g_hIocp, (DWORD)parm, BL_kTaskCallback, (LPOVERLAPPED)cb);
#endif
}

INLINE BlEvent BlCreateEvent(bool initial) {
	return CreateEvent(NULL, FALSE, initial, NULL);
}

INLINE void BlDestroyEvent(BlEvent ev) {
	CloseHandle(ev);
}

INLINE bool BlSetEvent(BlEvent ev) {
	return SetEvent(ev);
}

INLINE bool BlWaitEventForTime(BlEvent ev, uint32_t t) {
	return WaitForSingleObject(ev, t) == WAIT_OBJECT_0;
}

SOCKET BlSockCreate(sa_family_t family, int sockType, int protocol);

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
	return closesocket(sock);
}

INLINE int BlSockSetKeepAliveVals(SOCKET sock, bool onoff, uint32_t keepAliveTime, uint32_t keepAliveInterval) {
	DWORD ulBytesReturn = 0;
	struct tcp_keepalive inKeepAlive;
	inKeepAlive.onoff = (onoff ? 1 : 0);
	inKeepAlive.keepalivetime = keepAliveTime;
	inKeepAlive.keepaliveinterval = keepAliveInterval;
	return WSAIoctl(sock, SIO_KEEPALIVE_VALS, &inKeepAlive, sizeof(struct tcp_keepalive), NULL, 0, &ulBytesReturn, NULL, NULL);
}

INLINE int BlGetLastError() {
	return GetLastError();
}

INLINE void BlSetLastError(int err) {
	SetLastError(err);
}


INLINE int BlSockPoll(SOCKET sock, short events, int timeout) {
	WSAPOLLFD pollfd = {sock, events, 0};
	int r = WSAPoll(&pollfd, 1, timeout);
	return r>0? pollfd.revents: r;
}


struct iovec { // copy from linux, this type should be defined in the anonymous namespace
	void* iov_base;
	size_t iov_len;
};

#define BL_MAX_BUFS_IN_IOVEC 8

INLINE int IoVecConvertToWSABUF(const struct iovec* bufVec, size_t bufCnt, WSABUF* bufs) {
	if (bufCnt > BL_MAX_BUFS_IN_IOVEC)
		return -1;
	for (int i = 0; i < bufCnt; ++i) {
		bufs[i].buf = (char*)bufVec[i].iov_base;
		bufs[i].len = bufVec[i].iov_len;
	}
	return bufCnt;
}

INLINE void WSABUFAdjustAfterIo(WSABUF** pbufVec, size_t* pbufCnt, size_t nXfer) {
	int n = nXfer;
	size_t i = 0;
	size_t bufCnt1 = *pbufCnt;
	WSABUF* bufVec1 = *pbufVec;
	for (; i < bufCnt1; ++i) {
		n -= bufVec1[i].len;
		if (n < 0)
			break;
	}
	if (i < bufCnt1) { // bufVec[i] remain (-n) bytes not used
		bufVec1[i].buf = ((char*)bufVec1[i].buf) + bufVec1[i].len + n;
		bufVec1[i].len = -n;
	}
	else
		assert(n == 0);
	*pbufCnt = bufCnt1 - i;
	*pbufVec = bufVec1 + i;
}

typedef struct tagBlAioBase BlAioBase;
typedef void (*_BlInternalOnCompleted)(BlAioBase* base, int err, DWORD nXfer);
typedef void (*BlOnCompletedAio)(void* coro);
struct tagBlAioBase {
	OVERLAPPED ov;
	_BlInternalOnCompleted internalOnCompleted;
	BlOnCompletedAio onCompleted;
	void* coro;
	int ret;
};
#define _BLAIOBASE_INIT(fn) \
	memset(&io->base.ov, 0, sizeof(OVERLAPPED)); \
	io->base.internalOnCompleted = (_BlInternalOnCompleted)(fn); \
	io->base.onCompleted = onCompleted;

INLINE bool _BlProcessRetInt(int* ret) {
	int err = WSAGetLastError();
	if (err == WSA_IO_PENDING)
		return true;
	*ret = -err;
	return false;
}

INLINE bool _BlProcessRetXferSize(bool ok, int* ret, DWORD nXfer) {
	if (ok) {
		*ret = nXfer;
		return false;
	}
	return _BlProcessRetInt(ret);
}

typedef struct {
	BlAioBase base;

	SOCKET listenSock;
	sa_family_t family;
	char tmpBuf[sizeof(BlSockAddr) * 2];
	struct sockaddr* peer;
	socklen_t* peerLen;
	SOCKET sock;
}  BlTcpAccept_t;

void _BlInternalOnCompletedTcpAccept(BlTcpAccept_t* io, int err, DWORD unused);

INLINE void BlInitTcpAccept(BlTcpAccept_t* io, SOCKET sock, sa_family_t family,
	struct sockaddr* peer, socklen_t* peerLen, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedTcpAccept)
	io->listenSock = sock;
	io->family = family;
	io->peer = peer;
	io->peerLen = peerLen;
}

INLINE void _BlOnTcpAcceptOk(BlTcpAccept_t* io, SOCKET sock, int err) {
	if (err == 0) {
		if (setsockopt(sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&io->listenSock, sizeof(io->listenSock)) != 0)
			err = WSAGetLastError();
	}
	if (err == 0) {
		io->base.ret = sock;
		if (io->peer && io->peerLen) {
			struct sockaddr* sa = (struct sockaddr*)(io->tmpBuf + sizeof(BlSockAddr));
			BlCopySockAddr(io->peer, *io->peerLen, sa);
			*io->peerLen = BlGetSockAddrLen(sa);
		}
	}
	else {
		BlSockClose(sock);
		io->base.ret = -err;
	}
}

INLINE bool BlDoTcpAccept(BlTcpAccept_t* io) {
	sa_family_t family = io->family;
	SOCKET aSock;
	if (family == AF_INET || family == AF_INET6)
		aSock = BlSockTcp(family == AF_INET6);
	else if (family == AF_UNIX)
		aSock = BlSockUnix();
	else
		aSock = INVALID_SOCKET;
	BOOL b = g_AcceptEx(io->listenSock, aSock, io->tmpBuf, 0,
		sizeof(BlSockAddr), sizeof(BlSockAddr), NULL, &io->base.ov);
	if (b)
		return _BlOnTcpAcceptOk(io, aSock, 0), false;

	int err = WSAGetLastError();
	if (err != WSA_IO_PENDING)
		return _BlOnTcpAcceptOk(io, aSock, err), false;

	io->sock = aSock;
	return true;
}

typedef struct {
	BlAioBase base;

	SOCKET sock;
	const struct sockaddr* addr;
} BlTcpConnect_t;

void _BlInternalOnCompletedTcpConnect(BlTcpConnect_t* io, int err, DWORD unused);

INLINE void BlInitTcpConnect(BlTcpConnect_t* io, SOCKET sock, const struct sockaddr* addr, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedTcpConnect)
	io->sock = sock;
	io->addr = addr;
}

INLINE void _BlOnTcpConnectOk(BlTcpConnect_t* io, int err) {
	if (err == 0) {
		if (setsockopt(io->sock, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) != 0)
			err = BlGetLastError();
	}
	io->base.ret = -err;
}

INLINE bool BlDoTcpConnect(BlTcpConnect_t* io) {
	BlSockAddr newaddr;
	newaddr.sa.sa_family = io->addr->sa_family;
	BlClearSockAddr(&newaddr.sa);
	if (BlSockBind(io->sock, &newaddr.sa) != 0) {
		io->base.ret = -BlGetLastError();
		return false;
	}
	BOOL b = g_ConnectEx(io->sock, io->addr, BlGetSockAddrLen(io->addr), NULL, 0, NULL, &io->base.ov);
	if (b) {
		_BlOnTcpConnectOk(io, 0);
		return false;
	}

	return _BlProcessRetInt(&io->base.ret);
}

void _BlInternalOnCompletedXfer(BlAioBase* io, int err, DWORD nXfer);

typedef struct {
	BlAioBase base;

	SOCKET sock;
	DWORD flags;
	WSABUF buf;
} BlSockSend_t;

INLINE void BlInitSockSend(BlSockSend_t* io, SOCKET sock, const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->sock = sock;
	io->buf.buf = (char*)buf;
	io->buf.len = len;
	io->flags = flags;
}

INLINE bool BlDoSockSend(BlSockSend_t* io) {
	DWORD nXfer;
	int r = WSASend(io->sock, &io->buf, 1, &nXfer, io->flags, &io->base.ov, NULL);
	return _BlProcessRetXferSize(r==0, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	SOCKET sock;
	DWORD flags;
	int nBufs;
	WSABUF bufs[BL_MAX_BUFS_IN_IOVEC];
} BlSockSendVec_t;

INLINE void BlInitSockSendVec(BlSockSendVec_t* io, SOCKET sock,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->sock = sock;
	io->flags = flags;
	io->nBufs = IoVecConvertToWSABUF(bufVec, bufCnt, io->bufs);
}

INLINE bool BlDoSockSendVec(BlSockSendVec_t* io) {
	if (io->nBufs <= 0) {
		io->base.ret = -EINVAL; // invalid argument `bufCnt`
		return false;
	}
	DWORD nXfer;
	int r = WSASend(io->sock, io->bufs, io->nBufs, &nXfer, io->flags, &io->base.ov, NULL);
	return _BlProcessRetXferSize(r==0, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	const struct sockaddr* addr;
	SOCKET sock;
	DWORD flags;
	WSABUF buf;
} BlSockSendTo_t;

INLINE void BlInitSockSendTo(BlSockSendTo_t* io, SOCKET sock, const struct sockaddr* addr,
	const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->sock = sock;
	io->addr = addr;
	io->buf.buf = (char*)buf;
	io->buf.len = len;
	io->flags = flags;
}

INLINE bool BlDoSockSendTo(BlSockSendTo_t* io) {
	socklen_t addrLen = BlGetSockAddrLen(io->addr);
	DWORD nXfer;
	int r = WSASendTo(io->sock, &io->buf, 1, &nXfer, io->flags, io->addr, addrLen, &io->base.ov, NULL);
	return _BlProcessRetXferSize(r==0, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	const struct sockaddr* addr;
	SOCKET sock;
	DWORD flags;
	size_t nBufs;
	WSABUF bufs[BL_MAX_BUFS_IN_IOVEC];
} BlSockSendVecTo_t;

INLINE void BlInitSockSendVecTo(BlSockSendVecTo_t* io, SOCKET sock, const struct sockaddr* addr,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->sock = sock;
	io->addr = addr;
	io->flags = flags;
	io->nBufs = IoVecConvertToWSABUF(bufVec, bufCnt, io->bufs);
}

INLINE bool BlDoSockSendVecTo(BlSockSendVecTo_t* io) {
	if (io->nBufs <= 0) {
		io->base.ret = -EINVAL; // invalid argument `bufCnt`
		return false;
	}
	socklen_t addrLen = BlGetSockAddrLen(io->addr);
	DWORD nXfer;
	int r = WSASendTo(io->sock, io->bufs, io->nBufs, &nXfer, io->flags, io->addr, addrLen, &io->base.ov, NULL);
	return _BlProcessRetXferSize(r==0, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	SOCKET sock;
	DWORD flags;
	WSABUF buf;
} BlSockMustSend_t;

void _BlInternalOnCompletedSockMustSend(BlSockMustSend_t* io, int err, DWORD nXfer);

INLINE void BlInitSockMustSend(BlSockMustSend_t* io, SOCKET sock,
	const void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedSockMustSend)
	io->sock = sock;
	io->buf.buf = (char*)buf;
	io->buf.len = len;
	io->flags = flags;
}

INLINE bool _BlOnSockMustSendOk(BlSockMustSend_t* io, int err, DWORD nXfer) {
	if (err == 0) {
		for (;;) {
			if (nXfer <= 0) // peer closed
				err = E_PEER_CLOSED;
			else {
				io->buf.buf += nXfer;
				uint32_t nRemain = io->buf.len - nXfer;
				io->buf.len = nRemain;
				if (nRemain > 0) { // has more data to send
					if (0 == WSASend(io->sock, &io->buf, 1, &nXfer, io->flags, &io->base.ov, NULL))
						continue;

					err = WSAGetLastError();
					if (err == WSA_IO_PENDING)
						return true;
				}
			}
			break;
		}
	}
	io->base.ret = -err;
	return false;
}

INLINE bool BlDoSockMustSend(BlSockMustSend_t* io) {
	DWORD nXfer;
	if (WSASend(io->sock, &io->buf, 1, &nXfer, io->flags, &io->base.ov, NULL) == 0)
		return _BlOnSockMustSendOk(io, 0, nXfer);
	return _BlProcessRetInt(&io->base.ret);
}

typedef struct {
	BlAioBase base;

	SOCKET sock;
	DWORD flags;
	size_t nBufsRemain;
	WSABUF* bufsRemain;
	size_t nBufs;
	WSABUF bufs[BL_MAX_BUFS_IN_IOVEC];
} BlSockMustSendVec_t;

void _BlInternalOnCompletedSockMustSendVec(BlSockMustSendVec_t* io, int err, DWORD nXfer);

INLINE void BlInitSockMustSendVec(BlSockMustSendVec_t* io, SOCKET sock,
	const struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedSockMustSendVec)
	io->sock = sock;
	io->flags = flags;
	io->nBufs = IoVecConvertToWSABUF(bufVec, bufCnt, io->bufs);
}

INLINE bool _BlOnSockMustSendVecOk(BlSockMustSendVec_t* io, int err, DWORD nXfer) {
	if (err == 0) {
		for (;;) {
			if (nXfer <= 0) // peer closed
				err = E_PEER_CLOSED;
			else {
				WSABUFAdjustAfterIo(&io->bufsRemain, &io->nBufsRemain, nXfer);
				if (io->nBufsRemain > 0) { // has more data to send
					int r = WSASend(io->sock, io->bufsRemain, io->nBufsRemain, &nXfer, io->flags, &io->base.ov, NULL);
					if (r == 0)
						continue;

					err = WSAGetLastError();
					if (err == WSA_IO_PENDING)
						return true;
				}
			}
			break;
		}
	}
	io->base.ret = -err;
	return false;
}

INLINE bool BlDoSockMustSendVec(BlSockMustSendVec_t* io) {
	if (io->nBufs <= 0) {
		io->base.ret = -EINVAL; // invalid argument `bufCnt`
		return false;
	}
	io->nBufsRemain = io->nBufs;
	io->bufsRemain = io->bufs;
	DWORD nXfer;
	int r = WSASend(io->sock, io->bufsRemain, io->nBufsRemain, &nXfer, io->flags, &io->base.ov, NULL);
	if (r == 0)
		return _BlOnSockMustSendVecOk(io, 0, nXfer);
	return _BlProcessRetInt(&io->base.ret);
}

typedef struct {
	BlAioBase base;

	SOCKET sock;
	DWORD flags;
	WSABUF buf;
} BlSockRecv_t;

INLINE void BlInitSockRecv(BlSockRecv_t* io, SOCKET sock, void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->sock = sock;
	io->buf.buf = (char*)buf;
	io->buf.len = len;
	io->flags = flags;
}

INLINE bool BlDoSockRecv(BlSockRecv_t* io) {
	DWORD nXfer;
	int r = WSARecv(io->sock, &io->buf, 1, &nXfer, &io->flags, &io->base.ov, NULL);
	return _BlProcessRetXferSize(r==0, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	SOCKET sock;
	DWORD flags;
	int nBufs;
	WSABUF bufs[BL_MAX_BUFS_IN_IOVEC];
} BlSockRecvVec_t;

INLINE void BlInitSockRecvVec(BlSockRecvVec_t* io, SOCKET sock,
	struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->sock = sock;
	io->flags = flags;
	io->nBufs = IoVecConvertToWSABUF(bufVec, bufCnt, io->bufs);
}

INLINE bool BlDoSockRecvVec(BlSockRecvVec_t* io) {
	if (io->nBufs <= 0) {
		io->base.ret = -EINVAL; // invalid argument `bufCnt`
		return false;
	}
	DWORD nXfer;
	int r = WSARecv(io->sock, io->bufs, io->nBufs, &nXfer, &io->flags, &io->base.ov, NULL);
	return _BlProcessRetXferSize(r==0, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	struct sockaddr* addr;
	socklen_t* addrLen;
	SOCKET sock;
	DWORD flags;
	WSABUF buf;
} BlSockRecvFrom_t;

INLINE void BlInitSockRecvFrom(BlSockRecvFrom_t* io, SOCKET sock, struct sockaddr* addr,
	socklen_t* addrLen, void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->sock = sock;
	io->addr = addr;
	io->addrLen = addrLen;
	io->buf.buf = (char*)buf;
	io->buf.len = len;
	io->flags = flags;
}

INLINE bool BlDoSockRecvFrom(BlSockRecvFrom_t* io) {
	DWORD nXfer;
	int r = WSARecvFrom(io->sock, &io->buf, 1, &nXfer, &io->flags, io->addr, io->addrLen, &io->base.ov, NULL);
	return _BlProcessRetXferSize(r==0, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	struct sockaddr* addr;
	socklen_t* addrLen;
	SOCKET sock;
	DWORD flags;
	size_t nBufs;
	WSABUF bufs[BL_MAX_BUFS_IN_IOVEC];
} BlSockRecvVecFrom_t;

INLINE void BlInitSockRecvVecFrom(BlSockRecvVecFrom_t* io, SOCKET sock, struct sockaddr* addr,
	socklen_t* addrLen, struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->sock = sock;
	io->addr = addr;
	io->addrLen = addrLen;
	io->flags = flags;
	io->nBufs = IoVecConvertToWSABUF(bufVec, bufCnt, io->bufs);
}

INLINE bool BlDoSockRecvVecFrom(BlSockRecvVecFrom_t* io) {
	if (io->nBufs <= 0) {
		io->base.ret = -EINVAL; // invalid argument `bufCnt`
		return false;
	}
	DWORD nXfer;
	int r = WSARecvFrom(io->sock, io->bufs, io->nBufs, &nXfer, &io->flags,
		io->addr, io->addrLen, &io->base.ov, NULL);
	return _BlProcessRetXferSize(r==0, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	SOCKET sock;
	DWORD flags;
	WSABUF buf;
} BlSockMustRecv_t;

void _BlInternalOnCompletedMustRecv(BlSockMustRecv_t* io, int err, DWORD nXfer);

INLINE void BlInitSockMustRecv(BlSockMustRecv_t* io, SOCKET sock,
	void* buf, uint32_t len, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedMustRecv)
	io->sock = sock;
	io->buf.buf = (char*)buf;
	io->buf.len = len;
	io->flags = flags;
}

INLINE bool _BlOnSockMustRecvOk(BlSockMustRecv_t* io, int err, DWORD nXfer){
	if (err == 0) {
		for (;;) {
			if (nXfer <= 0) // peer closed
				err = E_PEER_CLOSED;
			else {
				io->buf.buf += nXfer;
				uint32_t nRemain = io->buf.len - nXfer;
				io->buf.len = nRemain;
				if (nRemain > 0) { // has more data to recv
					if (WSARecv(io->sock, &io->buf, 1, &nXfer, &io->flags, &io->base.ov, NULL) == 0)
						continue;

					err = WSAGetLastError();
					if (err == WSA_IO_PENDING)
						return true;
				}
			}
			break;
		}
	}
	io->base.ret = -err;
	return false;
}

INLINE bool BlDoSockMustRecv(BlSockMustRecv_t* io) {
	DWORD nXfer;
	if (WSARecv(io->sock, &io->buf, 1, &nXfer, &io->flags, &io->base.ov, NULL) == 0)
		return _BlOnSockMustRecvOk(io, 0, nXfer);
	return _BlProcessRetInt(&io->base.ret);
}

typedef struct {
	BlAioBase base;

	SOCKET sock;
	DWORD flags;
	size_t nBufsRemain;
	WSABUF* bufsRemain;
	size_t nBufs;
	WSABUF bufs[BL_MAX_BUFS_IN_IOVEC];
} BlSockMustRecvVec_t;

void _BlInternalOnCompletedMustRecvVec(BlSockMustRecvVec_t* io, int err, DWORD nXfer);

INLINE void BlInitSockMustRecvVec(BlSockMustRecvVec_t* io, SOCKET sock,
	struct iovec* bufVec, size_t bufCnt, int flags, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedMustRecvVec)
	io->sock = sock;
	io->flags = flags;
	io->nBufs = IoVecConvertToWSABUF(bufVec, bufCnt, io->bufs);
}

INLINE bool _BlOnSockMustRecvVecOk(BlSockMustRecvVec_t* io, int err, DWORD nXfer) {
	if (err == 0) {
		for (;;) {
			if (nXfer <= 0) // peer closed
				err = E_PEER_CLOSED;
			else {
				WSABUFAdjustAfterIo(&io->bufsRemain, &io->nBufsRemain, nXfer);
				if (io->nBufsRemain > 0) { // has more data to recv
					int r = WSARecv(io->sock, io->bufsRemain, io->nBufsRemain,
						&nXfer, &io->flags, &io->base.ov, NULL);
					if (r == 0)
						continue;

					err = WSAGetLastError();
					if (err == WSA_IO_PENDING)
						return true;
				}
			}
			break;
		}
	}
	io->base.ret = -err;
	return false;
}

INLINE bool BlDoSockMustRecvVec(BlSockMustRecvVec_t* io) {
	if (io->nBufs <= 0) {
		io->base.ret = -EINVAL; // invalid argument `bufCnt`
		return false;
	}
	io->nBufsRemain = io->nBufs;
	io->bufsRemain = io->bufs;
	DWORD nXfer;
	int r = WSARecv(io->sock, io->bufsRemain, io->nBufsRemain, &nXfer, &io->flags, &io->base.ov, NULL);
	if (r == 0)
		return _BlOnSockMustRecvVecOk(io, 0, nXfer);
	return _BlProcessRetInt(&io->base.ret);
}

typedef struct {
	BlAioBase base;

	SOCKET sock;
} BlTcpClose_t;

void _BlInternalOnCompletedTcpClose(BlTcpClose_t* io, int err, DWORD unused);

INLINE void BlInitTcpClose(BlTcpClose_t* io, SOCKET sock, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedTcpClose)
	io->sock = sock;
}

INLINE bool BlDoTcpClose(BlTcpClose_t* io) {
	BOOL b = g_DisconnectEx(io->sock, &io->base.ov, 0, 0);
	if (b) {
		BlSockClose(io->sock);
		io->base.ret = 0;
		return false;
	}
	return _BlProcessRetInt(&io->base.ret);
}

void _BlInternalOnCompletedAioRetInt(BlAioBase* io, int err, DWORD unused);

typedef struct {
	BlAioBase base;

	SOCKET sock;
	int how;
} BlTcpShutdown_t;

INLINE void BlInitTcpShutdown(BlTcpShutdown_t* io, SOCKET sock, int how, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedAioRetInt)
	io->sock = sock;
	io->how = how;
}

INLINE bool BlDoTcpShutdown(BlTcpShutdown_t* io) {
	BOOL b = g_DisconnectEx(io->sock, &io->base.ov, 0, 0);
	if (b) {
		io->base.ret = 0;
		return false;
	}
	return _BlProcessRetInt(&io->base.ret);
}


/* BlFileOpen() flags supported on this platform: */
static const int
	BLFILE_O_APPEND       = _O_APPEND,
	BLFILE_O_CREAT        = _O_CREAT,
	BLFILE_O_EXCL         = _O_EXCL,
	BLFILE_O_RANDOM       = _O_RANDOM,
	BLFILE_O_RDONLY       = _O_RDONLY,
	BLFILE_O_RDWR         = _O_RDWR,
	BLFILE_O_SEQUENTIAL   = _O_SEQUENTIAL,
	BLFILE_O_SHORT_LIVED  = _O_SHORT_LIVED,
	BLFILE_O_TEMPORARY    = _O_TEMPORARY,
	BLFILE_O_TRUNC        = _O_TRUNC,
	BLFILE_O_WRONLY       = _O_WRONLY,

	/* fs open() flags supported on other platforms (or mapped on this platform): */
	BLFILE_O_DIRECT       = 0x02000000, /* FILE_FLAG_NO_BUFFERING */
	BLFILE_O_DIRECTORY    = 0,
	BLFILE_O_DSYNC        = 0x04000000, /* FILE_FLAG_WRITE_THROUGH */
	BLFILE_O_EXLOCK       = 0x10000000, /* EXCLUSIVE SHARING MODE */
	BLFILE_O_NOATIME      = 0,
	BLFILE_O_NOCTTY       = 0,
	BLFILE_O_NOFOLLOW     = 0,
	BLFILE_O_NONBLOCK     = 0,
	BLFILE_O_SYMLINK      = 0,
	BLFILE_O_SYNC         = 0x08000000; /* FILE_FLAG_WRITE_THROUGH */

INLINE WCHAR* _BlUtf8ToWCHAR(const char* s, int n, int* pwn) {
	if (n < 0)
		n = strlen(s) + 1;
	int wn = 2 * n;
	WCHAR* ws = (WCHAR*)malloc(wn);
	if (!ws)
		return NULL;
	n = MultiByteToWideChar(CP_UTF8, 0, s, n, ws, wn);
	if (n <= 0) {
		free(ws);
		return NULL;
	}
	if(pwn)
		*pwn = n;
	return ws;
}

INLINE WCHAR* _BlUtf8PathToWCHAR(const char* s, int n, int* pwn) {
	int wn;
	WCHAR* ws = _BlUtf8ToWCHAR(s, n, &wn);
	if (!ws)
		return NULL;
	for (int i = 0; i < wn; ++i) { // replace all unix path splitor to windows path splitor
		if (ws[i] == L'/')
			ws[i] = L'\\';
	}
	if (pwn)
		*pwn = wn;
	return ws;
}

INLINE HANDLE _BlCreateFileUtf8(
	_In_ const char* fileName, /* utf8 */
	_In_ DWORD dwDesiredAccess,
	_In_ DWORD dwShareMode,
	_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	_In_ DWORD dwCreationDisposition,
	_In_ DWORD dwFlagsAndAttributes,
	_In_opt_ HANDLE hTemplateFile
) {
	WCHAR* pathw = _BlUtf8PathToWCHAR(fileName, -1, NULL);
	if (!pathw) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}
	HANDLE file = CreateFileW(pathw,
		dwDesiredAccess,
		dwShareMode,
		lpSecurityAttributes,
		dwCreationDisposition,
		dwFlagsAndAttributes,
		hTemplateFile);
	free(pathw);
	return file;
}

INLINE int BlFileClose(int f) {
#pragma warning(push)
#pragma warning(disable: 4312)
	return CloseHandle((HANDLE)f) ? 0 : -1;
#pragma warning(pop)
}

typedef struct {
	BlAioBase base;

	int f;
	uint32_t bufLen;
	void* buf;
} BlFileRead_t;

INLINE void BlInitFileRead(BlFileRead_t* io, int f, uint64_t offset,
	void* buf, uint32_t len, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->f = f;
	io->base.ov.Pointer = (PVOID)offset;
	io->buf = buf;
	io->bufLen = len;
}

INLINE bool BlDoFileRead(BlFileRead_t* io) {
#pragma warning(push)
#pragma warning(disable: 4312)
	DWORD nXfer;
	bool b = ReadFile((HANDLE)io->f, io->buf, io->bufLen, &nXfer, &io->base.ov);
#pragma warning(pop)
	return _BlProcessRetXferSize(b, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	int f;
	struct iovec* bufs;
	size_t bufCnt;
} BlFileReadVec_t;

void _BlInternalOnCompletedFileReadVec(BlFileReadVec_t* io, int err, DWORD nXfer);

INLINE void BlInitFileReadVec(BlFileReadVec_t* io, int f, uint64_t offset,
	struct iovec* bufs, size_t bufCnt, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedFileReadVec)
	io->f = f;
	io->base.ov.Pointer = (PVOID)offset;
	io->base.ret = 0;
	io->bufs = bufs;
	io->bufCnt = bufCnt;
}

INLINE bool _BlOnFileReadVecOk(BlFileReadVec_t* io, int err, DWORD nXfer) {
	if (err == 0) {
		for (;;) {
			if (nXfer <= 0) {
				if (io->base.ret <= 0)
					io->base.ret = -ERROR_HANDLE_EOF;
				return false;
			}
			io->base.ret += nXfer;
			if (nXfer < io->bufs->iov_len)
				return false;
			if (--(io->bufCnt) <= 0)
				return false;
			++(io->bufs);
			io->base.ov.Pointer = ((char*)io->base.ov.Pointer) + nXfer;
#pragma warning(push)
#pragma warning(disable: 4312)
			if (ReadFile((HANDLE)io->f, io->bufs->iov_base, io->bufs->iov_len, &nXfer, &io->base.ov))
				continue;
#pragma warning(pop)
			err = WSAGetLastError();
			if (err == WSA_IO_PENDING)
				return true;
			if (err == ERROR_HANDLE_EOF)
				return false;
			io->base.ret = -err;
			return false;
		}
	}
	if (err != ERROR_HANDLE_EOF || io->base.ret == 0)
		io->base.ret = -err;
	return false;
}

INLINE bool BlDoFileReadVec(BlFileReadVec_t* io) {
#pragma warning(push)
#pragma warning(disable: 4312)
	DWORD nXfer;
	bool b = ReadFile((HANDLE)io->f, io->bufs->iov_base, io->bufs->iov_len, &nXfer, &io->base.ov);
#pragma warning(pop)
	if (b)
		return _BlOnFileReadVecOk(io, 0, nXfer);
	return _BlProcessRetXferSize(b, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	int f;
	uint32_t bufLen;
	const void* buf;
} BlFileWrite_t;

INLINE void BlInitFileWrite(BlFileWrite_t* io, int f, uint64_t offset,
	const void* buf, uint32_t len, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedXfer)
	io->f = f;
	io->base.ov.Pointer = (PVOID)offset;
	io->buf = buf;
	io->bufLen = len;
}

INLINE bool BlDoFileWrite(BlFileWrite_t* io) {
	DWORD nXfer;
#pragma warning(push)
#pragma warning(disable: 4312)
	bool b = WriteFile((HANDLE)io->f, io->buf, io->bufLen, &nXfer, &io->base.ov);
#pragma warning(pop)
	return _BlProcessRetXferSize(b, &io->base.ret, nXfer);
}

typedef struct {
	BlAioBase base;

	int f;
	const struct iovec* bufs;
	size_t bufCnt;
} BlFileWriteVec_t;

void _BlInternalOnCompletedFileWriteVec(BlFileWriteVec_t* io, int err, DWORD nXfer);

INLINE void BlInitFileWriteVec(BlFileWriteVec_t* io, int f, uint64_t offset,
	const struct iovec* bufs, size_t bufCnt, BlOnCompletedAio onCompleted) {
	_BLAIOBASE_INIT(_BlInternalOnCompletedFileWriteVec)
	io->f = f;
	io->base.ov.Pointer = (PVOID)offset;
	io->base.ret = 0;
	io->bufs = bufs;
	io->bufCnt = bufCnt;
}

INLINE bool _BlOnFileWriteVecOk(BlFileWriteVec_t* io, int err, DWORD nXfer) {
	if (err == 0) {
		for (;;) {
			if (nXfer <= 0) {
				if (io->base.ret <= 0)
					io->base.ret = -ERROR_HANDLE_EOF;
				return false;
			}
			io->base.ret += nXfer;
			if (nXfer < io->bufs->iov_len)
				return false;
			if (--(io->bufCnt) <= 0)
				return false;
			++(io->bufs);
			io->base.ov.Pointer = ((char*)io->base.ov.Pointer) + nXfer;
#pragma warning(push)
#pragma warning(disable: 4312)
			if (WriteFile((HANDLE)io->f, io->bufs->iov_base, io->bufs->iov_len, &nXfer, &io->base.ov))
				continue;
#pragma warning(pop)
			err = WSAGetLastError();
			if (err == WSA_IO_PENDING)
				return true;
			if (err == ERROR_HANDLE_EOF)
				return false;
			io->base.ret = -err;
			return false;
		}
	}
	if (err != ERROR_HANDLE_EOF || io->base.ret == 0)
		io->base.ret = -err;
	return false;
}

INLINE bool BlDoFileWriteVec(BlFileWriteVec_t* io) {
#pragma warning(push)
#pragma warning(disable: 4312)
	DWORD nXfer;
	bool b = WriteFile((HANDLE)io->f, io->bufs->iov_base, io->bufs->iov_len, &nXfer, &io->base.ov);
#pragma warning(pop)
	if (b)
		return _BlOnFileWriteVecOk(io, 0, nXfer);
	return _BlProcessRetXferSize(b, &io->base.ret, nXfer);
}

INLINE int BlCancelIo(int fd, BlAioBase* io) {
#pragma warning(push)
#pragma warning(disable: 4312)
	if(CancelIoEx((HANDLE)fd, (LPOVERLAPPED)io))
#pragma warning(pop)
		return 0;
	return -1;
}

#ifdef __cplusplus
}
#endif
