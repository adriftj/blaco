#pragma once

#include "blaio.h"
#include "minicoro.h"

#ifdef _WIN32
#define mco_resume_io mco_resume_mt
#define mco_yield_io mco_yield_mt
#else
#define mco_resume_io mco_resume
#define mco_yield_io mco_yield
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
* @brief Schedule a suspended coroutine to run in thread pool
* @param[in] coro the suspended coroutine
* @param[in] isIoTask true-should be scheduled to run in io worker threads, false-other
* @warning if `coro` is not suspended, the behavior is undefined(maybe continue run in current thread).
*/
INLINE void McoSchedule(mco_coro* coro, bool isIoTask) {
	BlPostTask((BlTaskCallback)mco_resume, coro, isIoTask);
}

/*
* @brief Accept a connection from client
*
* @note Support unix domain socket
* @return
*   @retval ret>=0, ok. ret is the accepted socket, `peer` was filled.
*   @retval ret<0, -ret is the error code, *peer not modified.
*/
INLINE int McoTcpAccept(SOCKET sock, sa_family_t family, struct sockaddr* peer, socklen_t* peerLen) {
	BlTcpAccept_t io;
	BlInitTcpAccept(&io, sock, family, peer, peerLen, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoTcpAccept(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief Connect to server
* 
* @note Support unix domain socket
*/
INLINE int McoTcpConnect(SOCKET sock, const struct sockaddr* addr) {
	BlTcpConnect_t io;
	BlInitTcpConnect(&io, sock, addr, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoTcpConnect(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief Send data using a socket
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
* @note Support unix domain socket
*/
INLINE int McoSockSend(SOCKET sock, const void* buf, uint32_t len, int flags) {
	BlSockSend_t io;
	BlInitSockSend(&io, sock, buf, len, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockSend(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief Send data vector using a socket
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
* @note Support unix domain socket
*/
INLINE int McoSockSendVec(SOCKET sock, const struct iovec* bufVec, size_t bufCnt, int flags) {
	BlSockSendVec_t io;
	BlInitSockSendVec(&io, sock, bufVec, bufCnt, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if(BlDoSockSendVec(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief Send data to an address using a socket
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
* @note DON'T support unix domain socket
*/
INLINE int McoSockSendTo(SOCKET sock, const struct sockaddr* addr,
	const void* buf, uint32_t len, int flags) {
	BlSockSendTo_t io;
	BlInitSockSendTo(&io, sock, addr, buf, len, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockSendTo(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
* @note DON'T support unix domain socket
*/
INLINE int McoSockSendVecTo(SOCKET sock, const struct sockaddr* addr,
	const struct iovec* bufVec, size_t bufCnt, int flags) {
	BlSockSendVecTo_t io;
	BlInitSockSendVecTo(&io, sock, addr, bufVec, bufCnt, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockSendVecTo(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @return err
*   @retval err=0, ok. Received n bytes data exactly.
*   @retval err<>0, the error code.
* @note Support unix domain socket
*/
INLINE int McoSockMustSend(SOCKET sock, const void* buf, uint32_t len, int flags) {
	BlSockMustSend_t io;
	BlInitSockMustSend(&io, sock, buf, len, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockMustSend(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @return err
*   @retval err=0, ok. Received n bytes data exactly.
*   @retval err<>0, the error code.
* @note Support unix domain socket
*/
INLINE int McoSockMustSendVec(SOCKET sock, const struct iovec* bufVec, size_t bufCnt, int flags) {
	BlSockMustSendVec_t io;
	BlInitSockMustSendVec(&io, sock, bufVec, bufCnt, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockMustSendVec(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
* @note Support unix domain socket
*/
INLINE int McoSockRecv(SOCKET sock, void* buf, uint32_t len, int flags) {
	BlSockRecv_t io;
	BlInitSockRecv(&io, sock, buf, len, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockRecv(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
* @note Support unix domain socket
*/
INLINE int McoSockRecvVec(SOCKET sock, struct iovec* bufVec, size_t bufCnt, int flags) {
	BlSockRecvVec_t io;
	BlInitSockRecvVec(&io, sock, bufVec, bufCnt, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockRecvVec(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
* @note DON'T support unix domain socket
*/
INLINE int McoSockRecvFrom(SOCKET sock, struct sockaddr* addr,
	socklen_t* addrLen, void* buf, uint32_t len, int flags) {
	BlSockRecvFrom_t io;
	BlInitSockRecvFrom(&io, sock, addr, addrLen, buf, len, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockRecvFrom(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
* @note DON'T support unix domain socket
*/
INLINE int McoSockRecvVecFrom(SOCKET sock, struct sockaddr* addr, socklen_t* addrLen,
	struct iovec* bufVec, size_t bufCnt, int flags) {
	BlSockRecvVecFrom_t io;
	BlInitSockRecvVecFrom(&io, sock, addr, addrLen, bufVec, bufCnt, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockRecvVecFrom(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @return err
*   @retval err=0, ok. Received n bytes data exactly.
*   @retval err<>0, the error code.
* @note Support unix domain socket
*/
INLINE int McoSockMustRecv(SOCKET sock, void* buf, uint32_t len, int flags) {
	BlSockMustRecv_t io;
	BlInitSockMustRecv(&io, sock, buf, len, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockMustRecv(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @return err
*   @retval err=0, ok. Received n bytes data exactly.
*   @retval err<>0, the error code.
* @note Support unix domain socket
*/
INLINE int McoSockMustRecvVec(SOCKET sock, struct iovec* bufVec, size_t bufCnt, int flags) {
	BlSockMustRecvVec_t io;
	BlInitSockMustRecvVec(&io, sock, bufVec, bufCnt, flags, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoSockMustRecvVec(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE int McoTcpClose(SOCKET sock) {
	BlTcpClose_t io;
	BlInitTcpClose(&io, sock, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoTcpClose(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief
* @note Support unix domain socket
*/
INLINE int McoTcpShutdown(SOCKET sock, int how) {
	BlTcpShutdown_t io;
	BlInitTcpShutdown(&io, sock, how, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoTcpShutdown(&io))
		mco_yield_io();
	return io.base.ret;
}

typedef void (*McoFnLog)(void* logger, const char* s, int r);

typedef struct {
	size_t numConcurrentAccepts;
	size_t sessionStackSize;
	McoFnLog fnLog;
	void* logger;
} McoTcpServerOptions;

typedef void (*McoFnStartSession)(SOCKET, const struct sockaddr*, void* parm, const McoTcpServerOptions* opts);

/*
* @brief Start a TCP server
* @param[in] sock the listening socket of server
* @param[in] family sock address family
* @param[in] fn session handler
* @param[in] fnParm
* @param[in] opts options, NULL for default
*/
void McoTcpStartServer(SOCKET sock, sa_family_t family,
	McoFnStartSession fn, void* fnParm, const McoTcpServerOptions* opts);

/*
* @brief Read file
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
*/
INLINE int McoFileRead(int f, uint64_t offset, void* buf, uint32_t len) {
	BlFileRead_t io;
	BlInitFileRead(&io, f, offset, buf, len, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoFileRead(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief ReadVec file
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
*/
INLINE int McoFileReadVec(int f, uint64_t offset, struct iovec* bufs, size_t bufCnt) {
	BlFileReadVec_t io;
	BlInitFileReadVec(&io, f, offset, bufs, bufCnt, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoFileReadVec(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief Write file
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
*/
INLINE int McoFileWrite(int f, uint64_t offset, const void* buf, uint32_t len) {
	BlFileWrite_t io;
	BlInitFileWrite(&io, f, offset, buf, len, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoFileWrite(&io))
		mco_yield_io();
	return io.base.ret;
}

/*
* @brief WriteVec file
* @return
*   @retval ret>=0, ok. ret is the size of transfered data.
*   @retval ret<0, -ret is the error code.
*/
INLINE int McoFileWriteVec(int f, uint64_t offset, const struct iovec* bufs, size_t bufCnt) {
	BlFileWriteVec_t io;
	BlInitFileWriteVec(&io, f, offset, bufs, bufCnt, (BlOnCompletedAio)mco_resume_io);
	io.base.coro = mco_running();
	if (BlDoFileWriteVec(&io))
		mco_yield_io();
	return io.base.ret;
}

INLINE void McoCutexLock(BlCutex* cutex) {
	BlCutexCoro coro;
	coro.onLocked = (BlCutexOnLocked)mco_resume_mt;
	coro.coro = mco_running();
	if(!BlCutexLock(cutex, &coro))
		mco_yield_mt();
}

/*
* @brief make the current coroutine into sleeping
* @param[in] ms milliseconds to sleep
*/
INLINE void McoSleep(uint32_t ms) {
	if (ms > 0) {
		BlAddOnceTimer(-((int64_t)ms), (BlTaskCallback)mco_resume, mco_running());
		mco_yield();
	}
}

#ifdef __cplusplus
}
#endif
