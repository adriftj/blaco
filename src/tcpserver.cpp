#include "blaco.hpp"

namespace bl {

struct TcpServerParm {
	FnStartTcpSession fn;
	bool ioTask;
	FnTcpServerLog fnLog;
};

static void _OnAccepted(TcpServerParm* parm, int acceptedSock, const struct sockaddr* peer) {
	bl::go(parm->fn(acceptedSock, *peer, parm->fnLog).get_handle(), parm->ioTask);
}

static void _OnFailure(TcpServerParm* parm, int sock, int err) {
	parm->fnLog("TcpAccept failed", err);
}

static void _OnStopped(TcpServerParm* parm) {
	parm->fnLog("TcpAcceptLoop stopped", 0);
	delete parm;
}

UINT64 TcpStartServer(int sock, sa_family_t family, FnStartTcpSession fn, bool ioTask, FnTcpServerLog fnLog, size_t numConcurrentAccepts) {
	if (numConcurrentAccepts == 0)
		numConcurrentAccepts = BL_gNumCpus;
	TcpServerParm* parm = new TcpServerParm{};
	if (!parm) {
		fnLog("TcpStartServer: failed to new TcpServerParm", 0);
		return 0;
	}
	parm->fn = fn;
	parm->ioTask = ioTask;
	parm->fnLog = fnLog;
	UINT64 h = BlSockStartAcceptLoop(sock, family, numConcurrentAccepts, (BlOnSockAccepted)_OnAccepted,
		(BlOnSockAcceptFailure)_OnFailure, (BlOnSockAcceptLoopStopped)_OnStopped, parm);
	if (!h) {
		fnLog("TcpStartServer: failed to start", BlGetLastError());
		delete parm;
	}
	return h;
}

} // end of namespace bl
