#include "blaio.h"

void _BlInternalOnCompletedTcpAccept(BlTcpAccept_t* io, int err, DWORD unused) {
	_BlOnTcpAcceptOk(io, io->sock, err);
	if (io->base.onCompleted)
		io->base.onCompleted(io->base.coro);
}

void _BlInternalOnCompletedTcpConnect(BlTcpConnect_t* io, int err, DWORD unused) {
	_BlOnTcpConnectOk(io, err);
	if (io->base.onCompleted)
		io->base.onCompleted(io->base.coro);
}

void _BlInternalOnCompletedXfer(BlAioBase* io, int err, DWORD nXfer) {
	io->ret = err == 0 ? nXfer : -err;
	if (io->onCompleted)
		io->onCompleted(io->coro);
}

void _BlInternalOnCompletedSockMustSend(BlSockMustSend_t* io, int err, DWORD nXfer) {
	if (!_BlOnSockMustSendOk(io, err, nXfer) && io->base.onCompleted)
		io->base.onCompleted(io->base.coro);
}

void _BlInternalOnCompletedSockMustSendVec(BlSockMustSendVec_t* io, int err, DWORD nXfer) {
	if (!_BlOnSockMustSendVecOk(io, err, nXfer) && io->base.onCompleted)
		io->base.onCompleted(io->base.coro);
}

void _BlInternalOnCompletedMustRecv(BlSockMustRecv_t* io, int err, DWORD nXfer) {
	if (!_BlOnSockMustRecvOk(io, err, nXfer) && io->base.onCompleted)
		io->base.onCompleted(io->base.coro);
}

void _BlInternalOnCompletedMustRecvVec(BlSockMustRecvVec_t* io, int err, DWORD nXfer) {
	if (!_BlOnSockMustRecvVecOk(io, err, nXfer) && io->base.onCompleted)
		io->base.onCompleted(io->base.coro);
}

void _BlInternalOnCompletedTcpClose(BlTcpClose_t* io, int err, DWORD unused) {
	io->base.ret = err;
	BlSockClose(io->sock);
	if (io->base.onCompleted)
		io->base.onCompleted(io->base.coro);
}

void _BlInternalOnCompletedAioRetInt(BlAioBase* io, int err, DWORD unused) {
	io->ret = err;
	if (io->onCompleted)
		io->onCompleted(io->coro);
}

void _BlInternalOnCompletedFileReadVec(BlFileReadVec_t* io, int err, DWORD nXfer) {
	if (!_BlOnFileReadVecOk(io, err, nXfer) && io->base.onCompleted)
		io->base.onCompleted(io->base.coro);
}

void _BlInternalOnCompletedFileWriteVec(BlFileWriteVec_t* io, int err, DWORD nXfer) {
	if (!_BlOnFileWriteVecOk(io, err, nXfer) && io->base.onCompleted)
		io->base.onCompleted(io->base.coro);
}
