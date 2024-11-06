#pragma once

#include "blconfig.h"

#if defined(WIN32)
	typedef CRITICAL_SECTION BlMutex_t;
#elif defined(__linux__)
	#include <pthread.h>
	typedef pthread_mutex_t BlMutex_t;
#else
	#error "Unsupported OS"
#endif

INLINE void BlMutexInit(BlMutex_t* lock) {
#if defined(WIN32)
	InitializeCriticalSection(lock);
#elif defined(__linux__)
	pthread_mutex_init(lock, NULL);
#endif
}

INLINE void BlMutexDestroy(BlMutex_t* lock) {
#if defined(WIN32)
	DeleteCriticalSection(lock);
#else
	(void)lock;
#endif
}

INLINE void BlLock(BlMutex_t* lock) {
#if defined(WIN32)
	EnterCriticalSection(lock);
#elif defined(__linux__)
	pthread_mutex_lock(lock);
#endif
}

INLINE void BlUnlock(BlMutex_t* lock) {
#if defined(WIN32)
	LeaveCriticalSection(lock);
#elif defined(__linux__)
	pthread_mutex_unlock(lock);
#endif
}

#ifdef __cplusplus
namespace bl {

class Mutex {
	BlMutex_t lock_;

private:
	Mutex(const Mutex&) = delete;
	Mutex& operator=(const Mutex&) = delete;

public:
	explicit Mutex()	{ BlMutexInit(&lock_); }
	~Mutex() { BlMutexDestroy(&lock_); }
	void Lock() { BlLock(&lock_); }
	void Unlock() { BlUnlock(&lock_); }

public:
	class Auto {
	private:
		Auto(const Auto&) = delete;
		Auto& operator=(const Auto&) = delete;

		Mutex& lock_;

	public:
		explicit Auto(Mutex& lock) : lock_(lock) { lock_.Lock(); }
		~Auto() { lock_.Unlock(); }
	};
};

} // end of namespace bl
#endif /* __cplusplus */
