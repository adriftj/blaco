#include "blaio.h"

#if 0

struct tagBlFileContentWatcher {
	_BlAioBase base;
	HANDLE hDir;
	char buf[4096];
	DWORD nXfer;
};

static HANDLE OpenDir(const char* fileName) {
	int n = strlen(dir) + 1;
	int wn = 2 * n;
	WCHAR* pathw = (WCHAR*)malloc(wn);
	if (!pathw) {
		BlSetLastError(ERROR_OUTOFMEMORY);
		return INVALID_HANDLE_VALUE;
	}
	n = MultiByteToWideChar(CP_UTF8, 0, dir, n, pathw, wn);
	if (n <= 0) {
		free(pathw);
		BlSetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}
	for (wn = 0; wn < n; ++wn) { // replace all unix path splitor to windows path splitor
		if (pathw[wn] == L'/')
			pathw[wn] = L'\\';
	}
	HANDLE h = CreateFileW(pathw,
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL);
	free(pathw);
	if (h != INVALID_HANDLE_VALUE) {
		if (g_hIocp != CreateIoCompletionPort(h, g_hIocp, BL_kSocket, 0)) {
			int err = BlGetLastError();
			CloseHandle(h);
			BlSetLastError(err);
			return INVALID_HANDLE_VALUE;
		}
	}

	return h;
}

static void OnReadDirNotify(BlFileContentWatcher* watcher) {

}

BlFileContentWatcher* BlFileContentWatcherOpen(const char* fileName, int delay, BlFileContentWatcherCallback cb, void* parm) {
	HANDLE h = OpenDir(fileName);
	if (h == INVALID_HANDLE_VALUE)
		return nullptr;

	BlFileContentWatcher* watcher = (BlFileContentWatcher*)malloc(sizeof(BlFileContentWatcher));
	memset(&watcher->base.ov, 0, sizeof(OVERLAPPED));
	watcher->base.internalOnCompleted = (_BlInternalOnCompleted)OnReadDirNotify;
	watcher->base.onCompleted = (BlOnCompletedAio)cb;
	watcher->base.coro = parm;
	watcher->hDir = h;

	BOOL b = ReadDirectoryChangesW(h, watcher->buf, sizeof(watcher->buf), FALSE,
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
		&watcher->nXfer, &watcher->base.ov, NULL);
}

void BlFileContentWatcherClose(BlFileContentWatcher* watcher);

#endif
