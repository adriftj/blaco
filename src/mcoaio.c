#include <stdio.h>
#include "mcoaio.h"

typedef struct {
	SOCKET sock;
	sa_family_t family;
	McoFnStartSession fn;
	const McoTcpServerOptions* opts;
} McoTcpAcceptLoopParm;

typedef struct {
	SOCKET sock;
	BlSockAddr peer;
	McoFnStartSession fn;
	const McoTcpServerOptions* opts;
} McoTcpSessionLoopParm;

static void McoTcpSessionLoop(mco_coro* coro) {
	McoTcpSessionLoopParm* parm = (McoTcpSessionLoopParm*)coro->storage;
	parm->fn(parm->sock, &parm->peer.sa, coro->user_data, parm->opts);
}

INLINE void Tlog(McoFnLog fnLog, void* logger, const char* s, int r) {
	if (fnLog)
		fnLog(logger, s, r);
}

static void McoTcpAcceptLoop(mco_coro* coro) {
	McoTcpAcceptLoopParm* parm = (McoTcpAcceptLoopParm*)coro->storage;
	SOCKET aSock;
	BlSockAddr peer;
	socklen_t peerLen;
	mco_coro* coroSession;
	mco_desc desc = mco_desc_init(McoTcpSessionLoop, parm->opts->sessionStackSize);
	desc.storage_size = sizeof(McoTcpSessionLoopParm);
	for (;;) {
		peerLen = sizeof(peer);
		aSock = McoTcpAccept(parm->sock, parm->family, &peer.sa, &peerLen);
		if (aSock < 0) {
			Tlog(parm->opts->fnLog, parm->opts->logger, "McoTcpAccept failed", -aSock);
			//McoTcpClose(parm->sock);//TODO: FIXME: how to handle this situation?
			break;
		}
		mco_result r = mco_create(&coroSession, &desc);
		if (r != MCO_SUCCESS) {
			McoTcpClose(aSock);
			Tlog(parm->opts->fnLog, parm->opts->logger, "mco_create failed for session", r);
			McoSleep(1000); // Don't accept new connection for a while
			continue;
		}
		McoTcpSessionLoopParm* sessionParm = (McoTcpSessionLoopParm*)coroSession->storage;
		sessionParm->sock = aSock;
		BlCopySockAddr(&sessionParm->peer.sa, sizeof(sessionParm->peer), &peer.sa);
		sessionParm->fn = parm->fn;
		sessionParm->opts = parm->opts;
		coroSession->user_data = coro->user_data;
		McoSchedule(coroSession, true);
	}
}

static McoTcpServerOptions s_defaultServerOptions = {
	0,     // numConcurrentAccepts
	0,     // sessionStackSize
	NULL,  // fnLog
	NULL   // logger
};

void McoTcpStartServer(SOCKET sock, sa_family_t family,
		McoFnStartSession fn, void* fnParm, const McoTcpServerOptions* opts) {
	if (!opts)
		opts = &s_defaultServerOptions;
	size_t numConcurrentAccepts = opts->numConcurrentAccepts;
	if(numConcurrentAccepts == 0)
		numConcurrentAccepts = g_numCpus;
	mco_desc desc = mco_desc_init(McoTcpAcceptLoop, 1);
	desc.storage_size = sizeof(McoTcpAcceptLoopParm);
	for (int i = 0; i < numConcurrentAccepts; ++i) {
		mco_coro* coro;
		int r = mco_create(&coro, &desc);
		if (r != MCO_SUCCESS) {
			Tlog(opts->fnLog, opts->logger, "mco_create failed for accept loop", r);
			continue;
		}
		McoTcpAcceptLoopParm* parm = (McoTcpAcceptLoopParm*)coro->storage;
		parm->sock = sock;
		parm->family = family;
		parm->fn = fn;
		parm->opts = opts;
		coro->user_data = fnParm;
		McoSchedule(coro, true);
	}
}
