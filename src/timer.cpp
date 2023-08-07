#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <list>
#include <set>
#include <unordered_map>
#include <atomic>
#include "blaio.h"
#include "timer_priv.h"

bool g_exitApplication = false;
size_t g_numCpus = std::thread::hardware_concurrency();

using Duration = std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

struct TimerInfo {
	uint64_t id;
	bool running; // false-waiting, true-(callback is) running
	bool deleting; // false-normal, true-is marked as deleted but the callback is running(will be deleted after the callback return)
	bool isIoTask; // true - should be scheduled in io worker threads, false - other
	BlTimerCallback cb; // cbDeleted if deleting is true, cbTask if deleting is false
	void* parm;
	Duration period;
	TimePoint startTime;
};

struct TimerQItem {
	TimePoint startTime;
	uint64_t id;

	bool operator<(const TimerQItem& r) const {
		return startTime < r.startTime || (startTime == r.startTime && id < r.id);
	}
};

static std::mutex s_timerMutex;
static BlEvent s_evWakeupTimerLoop = BL_INVALID_EVENT;
static std::unordered_map<uint64_t, TimerInfo> s_timers;
static std::set<TimerQItem> s_timerQ;
static std::atomic_uint64_t s_nextTimerId = 0;

struct TimerCallbackParm {
	BlTimerCallback cb;
	void* parm;
	uint64_t id;
};

static void OnTimerCallback(TimerCallbackParm* aparm) {
	uint64_t id = aparm->id;
	BlTimerCallback cb = aparm->cb;
	void* parm = aparm->parm;
	delete aparm;
	if (cb)
		cb(parm);
	BlTimerCallback cbDeleted = nullptr;
	bool wakeup = false;
	{
		std::lock_guard lock(s_timerMutex);
		auto it = s_timers.find(id);
		assert(it != s_timers.end() && it->second.running);
		if (it->second.deleting) {
			cbDeleted = it->second.cb;
			parm = it->second.parm;
			s_timers.erase(it);
		}
		else if (it->second.period == Duration(0)) // run-once timer will be deleted automaticly
			s_timers.erase(it);
		else {
			it->second.running = false;
			auto itQ = s_timerQ.begin();
			wakeup = (itQ == s_timerQ.end() || it->second.startTime < itQ->startTime);
			s_timerQ.emplace(it->second.startTime, id);
		}
	}
	if (wakeup)
		BlSetEvent(s_evWakeupTimerLoop);
	if (cbDeleted)
		cbDeleted(parm);
}

static void TimerLoop() {
	while (!g_exitApplication) {
		int64_t toWait = 0;
		TimePoint tNow = std::chrono::system_clock::now();
		{
			std::lock_guard lock(s_timerMutex);
			for (auto itQ = s_timerQ.begin(); itQ != s_timerQ.end(); ) {
				toWait = std::chrono::duration_cast<Duration>(itQ->startTime - tNow).count();
				if (toWait > 0)
					break;
				auto it = s_timers.find(itQ->id);
				itQ = s_timerQ.erase(itQ);
				if (it != s_timers.end()) {
					assert(!(it->second.running) && !(it->second.deleting));
					it->second.running = true;
					auto k = it->second.period.count();
					if (k > 0) { // not a run-once timer
						auto n = ((k - toWait) / k) * k;
						it->second.startTime += Duration(n);
					}

					BlPostTask((BlTimerCallback)OnTimerCallback,
						new TimerCallbackParm{ it->second.cb, it->second.parm, it->first },
						it->second.isIoTask);
				}
			}
		}
		if (toWait >= 0xffffffff)
			toWait = 0xfffffffe;
		else if (toWait <= 0)
			toWait = INFINITE;
		BlWaitEventForTime(s_evWakeupTimerLoop, (uint32_t)toWait);
	}
}

uint64_t BlAddTimer(uint64_t period, int64_t startTime, BlTimerCallback cb, void* parm, bool isIoTask) {
	if (!cb)
		return 0;
	uint64_t id;
	Duration dPeriod(period);
	TimePoint tNow = std::chrono::system_clock::now();
	TimePoint t = startTime <= 0 ? tNow + Duration(-startTime) :
		TimePoint() + Duration(startTime);
	if (period > 0 && t < tNow) {
		auto d = std::chrono::duration_cast<Duration>(tNow - t);
		auto n = ((d.count() + period) / period) * period;
		t += Duration(n);
	}
	bool wakeup = false;
	{
		std::lock_guard lock(s_timerMutex);
		auto itQ = s_timerQ.begin();
		wakeup = (itQ == s_timerQ.end() || t < itQ->startTime);
		do {
			while ((id = std::atomic_fetch_add(&s_nextTimerId, 1)) == 0)
				;
		} while (!s_timers.emplace(id,
			TimerInfo{ id, false, false, isIoTask, cb, parm, dPeriod, t }).second);
		s_timerQ.emplace(t, id);
	}
	if (wakeup)
		BlSetEvent(s_evWakeupTimerLoop);
	return id;
}

int BlDelTimer(int id, BlTimerCallback cbDeleted, void* parm) {
	std::lock_guard lock(s_timerMutex);
	auto it = s_timers.find(id);
	if (it == s_timers.end() || it->second.deleting) // id not found or deleted
		return -1;
	if (it->second.running) {
		it->second.cb = cbDeleted;
		it->second.parm = parm;
		it->second.deleting = true;
		return cbDeleted ? 1 : 0;
	}
	s_timers.erase(it);
	return 0;
}

static std::thread s_thTimer;

bool _BlInitTimerLoop() {
	s_evWakeupTimerLoop = BlCreateEvent(false);
	if (s_evWakeupTimerLoop == 0)
		return false; // TODO: add log "Can not create timerloop wakeup event";
	s_thTimer = std::thread(TimerLoop);
	return true;
}

void _BlExitNotifyTimerLoop() {
	BlSetEvent(s_evWakeupTimerLoop);
}

void _BlWaitTimerLoopExited() {
	if (s_thTimer.get_id() != std::thread::id())
		s_thTimer.join();
	BlDestroyEvent(s_evWakeupTimerLoop);
	s_evWakeupTimerLoop = BL_INVALID_EVENT;
	s_timers.clear();
	s_timerQ.clear();
}
