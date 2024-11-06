#include <thread>
#include <algorithm>
#include <set>
#include <map>
#include <tuple>
#include <queue>
#include "oslock.h"
#include "blaco.hpp"
#include "minicoro.h"
#include "../../timer_priv.h"

LPFN_ACCEPTEX BL_gAcceptEx = NULL;
LPFN_CONNECTEX BL_gConnectEx = NULL;
LPFN_DISCONNECTEX BL_gDisconnectEx = NULL;

HANDLE BL_gIocp = NULL;

struct Worker : public TimerMgr {
};
BL_THREAD_LOCAL Worker* st_worker = nullptr;

static std::set<std::tuple<int, int, int>> s_canSocketSkipNotify;
static Worker* s_workers = nullptr;
static HANDLE s_initSema = NULL;
static size_t s_nextWorker = 0;
static bool s_exitApplication = false;
static size_t s_numWorkers = 0;

struct AcceptLoop {
	int sock;
	size_t numAccepts;
};
static bl::Mutex s_lockAcceptLoop;
static std::map<UINT64, AcceptLoop> s_acceptLoops; // first: handle
static std::map<int, UINT64> s_acceptLoopSocks; // first: sock, second: handle of AcceptLoop
static UINT64 s_lastAcceptLoopHandle = 0;

static void FatalErr(const char* sErr, const char* file, int line) {
	DWORD err = GetLastError();
	char errBuf[256];
	sprintf_s(errBuf, "%s(err=%d) in %s:%d", sErr, err, file, line);
	//TODO: add log
	throw errBuf;
}

#define FATAL_ERR(sErr) FatalErr((sErr), __FILE__, __LINE__)

static void LoadWsaFuncs() {
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	DWORD dwBytes;

	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	int ret = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx, sizeof(GuidAcceptEx), &BL_gAcceptEx, sizeof(BL_gAcceptEx), &dwBytes, NULL, NULL);
	if (ret == SOCKET_ERROR)
		FATAL_ERR("Can load AcceptEx");

	GUID GuidConnectEx = WSAID_CONNECTEX;
	ret = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidConnectEx, sizeof(GuidConnectEx), &BL_gConnectEx, sizeof(BL_gConnectEx), &dwBytes, NULL, NULL);
	if (ret == SOCKET_ERROR)
		FATAL_ERR("Can load ConnectEx");

	GUID GuidDisconnectEx = WSAID_DISCONNECTEX;
	ret = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidDisconnectEx, sizeof(GuidDisconnectEx), &BL_gDisconnectEx, sizeof(BL_gDisconnectEx), &dwBytes, NULL, NULL);
	if (ret == SOCKET_ERROR)
		FATAL_ERR("Can load DisconnectEx");

	BlSockClose(sock);
}

// Enumerate available protocol providers for the specified socket type.
static void EnumWsaProtocols() {
	WSAPROTOCOL_INFOW stackInfos[16];
	std::unique_ptr<WSAPROTOCOL_INFOW[]> heapInfos;
	WSAPROTOCOL_INFOW* selectedProtocolInfo = nullptr;

	DWORD bufferSize = sizeof(stackInfos);
	WSAPROTOCOL_INFOW* infos = stackInfos;

	int protocolCount = ::WSAEnumProtocolsW(NULL, infos, &bufferSize);
	if (protocolCount == SOCKET_ERROR) {
		int errorCode = ::WSAGetLastError();
		if (errorCode == WSAENOBUFS) {
			DWORD requiredElementCount = bufferSize / sizeof(WSAPROTOCOL_INFOW);
			heapInfos = std::make_unique<WSAPROTOCOL_INFOW[]>(requiredElementCount);
			bufferSize = requiredElementCount * sizeof(WSAPROTOCOL_INFOW);
			infos = heapInfos.get();
			protocolCount = ::WSAEnumProtocolsW(NULL, infos, &bufferSize);
		}
		if (protocolCount == SOCKET_ERROR)
			FATAL_ERR("WSAEnumProtocolsW error");
	}
	if (protocolCount == 0)
		FATAL_ERR("protocol_not_supported");

	for (int i = 0; i < protocolCount; ++i) {
		auto& info = infos[i];
		if ((info.dwServiceFlags1 & XP1_IFS_HANDLES) != 0)
			s_canSocketSkipNotify.emplace(info.iAddressFamily, info.iSocketType, info.iProtocol);
	}
}

void _BlIoLoop(size_t slot) {
	Worker* worker = s_workers + slot;
	st_worker = worker;
	DWORD toWait;
	int err;
	DWORD bytesXfer;
	ULONG_PTR perHandle;
	BlAioBase* lazyio;
	OVERLAPPED_ENTRY ovEntry;
	ULONG nEntries = 0;
	ReleaseSemaphore(s_initSema, 1, NULL);
	for (;;) {
		lazyio = nullptr;
		err = 0;
		toWait = GetWaitTime(&worker->onceTimerQ, &worker->timerMap, &worker->timerQ);
		if (!::GetQueuedCompletionStatusEx(BL_gIocp, &ovEntry, 1, &nEntries, toWait, TRUE)) {
			err = GetLastError();
			if (err == ERROR_SEM_TIMEOUT || err == WAIT_TIMEOUT)
				continue;
			if (err == ERROR_ABANDONED_WAIT_0 || err == ERROR_INVALID_HANDLE) // iocp handle is closed
				break;
			if (nEntries == 1 && !ovEntry.lpOverlapped) {
				break; // TODO: other reason?
			}
		}
		if (nEntries < 1)
			continue;
		bytesXfer = ovEntry.dwNumberOfBytesTransferred;
		perHandle = ovEntry.lpCompletionKey;
		lazyio = (BlAioBase*)ovEntry.lpOverlapped;

		if (perHandle == BL_kSocket)
			lazyio->internalOnCompleted(lazyio, err, bytesXfer);
#if BL_BITNESS == 64
		else if ((perHandle & 0xff000000)==BL_kTaskCallback) {
			void* parm = (void*)(bytesXfer | ( size_t(perHandle&0x00ffffff)<<32 ) );
			((BlTaskCallback)lazyio)(parm);
		}
#else
		else if (perHandle == BL_kTaskCallback)
			((BlTaskCallback)lazyio)((void*)bytesXfer);
#endif
		else {
			// TODO: add error log 'unknown perHandle'
			assert(false);
		}
	}

	mco_thread_cleanup();
}

int BlSockCreate(sa_family_t family, int sockType, int protocol) {
	int sock = WSASocket(family, sockType, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (sock >= 0) {
		if (BL_gIocp != CreateIoCompletionPort((HANDLE)(intptr_t)sock, BL_gIocp, BL_kSocket, 0)) {
			int err = BlGetLastError();
			BlSockClose(sock);
			BlSetLastError(err);
			return -1;
		}
		if (s_canSocketSkipNotify.contains({family, sockType, protocol}))
			SetFileCompletionNotificationModes((HANDLE)(intptr_t)sock, FILE_SKIP_SET_EVENT_ON_HANDLE | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
	}
	return sock;
}

void BlInit(uint32_t options, size_t numIoWorkers, size_t numOtherWorkers) {
	if (!BL_gDisconnectEx) {
		WSADATA wsadata;
		if (::WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
			FATAL_ERR("Can not initialize winsock");
		LoadWsaFuncs();
		EnumWsaProtocols();
	}

	BL_gIocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, BL_gNumCpus);
	if (BL_gIocp == NULL)
		FATAL_ERR("Can not create iocp");
	if (numIoWorkers == 0)
		numIoWorkers = BL_gNumCpus;
	if (numOtherWorkers == 0)
		numOtherWorkers = BL_gNumCpus;
	size_t numWorkers = numIoWorkers + numOtherWorkers;
	s_numWorkers = numWorkers;
	if (numWorkers >= 4096)
		FATAL_ERR("Two many workers");
	s_initSema = CreateSemaphore(NULL, 0, s_numWorkers, NULL);
	if (s_initSema == NULL)
		FATAL_ERR("Can not create init sema");
	s_workers = new Worker[numWorkers];
	bool useMain = (options & BL_INIT_USE_MAIN_THREAD_AS_WORKER);
	for (size_t i = 0; i < numWorkers; ++i) {
		s_workers[i].slot = i;
		s_workers[i].nextTimerId = 0;
		if (!useMain || i > 0)
			s_workers[i].thread = std::thread([i]() { _BlIoLoop(i); });
	}
	for(size_t i = 0; i<numWorkers; ++i)
		WaitForSingleObject(s_initSema, INFINITE);
}

bool _BlPostTaskToWorker(size_t slot, BlTaskCallback cb, void* parm) {
	if (slot >= s_numWorkers)
		return false;
	DWORD r = QueueUserAPC((PAPCFUNC)cb, s_workers[slot].thread.native_handle(), (ULONG_PTR)parm);
	return r != 0;
}

inline size_t PickupWorker(bool /*isIoTask*/) {
	size_t x = s_nextWorker++;
	if (s_nextWorker >= s_numWorkers)
		s_nextWorker = 0;
	if (x >= s_numWorkers)
		x = 0;
	return x;
}

#include "../../blaco_priv.h"

void BlExitNotify() {
	s_exitApplication = true;
	CloseHandle(BL_gIocp);
}

void BlWaitExited() {
	_BlWaitExited();
	// WSACleanup(); // Needn't cleanup, all will be cleaned when process exits
	BL_gIocp = NULL;
}

int BlFileOpen(const char* fileName, int flags, int mode) {
	WCHAR* pathw = _BlUtf8PathToWCHAR(fileName, -1, NULL);
	if (!pathw) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return -1;
	}
	int f = BlFileOpenN(pathw, flags, mode);
	free(pathw);
	return f;
}

int BlFileOpenN(const WCHAR* fileName, int flags, int mode) {
	DWORD access;
	DWORD share;
	DWORD disposition;
	DWORD attributes = 0;
	HANDLE file;

	/* convert flags and mode to CreateFile parameters */
	switch (flags & (BLFILE_O_RDONLY | BLFILE_O_WRONLY | BLFILE_O_RDWR)) {
	case BLFILE_O_RDONLY:
		access = FILE_GENERIC_READ;
		break;
	case BLFILE_O_WRONLY:
		access = FILE_GENERIC_WRITE;
		break;
	case BLFILE_O_RDWR:
		access = FILE_GENERIC_READ | FILE_GENERIC_WRITE;
		break;
	default:
		BlSetLastError(ERROR_INVALID_PARAMETER);
		return -1;
	}

	if (flags & BLFILE_O_APPEND) {
		access &= ~FILE_WRITE_DATA;
		access |= FILE_APPEND_DATA;
	}

	/*
	 * Here is where we deviate significantly from what CRT's _open()
	 * does. We indiscriminately use all the sharing modes, to match
	 * UNIX semantics. In particular, this ensures that the file can
	 * be deleted even whilst it's open, fixing issue
	 * https://github.com/nodejs/node-v0.x-archive/issues/1449.
	 * We still support exclusive sharing mode, since it is necessary
	 * for opening raw block devices, otherwise Windows will prevent
	 * any attempt to write past the master boot record.
	 */
	share = (flags & BLFILE_O_EXLOCK)? 0: (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);

	switch (flags & (BLFILE_O_CREAT | BLFILE_O_EXCL | BLFILE_O_TRUNC)) {
	case 0:
	case BLFILE_O_EXCL:
		disposition = OPEN_EXISTING;
		break;
	case BLFILE_O_CREAT:
		disposition = OPEN_ALWAYS;
		break;
	case BLFILE_O_CREAT | BLFILE_O_EXCL:
	case BLFILE_O_CREAT | BLFILE_O_TRUNC | BLFILE_O_EXCL:
		disposition = CREATE_NEW;
		break;
	case BLFILE_O_TRUNC:
	case BLFILE_O_TRUNC | BLFILE_O_EXCL:
		disposition = TRUNCATE_EXISTING;
		break;
	case BLFILE_O_CREAT | BLFILE_O_TRUNC:
		disposition = CREATE_ALWAYS;
		break;
	default:
		BlSetLastError(ERROR_INVALID_PARAMETER);
		return -1;
	}

	attributes |= FILE_ATTRIBUTE_NORMAL;
	if (flags & BLFILE_O_CREAT) {
		if (!(mode & _S_IWRITE))
			attributes |= FILE_ATTRIBUTE_READONLY;
	}

	if (flags & BLFILE_O_TEMPORARY) {
		attributes |= FILE_FLAG_DELETE_ON_CLOSE | FILE_ATTRIBUTE_TEMPORARY;
		access |= DELETE;
	}

	if (flags & BLFILE_O_SHORT_LIVED)
		attributes |= FILE_ATTRIBUTE_TEMPORARY;

	switch (flags & (BLFILE_O_SEQUENTIAL | BLFILE_O_RANDOM)) {
	case 0:
		break;
	case BLFILE_O_SEQUENTIAL:
		attributes |= FILE_FLAG_SEQUENTIAL_SCAN;
		break;
	case BLFILE_O_RANDOM:
		attributes |= FILE_FLAG_RANDOM_ACCESS;
		break;
	default:
		BlSetLastError(ERROR_INVALID_PARAMETER);
		return -1;
	}

	if (flags & BLFILE_O_DIRECT) {
		/*
		 * FILE_APPEND_DATA and FILE_FLAG_NO_BUFFERING are mutually exclusive.
		 * Windows returns 87, ERROR_INVALID_PARAMETER if these are combined.
		 *
		 * FILE_APPEND_DATA is included in FILE_GENERIC_WRITE:
		 *
		 * FILE_GENERIC_WRITE = STANDARD_RIGHTS_WRITE |
		 *                      FILE_WRITE_DATA |
		 *                      FILE_WRITE_ATTRIBUTES |
		 *                      FILE_WRITE_EA |
		 *                      FILE_APPEND_DATA |
		 *                      SYNCHRONIZE
		 *
		 * Note: Appends are also permitted by FILE_WRITE_DATA.
		 *
		 * In order for direct writes and direct appends to succeed, we therefore
		 * exclude FILE_APPEND_DATA if FILE_WRITE_DATA is specified, and otherwise
		 * fail if the user's sole permission is a direct append, since this
		 * particular combination is invalid.
		 */
		if (access & FILE_APPEND_DATA) {
			if (access & FILE_WRITE_DATA)
				access &= ~FILE_APPEND_DATA;
			else {
				BlSetLastError(ERROR_INVALID_PARAMETER);
				return -1;
			}
		}
		attributes |= FILE_FLAG_NO_BUFFERING;
	}

	switch (flags & (BLFILE_O_DSYNC | BLFILE_O_SYNC)) {
	case 0:
		break;
	case BLFILE_O_DSYNC:
	case BLFILE_O_SYNC:
		attributes |= FILE_FLAG_WRITE_THROUGH;
		break;
	default:
		BlSetLastError(ERROR_INVALID_PARAMETER);
		return -1;
	}

	/* Setting this flag makes it possible to open a directory. */
	attributes |= FILE_FLAG_BACKUP_SEMANTICS;
	attributes |= FILE_FLAG_OVERLAPPED;
	file = CreateFileW(fileName,
		access,
		share,
		NULL,
		disposition,
		attributes,
		NULL);
	if (file == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		if (error == ERROR_FILE_EXISTS && (flags & BLFILE_O_CREAT) &&
			!(flags & BLFILE_O_EXCL)) {
			/* Special case: when ERROR_FILE_EXISTS happens and BLFILE_O_CREAT was
			 * specified, it means the path referred to a directory. */
			BlSetLastError(ERROR_DIRECTORY_NOT_SUPPORTED);
		}
		return -1;
	}

	if (BL_gIocp != CreateIoCompletionPort(file, BL_gIocp, BL_kSocket, 0)) {
		int err = BlGetLastError();
		CloseHandle(file);
		BlSetLastError(err);
		return -1;
	}

	SetFileCompletionNotificationModes(file, FILE_SKIP_SET_EVENT_ON_HANDLE | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
	return (int)(intptr_t)file;
}

static bool _BlDoTcpAccept2(BlTcpAccept_t* io);

static void _BlOnTcpAcceptOk2(BlTcpAccept_t* io, int sock, int err) {
	if (err == 0) {
		if (setsockopt(sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&io->listenSock, sizeof(io->listenSock)) != 0)
			err = WSAGetLastError();
	}
	if (err == 0) {
		// accepted
		if (io->base.onCompleted) {
			((BlOnSockAccepted)io->base.onCompleted)(io->base.coro, sock, (struct sockaddr*)(io->tmpBuf + sizeof(BlSockAddr)));
			_BlDoTcpAccept2(io);
		}
	}
	else {
		// failed
		if (io->peer)
			((BlOnSockAcceptFailure)(io->peer))(io->base.coro, sock, err);
		BlSockClose(sock);
		bool stopped = false;
		{
			bl::Mutex::Auto autoLock(s_lockAcceptLoop);
			auto it = s_acceptLoopSocks.find(io->listenSock);
			if (it != s_acceptLoopSocks.cend()) {
				auto it2 = s_acceptLoops.find(it->second);
				if (it2 != s_acceptLoops.end()) {
					size_t k = --(it2->second.numAccepts);
					if (k == 0) {
						s_acceptLoops.erase(it2);
						s_acceptLoopSocks.erase(it);
						stopped = true;
					}
				}
			}
		}
		BlOnSockAcceptLoopStopped onStopped = (BlOnSockAcceptLoopStopped)io->peerLen;
		void* coro = io->base.coro;
		delete io;
		if (stopped && onStopped)
			onStopped(coro);
	}
}

static void _BlInternalOnCompletedTcpAccept2(BlTcpAccept_t* io, int err, DWORD unused) {
	_BlOnTcpAcceptOk2(io, io->sock, err);
}

static void _BlInitTcpAccept2(BlTcpAccept_t* io, int sock, sa_family_t family, BlOnSockAccepted onAccepted,
			BlOnSockAcceptFailure onFailure, BlOnSockAcceptLoopStopped onStopped, void* parm) {
	memset(&io->base.ov, 0, sizeof(OVERLAPPED));
	io->base.internalOnCompleted = (_BlInternalOnCompleted)(_BlInternalOnCompletedTcpAccept2);
	io->listenSock = sock;
	io->base.coro = parm;
	io->base.onCompleted = (BlOnCompletedAio)onAccepted;
	io->peer = (struct sockaddr*)onFailure;
	io->peerLen = (socklen_t*)onStopped;
	io->family = family;
}

static bool _BlDoTcpAccept2(BlTcpAccept_t* io) {
	sa_family_t family = io->family;
	SOCKET aSock;
	if (family == AF_INET || family == AF_INET6)
		aSock = BlSockTcp(family == AF_INET6);
	else if (family == AF_UNIX)
		aSock = BlSockUnix();
	else
		return _BlOnTcpAcceptOk2(io, -1, EINVAL), false;

	BOOL b = BL_gAcceptEx(io->listenSock, aSock, io->tmpBuf, 0,
		sizeof(BlSockAddr), sizeof(BlSockAddr), NULL, &io->base.ov);
	if (b)
		return _BlOnTcpAcceptOk2(io, aSock, 0), false;

	int err = WSAGetLastError();
	if (err != WSA_IO_PENDING)
		return _BlOnTcpAcceptOk2(io, aSock, err), false;

	io->sock = aSock;
	return true;
}

UINT64 BlSockStartAcceptLoop(int sock, sa_family_t family, size_t numConcurrentAccepts, BlOnSockAccepted onAccepted,
			BlOnSockAcceptFailure onFailure, BlOnSockAcceptLoopStopped onStopped, void* parm) {
	if (sock < 0) {
		BlSetLastError(EINVAL);
		return 0;
	}
	if (numConcurrentAccepts == 0)
		numConcurrentAccepts = BL_gNumCpus;

	UINT64 h = 0;
	{
		bl::Mutex::Auto autoLock(s_lockAcceptLoop);
		auto it = s_acceptLoopSocks.find(sock);
		if (it != s_acceptLoopSocks.cend()) {
			BlSetLastError(EEXIST);
			return 0;
		}
		do {
			++s_lastAcceptLoopHandle;
			if (s_lastAcceptLoopHandle == 0)
				++s_lastAcceptLoopHandle;
		} while (s_acceptLoops.contains(s_lastAcceptLoopHandle));
		h = s_lastAcceptLoopHandle;
		s_acceptLoops.emplace(h, AcceptLoop{ sock, numConcurrentAccepts });
		s_acceptLoopSocks.emplace(sock, h);
	}

	for (size_t i = 0; i < numConcurrentAccepts; ++i) {
		auto* io = new BlTcpAccept_t;
		_BlInitTcpAccept2(io, sock, family, onAccepted, onFailure, onStopped, parm);
		_BlDoTcpAccept2(io);
	}
	return h;
}

bool BlSockStopAcceptLoop(UINT64 hLoop) {
	int sock;
	{
		bl::Mutex::Auto autoLock(s_lockAcceptLoop);
		auto it = s_acceptLoops.find(hLoop);
		if (it == s_acceptLoops.cend())
			return false;
		sock = it->second.sock;
	}
	BlCancelIo(sock, NULL);
	return true;
}
