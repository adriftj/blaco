#pragma once

using Duration = std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

size_t g_numCpus = std::thread::hardware_concurrency();

static const BlTaskCallback TimerToBeDelete = (BlTaskCallback)1;

static bool s_exitApplication = false;
static size_t s_numWorkers = 0;

struct TimerInfo {
	uint64_t id;
	Duration period;
	TimePoint startTime;
	BlTaskCallback cb;
	void* parm;
};

struct BTimer {
	TimePoint startTime;
	BlTaskCallback cb;
	void* parm;

	bool operator <(const BTimer& other) const {
		return startTime > other.startTime;
	}
};

struct Worker;
static Worker* s_workers = nullptr;
BL_THREAD_LOCAL Worker* st_worker = nullptr;

inline uint32_t GetWaitTime(std::priority_queue<BTimer>* timerQ) {
	if (timerQ->empty())
		return INFINITE;
	TimePoint tNow = std::chrono::system_clock::now();
	int64_t toWait;
	for (;;) {
		const BTimer& timer = timerQ->top();
		toWait = std::chrono::duration_cast<Duration>(timer.startTime - tNow).count();
		if (toWait > 0)
			break;
		BlTaskCallback cb = timer.cb;
		void* parm = timer.parm;
		timerQ->pop();
		if (cb)
			cb(parm);
		if (timerQ->empty())
			return INFINITE;
	}
	if (toWait >= INFINITE)
		return INFINITE - 1;
	return (uint32_t)toWait;
}
