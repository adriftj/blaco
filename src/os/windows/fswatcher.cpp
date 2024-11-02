#include <unordered_map>
#include <string>
#include <chrono>
#include "blaio.h"
#include <shlwapi.h>

using Duration = std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

struct FileWatcherInfo {
	uint64_t timerId;
	BlFileContentWatcher* watcher;
	std::wstring path;
};

struct tagBlFileContentWatcher {
	BlAioBase base;
	uint64_t refCnt;
	int delay;
	HANDLE hDir;
	std::wstring dir;
	char buf[4096];
	int32_t toClose;
	DWORD nXfer;
	std::unordered_map<std::wstring, FileWatcherInfo> watchedFiles;
};

inline void AddRefWatcher(BlFileContentWatcher* watcher) {
	BlAtomicInc64((int64_t volatile*)&watcher->refCnt);
}

inline void ReleaseWatcher(BlFileContentWatcher* watcher) {
	if (BlAtomicDec64((int64_t volatile*)&watcher->refCnt) == 0)
		delete watcher;
}

static HANDLE OpenDir(const WCHAR* wDir) {
	HANDLE h = CreateFileW(wDir,
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL);
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

static void OnFileContentChanged(void* parm) {
	FileWatcherInfo* fwi = (FileWatcherInfo*)parm;
	BlFileContentWatcher* watcher = fwi->watcher;
	if(watcher->base.onCompleted)
		((BlFileContentWatcherCallback)watcher->base.onCompleted)(fwi->path.c_str(),
			watcher->dir.size(), watcher->base.coro);
	ReleaseWatcher(watcher);
}

static void OnReadDirNotify(BlFileContentWatcher* watcher, int err, DWORD nXfer) {
	if (watcher->toClose) {
		ReleaseWatcher(watcher);
		return;
	}

	if (err) {
		// TODO: add log
	}
	else {
		FILE_NOTIFY_INFORMATION* fni;
		int offset = watcher->dir.size();
		char* p = watcher->buf;
		for(;;) {
			fni = (FILE_NOTIFY_INFORMATION*)p;
			std::wstring wFileRelPath(fni->FileName, fni->FileNameLength / sizeof(wchar_t));
			// watcher->dir should end with a trailing backslash.
			std::wstring wFilePath = watcher->dir + wFileRelPath;

			// If it could be a short filename, expand it.
			LPCWSTR wFileName = PathFindFileNameW(wFilePath.c_str());
			const int len = lstrlenW(wFileName);
			// The maximum length of an 8.3 filename is twelve, including the dot.
			if (len <= 12 && wcschr(wFileName, L'~')) {
				// Convert to the long filename form. Unfortunately, this
				// does not work for deletions, so it's an imperfect fix.
				wchar_t wbuf[MAX_PATH];
				if (::GetLongPathNameW(wFilePath.c_str(), wbuf, _countof(wbuf)) > 0)
					wFilePath = wbuf;
				if (!wFilePath.starts_with(watcher->dir)) {
					if (!fni->NextEntryOffset)
						break;
					p += fni->NextEntryOffset;
					continue;
				}
				wFileRelPath = wFilePath.substr(offset);
			}

			auto it = watcher->watchedFiles.find(wFileRelPath);
			if (it != watcher->watchedFiles.end()) {
				int64_t delay = watcher->delay;
				if (watcher->delay <= 0) {
					if (watcher->base.onCompleted)
						((BlFileContentWatcherCallback)watcher->base.onCompleted)(wFilePath.c_str(),
							offset, watcher->base.coro);
				}
				else if (it->second.timerId == 0) {
					AddRefWatcher(watcher);
					uint64_t id = BlAddTimer(0, -delay, OnFileContentChanged, 0, &it->second);
					it->second.timerId = id;
					if (id == 0) {
						// TODO: add log
						ReleaseWatcher(watcher);
					}
				}
				else
					BlChangeTimer(it->second.timerId, 0, -delay, nullptr, nullptr);
			}
			if(!fni->NextEntryOffset)
				break;
			p += fni->NextEntryOffset;
		}
	}
	BOOL b = ReadDirectoryChangesW(watcher->hDir, watcher->buf, sizeof(watcher->buf), FALSE,
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
		&watcher->nXfer, &watcher->base.ov, NULL);
	if (!b) {
		err = GetLastError();
		if (err != ERROR_IO_PENDING) {
			// TODO: add log
			ReleaseWatcher(watcher);
		}
	}
}

static WCHAR* GetAbsPath(const char* dir) {
	WCHAR* wDir = _BlUtf8PathToWCHAR(dir, -1, nullptr);
	if (!wDir)
		return nullptr;
	WCHAR* path = (WCHAR*)malloc(sizeof(WCHAR) * (MAX_PATH+1));
	if (!path) {
		free(wDir);
		return nullptr;
	}

	DWORD n = GetFullPathNameW(wDir, MAX_PATH, path, nullptr);
	if (n > MAX_PATH) {
		free(path);
		path = (WCHAR*)malloc((n+1) * sizeof(WCHAR));
		if (!path) {
			free(wDir);
			return nullptr;
		}
		n = GetFullPathNameW(wDir, n, path, nullptr);
	}
	if (n == 0)	{
		free(path);
		free(wDir);
		return nullptr;
	}
	free(wDir);
	if (path[n - 1] != L'\\') {
		path[n] = L'\\'; 
		path[n + 1] = 0;
	}
	return path;
}

BlFileContentWatcher* BlFileContentWatcherOpen(const char* dir, const char** fileNames,
		int nFileNames, int delay, BlFileContentWatcherCallback cb, void* parm) {
	WCHAR* wdir = GetAbsPath(dir);
	if (!wdir)
		return nullptr;
	std::wstring wstrDir = wdir;
	free(wdir);

	HANDLE h = OpenDir(wstrDir.c_str());
	if (h == INVALID_HANDLE_VALUE)
		return nullptr;

	auto watcher = new BlFileContentWatcher;
	for (int i = 0; i < nFileNames; ++i) {
		WCHAR* fnamew = _BlUtf8PathToWCHAR(fileNames[i], -1, NULL);
		if (!fnamew) {
			CloseHandle(h);
			delete watcher;
			return nullptr;
		}
		std::wstring filePath = wstrDir + fnamew;
		watcher->watchedFiles.emplace(std::wstring(fnamew), FileWatcherInfo{ 0, watcher, filePath });
		free(fnamew);
	}

	memset(&watcher->base.ov, 0, sizeof(OVERLAPPED));
	watcher->base.internalOnCompleted = (_BlInternalOnCompleted)OnReadDirNotify;
	watcher->base.onCompleted = (BlOnCompletedAio)cb;
	watcher->base.coro = parm;
	watcher->hDir = h;
	watcher->dir.swap(wstrDir);
	watcher->delay = delay;
	watcher->refCnt = 2; // Another one is for ReadDirectoryChangesW
	watcher->toClose = 0;

	BOOL b = ReadDirectoryChangesW(h, watcher->buf, sizeof(watcher->buf), FALSE,
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
		&watcher->nXfer, &watcher->base.ov, NULL);
	if (!b && GetLastError() != ERROR_IO_PENDING) {
		delete watcher;
		return nullptr;
	}
	return watcher;
}

void BlFileContentWatcherClose(BlFileContentWatcher* watcher) {
	BlStore32(&watcher->toClose, 1);
	ReleaseWatcher(watcher);
}
