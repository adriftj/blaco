#include <thread>
#include <set>
#include <tuple>
#include <queue>
#include "blaio.h"
#include "../../timer_priv.h"

HANDLE g_hIocp = NULL;
LPFN_ACCEPTEX g_AcceptEx = NULL;
LPFN_CONNECTEX g_ConnectEx = NULL;
LPFN_DISCONNECTEX g_DisconnectEx = NULL;

static void FatalErr(const char* sErr, const char* file, int line) {
	DWORD err = GetLastError();
	char errBuf[256];
	sprintf_s(errBuf, "%s(err=%d) in %s:%d", sErr, err, file, line);
	//TODO: add log
	throw errBuf;
}

#define FATAL_ERR(sErr) FatalErr((sErr), __FILE__, __LINE__)

static std::set<std::tuple<int, int, int>> s_canSocketSkipNotify;

static void LoadWsaFuncs() {
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	DWORD dwBytes;

	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	int ret = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx, sizeof(GuidAcceptEx), &g_AcceptEx, sizeof(g_AcceptEx), &dwBytes, NULL, NULL);
	if (ret == SOCKET_ERROR)
		FATAL_ERR("Can load AcceptEx");

	GUID GuidConnectEx = WSAID_CONNECTEX;
	ret = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidConnectEx, sizeof(GuidConnectEx), &g_ConnectEx, sizeof(g_ConnectEx), &dwBytes, NULL, NULL);
	if (ret == SOCKET_ERROR)
		FATAL_ERR("Can load ConnectEx");

	GUID GuidDisconnectEx = WSAID_DISCONNECTEX;
	ret = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidDisconnectEx, sizeof(GuidDisconnectEx), &g_DisconnectEx, sizeof(g_DisconnectEx), &dwBytes, NULL, NULL);
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

using Duration = std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

struct OnceTimer {
	TimePoint startTime;
	BlTimerCallback cb;
	void* parm;

	bool operator <(const OnceTimer& other) const {
		return startTime > other.startTime;
	}
};

struct Worker {
	size_t slot;
	std::thread thread;
	std::priority_queue<OnceTimer> onceTimers;
};
size_t s_numWorkers = 0;
static Worker* s_workers = nullptr;
BL_THREAD_LOCAL Worker* gt_worker = nullptr;


inline DWORD GetIocpTimeOut(std::priority_queue<OnceTimer>* onceTimers) {
	if (onceTimers->empty())
		return INFINITE;
	TimePoint tNow = std::chrono::system_clock::now();
	int64_t toWait;
	for (;;) {
		const OnceTimer& timer = onceTimers->top();
		toWait = std::chrono::duration_cast<Duration>(timer.startTime - tNow).count();
		if (toWait > 0)
			break;
		BlTimerCallback cb = timer.cb;
		void* parm = timer.parm;
		onceTimers->pop();
		if (cb)
			cb(parm);
		if (onceTimers->empty())
			return INFINITE;
	}
	if (toWait >= INFINITE)
		return INFINITE - 1;
	return (DWORD)toWait;
}

void _BlIoLoop(size_t slot) {
	Worker* worker = s_workers + slot;
	gt_worker = worker;
	DWORD toWait;
	int err;
	DWORD bytesXfer;
	ULONG_PTR perHandle;
	_BlAioBase* lazyio;
	for (;;) {
		lazyio = nullptr;
		err = 0;
		toWait = GetIocpTimeOut(&worker->onceTimers);
		if (!::GetQueuedCompletionStatus(g_hIocp, &bytesXfer, &perHandle, (LPOVERLAPPED*)&lazyio, toWait)) {
			err = GetLastError();
			if (err == ERROR_SEM_TIMEOUT || err == WAIT_TIMEOUT)
				continue;
			if (!lazyio) {
				if (err == ERROR_ABANDONED_WAIT_0) // iocp handle is closed
					return;
				return; // TODO: other reason?
			}
		}

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
}

SOCKET BlSockCreate(sa_family_t family, int sockType, int protocol) {
	SOCKET sock = WSASocket(family, sockType, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (sock != INVALID_SOCKET) {
		if (g_hIocp != CreateIoCompletionPort((HANDLE)sock, g_hIocp, BL_kSocket, 0)) {
			int err = BlGetLastError();
			BlSockClose(sock);
			BlSetLastError(err);
			return INVALID_SOCKET;
		}
		if (s_canSocketSkipNotify.contains({family, sockType, protocol}))
			SetFileCompletionNotificationModes((HANDLE)sock, FILE_SKIP_SET_EVENT_ON_HANDLE | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
	}
	return sock;
}

void BlIoLoop() {
	_BlIoLoop(0);
}

void BlInit(uint32_t options, size_t numIoWorkers, size_t numOtherWorkers) {
	if (!(options & BL_INIT_DONT_WSASTARTUP)) {
		WSADATA wsadata;
		if (::WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
			FATAL_ERR("Can not initialize winsock");
	}
	LoadWsaFuncs();
	EnumWsaProtocols();

	g_hIocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, g_numCpus);
	if (g_hIocp == NULL)
		FATAL_ERR("Can not create iocp");
	if (numIoWorkers == 0)
		numIoWorkers = g_numCpus;
	if (numOtherWorkers == 0)
		numOtherWorkers = g_numCpus;
	size_t numWorkers = numIoWorkers + numOtherWorkers;
	s_numWorkers = numWorkers;
	s_workers = new Worker[numWorkers];
	bool useMain = (options & BL_INIT_USE_MAIN_THREAD_AS_WORKER);
	for (size_t i = 0; i < numWorkers; ++i) {
		s_workers[i].slot = i;
		if (!useMain || i > 0)
			s_workers[i].thread = std::thread([i]() { _BlIoLoop(i); });
	}
	_BlInitTimerLoop();
}

void BlExitNotify() {
	g_exitApplication = true;
	_BlExitNotifyTimerLoop();
	CloseHandle(g_hIocp);
}

void BlWaitExited() {
	_BlWaitTimerLoopExited();
	for (size_t i = 0; i < s_numWorkers; ++i) {
		if (s_workers[i].thread.get_id() != std::thread::id())
			s_workers[i].thread.join();
	}
	delete[] s_workers;
	s_workers = nullptr;
	g_exitApplication = false;
}

bool BlAddOnceTimer(int64_t startTime, BlTimerCallback cb, void* parm) {
	Worker* worker = (Worker*)gt_worker;
	if (!worker)
		return false;
	TimePoint tNow = std::chrono::system_clock::now();
	TimePoint t = startTime <= 0 ? tNow + Duration(-startTime) : TimePoint() + Duration(startTime);
	worker->onceTimers.emplace(OnceTimer{ t, cb, parm });
	return true;
}

int BlFileOpen(const char* fileName, int flags, int mode) {
	DWORD access;
	DWORD share;
	DWORD disposition;
	DWORD attributes = 0;
	HANDLE file;
	int n, wn;
	WCHAR* pathw;

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

	n = strlen(fileName)+1;
	wn = 2 * n;
	pathw = (WCHAR*)malloc(wn);
	if (!pathw) {
		BlSetLastError(ERROR_OUTOFMEMORY);
		return -1;
	}
	n = MultiByteToWideChar(CP_UTF8, 0, fileName, n, pathw, wn);
	if (n <= 0) {
		free(pathw);
		BlSetLastError(ERROR_INVALID_PARAMETER);
		return -1;
	}
	for (wn = 0; wn < n; ++wn) { // replace all unix path splitor to windows path splitor
		if (pathw[wn] == L'/')
			pathw[wn] = L'\\';
	}
	attributes |= FILE_FLAG_OVERLAPPED;
	file = CreateFileW(pathw,
		access,
		share,
		NULL,
		disposition,
		attributes,
		NULL);
	free(pathw);
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

	if (g_hIocp != CreateIoCompletionPort(file, g_hIocp, BL_kSocket, 0)) {
		int err = BlGetLastError();
		CloseHandle(file);
		BlSetLastError(err);
		return -1;
	}

	SetFileCompletionNotificationModes(file, FILE_SKIP_SET_EVENT_ON_HANDLE | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
#pragma warning(push)
#pragma warning(disable: 4311)
	return (int)file;
#pragma warning(pop)
}
