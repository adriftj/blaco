void BlIoLoop() {
	_BlIoLoop(0);
}

inline size_t slot2id(size_t slot, size_t id) {
	return (slot << 52) | (id & ((((uint64_t)1) << 52) - 1));
}

inline size_t id2slot(size_t& id) {
	size_t slot = (id >> 52);
	id &= (((uint64_t)1) << 52) - 1;
	return slot;
}

static void OnAddTimer(TimerInfo* ti) {
	Worker* worker = (Worker*)st_worker;
	assert(worker != nullptr);
	bool ok = worker->timerMap.emplace(ti->id, ti).second;
	assert(ok);
	worker->timerQ.insert(ti);
}

uint64_t BlAddTimer(uint64_t period, int64_t startTime, BlTaskCallback cb, void* parm, bool isIoTask) {
	TimerInfo* ti = (TimerInfo*)malloc(sizeof(TimerInfo));
	if (!ti)
		return 0;
	size_t slot = PickupWorker(isIoTask);
	uint64_t id = (uint64_t)BlAtomicInc64((volatile int64_t*)&s_workers[slot].nextTimerId);
	Duration dPeriod(period);
	TimePoint tNow = std::chrono::system_clock::now();
	TimePoint t = startTime <= 0 ? tNow + Duration(-startTime) :
		TimePoint() + Duration(startTime);
	if (period > 0 && t < tNow) {
		auto d = std::chrono::duration_cast<Duration>(tNow - t);
		auto n = ((d.count() + period) / period) * period;
		t += Duration(n);
	}
	ti->id = id;
	ti->period = dPeriod;
	ti->startTime = t;
	ti->cb = cb;
	ti->parm = parm;
	if (!_BlPostTaskToWorker(slot, (BlTaskCallback)OnAddTimer, ti)) {
		free(ti);
		return 0;
	}
	return slot2id(slot, id);
}

static int DeleteTimerInWorker(Worker* worker, uint64_t id) {
	auto& timers = worker->timerMap;
	auto it = timers.find(id);
	if (it == timers.end())
		return -1;
	worker->timerQ.erase(it->second);
	timers.erase(it);
	return 0;
}

struct DelTimerInfo {
	uint64_t id;
	BlTaskCallback cb;
	void* parm;
};

static void OnDelTimer(DelTimerInfo* d) {
	Worker* worker = (Worker*)st_worker;
	assert(worker != nullptr);
	DeleteTimerInWorker(worker, d->id);
	if (d->cb)
		d->cb(d->parm);
	free(d);
}

int BlDelTimer(uint64_t id, BlTaskCallback cbDeleted, void* parm) {
	size_t slot = id2slot(id);
	if (slot >= s_numWorkers)
		return -1;
	Worker* worker = (Worker*)st_worker;
	if (worker && worker->slot == slot)
		return DeleteTimerInWorker(worker, id);
	DelTimerInfo* d = (DelTimerInfo*)malloc(sizeof(DelTimerInfo));
	if (!d)
		return -1;
	d->id = id;
	d->cb = cbDeleted;
	d->parm = parm;
	if (!_BlPostTaskToWorker(slot, (BlTaskCallback)OnDelTimer, d)) {
		free(d);
		return -1;
	}
	return 1;
}

struct ChangeTimerInfo {
	uint64_t id;
	uint64_t period;
	int64_t startTime;
	BlTaskCallback cb;
	void* parm;
};

static int ChangeTimerInWorker(Worker* worker, uint64_t id, uint64_t period, int64_t startTime) {
	auto& timers = worker->timerMap;
	auto it = timers.find(id);
	if (it == timers.end())
		return -1;
	TimerInfo* ti = it->second;
	worker->timerQ.erase(ti);
	Duration dPeriod(period);
	TimePoint tNow = std::chrono::system_clock::now();
	TimePoint t = startTime <= 0 ? tNow + Duration(-startTime) :
		TimePoint() + Duration(startTime);
	if (period > 0 && t < tNow) {
		auto d = std::chrono::duration_cast<Duration>(tNow - t);
		auto n = ((d.count() + period) / period) * period;
		t += Duration(n);
	}
	ti->period = dPeriod;
	ti->startTime = t;
	worker->timerQ.insert(ti);
	return 0;
}

static void OnChangeTimer(ChangeTimerInfo* d) {
	Worker* worker = (Worker*)st_worker;
	assert(worker != nullptr);
	ChangeTimerInWorker(worker, d->id, d->period, d->startTime);
	if (d->cb)
		d->cb(d->parm);
	free(d);
}

int BlChangeTimer(uint64_t id, uint64_t period, int64_t startTime, BlTaskCallback cbChanged, void* parm) {
	size_t slot = id2slot(id);
	if (slot >= s_numWorkers)
		return -1;
	Worker* worker = (Worker*)st_worker;
	if (worker && worker->slot == slot)
		return ChangeTimerInWorker(worker, id, period, startTime);
	ChangeTimerInfo* d = (ChangeTimerInfo*)malloc(sizeof(ChangeTimerInfo));
	d->id = id;
	d->cb = cbChanged;
	d->parm = parm;
	if (!_BlPostTaskToWorker(slot, (BlTaskCallback)OnChangeTimer, d)) {
		free(d);
		return -1;
	}
	return 1;
}

bool BlAddOnceTimer(int64_t startTime, BlTaskCallback cb, void* parm) {
	Worker* worker = (Worker*)st_worker;
	if (!worker)
		return false;
	TimePoint tNow = std::chrono::system_clock::now();
	TimePoint t = startTime <= 0 ? tNow + Duration(-startTime) : TimePoint() + Duration(startTime);
	worker->onceTimerQ.emplace(OnceTimer{ t, cb, parm });
	return true;
}

inline void _BlWaitExited() {
	for (size_t i = 0; i < s_numWorkers; ++i) {
		if (s_workers[i].thread.get_id() != std::thread::id())
			s_workers[i].thread.join();
		for (auto& it : s_workers[i].timerMap)
			free(it.second);
	}
	delete[] s_workers;
	s_workers = nullptr;
	s_exitApplication = false;
	s_numWorkers = 0;
}
