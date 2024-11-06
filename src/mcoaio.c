#include <stdio.h>
#include <stdlib.h>
#include "oslock.h"
#include "mcoaio.h"

typedef struct {
	int sock;
	sa_family_t family;
	McoFnStartSession fn;
	void* fnParm;
	bool ioTask;
	McoTcpServerOptions opts;
} McoTcpAcceptLoopParm;

typedef struct {
	int sock;
	BlSockAddr peer;
	McoTcpAcceptLoopParm* loop;
} McoTcpSessionLoopParm;

static void McoTcpSessionLoop(mco_coro* coro) {
	McoTcpSessionLoopParm* parm = (McoTcpSessionLoopParm*)coro->storage;
	parm->loop->fn(parm->sock, &parm->peer.sa, coro->user_data, &parm->loop->opts);
	BlAddOnceTimer(-100, (BlTaskCallback)mco_destroy, coro); // Can't destroy running coro, defer to 100ms after
}

INLINE void Tlog(McoFnLog fnLog, void* logger, const char* s, int r) {
	if (fnLog)
		fnLog(logger, s, r);
}

static McoTcpServerOptions s_defaultServerOptions = {
	0,     // numConcurrentAccepts
	0,     // sessionStackSize
	NULL,  // fnLog
	NULL   // logger
};

static void _OnAccepted(McoTcpAcceptLoopParm* parm, int acceptedSock, const struct sockaddr* peer) {
	mco_coro* coroSession;
	mco_desc desc = mco_desc_init(McoTcpSessionLoop, parm->opts.sessionStackSize);
	desc.storage_size = sizeof(McoTcpSessionLoopParm);
	mco_result r = mco_create(&coroSession, &desc);
	if (r != MCO_SUCCESS) {
		McoTcpClose(acceptedSock);
		Tlog(parm->opts.fnLog, parm->opts.logger, "mco_create failed for session", r);
		return;
	}
	McoTcpSessionLoopParm* sessionParm = (McoTcpSessionLoopParm*)coroSession->storage;
	sessionParm->sock = acceptedSock;
	BlCopySockAddr(&sessionParm->peer.sa, sizeof(sessionParm->peer), peer);
	sessionParm->loop = parm;
	coroSession->user_data = parm->fnParm;
	McoSchedule(coroSession, parm->ioTask);
}

static void _OnFailure(McoTcpAcceptLoopParm* parm, int sock, int err) {
	Tlog(parm->opts.fnLog, parm->opts.logger, "McoTcpAccept failed", err);
}

static void _OnStopped(McoTcpAcceptLoopParm* parm) {
	Tlog(parm->opts.fnLog, parm->opts.logger, "Mco AcceptLoop stopped", 0);
	free(parm);
}

UINT64 McoTcpStartServer(int sock, sa_family_t family,
		McoFnStartSession fn, void* fnParm, bool ioTask, const McoTcpServerOptions* opts) {
	if (!opts)
		opts = &s_defaultServerOptions;
	size_t numConcurrentAccepts = opts->numConcurrentAccepts;
	if(numConcurrentAccepts == 0)
		numConcurrentAccepts = BL_gNumCpus;
	McoTcpAcceptLoopParm* parm = (McoTcpAcceptLoopParm*)malloc(sizeof(McoTcpAcceptLoopParm));
	if (!parm) {
		Tlog(opts->fnLog, opts->logger, "McoStartServer: failed to malloc", 0);
		return 0;
	}
	parm->sock = sock;
	parm->family = family;
	parm->fn = fn;
	parm->fnParm = fnParm;
	parm->ioTask = ioTask;
	parm->opts = *opts;
	UINT64 h = BlSockStartAcceptLoop(sock, family, numConcurrentAccepts, (BlOnSockAccepted)_OnAccepted,
		(BlOnSockAcceptFailure)_OnFailure, (BlOnSockAcceptLoopStopped)_OnStopped, parm);
	if (!h) {
		Tlog(opts->fnLog, opts->logger, "McoStartServer: failed", BlGetLastError());
		free(parm);
	}
	return h;
}
