#pragma once

using Duration = std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

size_t g_numCpus = std::thread::hardware_concurrency();

static bool s_exitApplication = false;
static size_t s_numWorkers = 0;

struct TimerInfo {
	uint64_t id;
	Duration period;
	TimePoint startTime;
	BlTaskCallback cb;
	void* parm;
};

struct CmpTimerInfo {
	bool operator()(const TimerInfo* a, const TimerInfo* b) const {
		return a->startTime < b->startTime
			|| (a->startTime == b->startTime && a->id < b->id);
	}
};

struct OnceTimer {
	TimePoint startTime;
	BlTaskCallback cb;
	void* parm;

	bool operator <(const OnceTimer& other) const {
		return startTime > other.startTime;
	}
};

typedef std::unordered_map<uint64_t, TimerInfo*> TimerMap;
typedef std::set<TimerInfo*, CmpTimerInfo> TimerQueue;

struct TimerMgr {
	size_t slot;
	std::thread thread;
	std::priority_queue<OnceTimer> onceTimerQ;
	TimerMap timerMap;
	TimerQueue timerQ;
	uint64_t nextTimerId;
};

struct Worker;
static Worker* s_workers = nullptr;
BL_THREAD_LOCAL Worker* st_worker = nullptr;

inline uint32_t GetWaitTimeOnce(TimePoint tNow, std::priority_queue<OnceTimer>* onceTimerQ) {
	if (onceTimerQ->empty())
		return INFINITE;
	int64_t toWait;
	for (;;) {
		const OnceTimer& timer = onceTimerQ->top();
		toWait = std::chrono::duration_cast<Duration>(timer.startTime - tNow).count();
		if (toWait > 0)
			break;
		BlTaskCallback cb = timer.cb;
		void* parm = timer.parm;
		onceTimerQ->pop();
		if (cb)
			cb(parm);
		if (onceTimerQ->empty())
			return INFINITE;
	}
	if (toWait >= INFINITE)
		return INFINITE - 1;
	return (uint32_t)toWait;
}

inline uint32_t GetWaitTimePeriodic(TimePoint tNow, TimerMap* timerMap, TimerQueue* timerQ) {
	int64_t toWait;
	for (auto it = timerQ->begin(); it != timerQ->end(); it = timerQ->begin()) {
		TimerInfo* ti = *it;
		toWait = std::chrono::duration_cast<Duration>(ti->startTime - tNow).count();
		if (toWait > 0) {
			if (toWait >= INFINITE)
				return INFINITE - 1;
			return (uint32_t)toWait;
		}
		BlTaskCallback cb = ti->cb;
		timerQ->erase(it);
		if (cb)
			cb(ti->parm);
		if (ti->period == Duration(0)) { // run once
			timerMap->erase(ti->id);
			free(ti);
		}
		else {
			ti->startTime += ti->period;
			timerQ->insert(ti);
		}
	}
	return INFINITE;
}

inline uint32_t GetWaitTime(std::priority_queue<OnceTimer>* onceTimerQ, TimerMap* timerMap, TimerQueue* timerQ) {
	TimePoint tNow = std::chrono::system_clock::now();
	uint32_t tWait1 = GetWaitTimeOnce(tNow, onceTimerQ);
	uint32_t tWait2 = GetWaitTimePeriodic(tNow, timerMap, timerQ);
	return tWait1 <= tWait2 ? tWait1 : tWait2;
}
