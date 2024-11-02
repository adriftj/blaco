#include <thread>
#include <set>
#include <unordered_map>
#include <tuple>
#include <queue>
#include "blaio.h"

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

#include "../../timer_priv.h"

struct Worker : public TimerMgr {
};

static HANDLE s_initSema = NULL;
static size_t s_nextWorker = 0;

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
		if (!::GetQueuedCompletionStatusEx(g_hIocp, &ovEntry, 1, &nEntries, toWait, TRUE)) {
			err = GetLastError();
			if (err == ERROR_SEM_TIMEOUT || err == WAIT_TIMEOUT)
				continue;
			if (err == ERROR_ABANDONED_WAIT_0 || err == ERROR_INVALID_HANDLE) // iocp handle is closed
				return;
			if (nEntries == 1 && !ovEntry.lpOverlapped) {
				return; // TODO: other reason?
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

void BlInit(uint32_t options, size_t numIoWorkers, size_t numOtherWorkers) {
	WSADATA wsadata;
	if (::WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
		FATAL_ERR("Can not initialize winsock");
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
	CloseHandle(g_hIocp);
}

void BlWaitExited() {
	_BlWaitExited();
	WSACleanup();
	g_hIocp = NULL;
}

int BlFileOpen(const char* fileName, int flags, int mode) {
	WCHAR* pathw = _BlUtf8PathToWCHAR(fileName, -1, NULL);
	if (!pathw) {
		SetLastError(ERROR_INVALID_PARAMETER);
#pragma warning(push)
#pragma warning(disable: 4311)
		return (int)INVALID_HANDLE_VALUE;
#pragma warning(pop)
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
