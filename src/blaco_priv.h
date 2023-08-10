void BlIoLoop() {
	_BlIoLoop(0);
}

static void OnTimerArrived(void* p) {
	TimerInfo* ti = (TimerInfo*)p;
	BlTaskCallback cb = ti->cb;
	if (!cb)
		return;
	Worker* worker = st_worker;
	assert(worker != nullptr);
	if (cb == TimerToBeDelete) {
		worker->timers.erase(ti->id);
		free(ti);
	}
	else {
		cb(ti->parm);
		if (ti->period == Duration(0)) { // run once
			worker->timers.erase(ti->id);
			free(ti);
		}
		else {
			ti->startTime += ti->period;
			worker->timerQ.emplace(BTimer{ ti->startTime, OnTimerArrived, ti });
		}
	}
}

static void OnAddTimer(TimerInfo* ti) {
	Worker* worker = (Worker*)st_worker;
	assert(worker != nullptr);
	bool ok = worker->timers.insert(std::make_pair(ti->id, ti)).second;
	assert(ok);
	worker->timerQ.emplace(BTimer{ ti->startTime, OnTimerArrived, ti });
}

uint64_t BlAddTimer(uint64_t period, int64_t startTime, BlTaskCallback cb, void* parm, bool isIoTask) {
	TimerInfo* ti = (TimerInfo*)malloc(sizeof(TimerInfo));
	if (!ti)
		return 0;
	size_t slot = PickupWorker(isIoTask);
	uint64_t id = (uint64_t)BlAddFetch64((volatile int64_t*)&s_workers[slot].nextTimerId, 1);
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
	return (slot << 52) | (id & ( (((uint64_t)1) << 52) - 1) );
}

static int DeleteTimerInWorker(Worker* worker, uint64_t id) {
	auto& timers = worker->timers;
	auto it = timers.find(id);
	if (it == timers.end())
		return -1;
	it->second->cb = TimerToBeDelete;
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
	size_t slot = (id >> 52);
	id &= (((uint64_t)1) << 52) - 1;
	if (slot >= s_numWorkers)
		return -1;
	Worker* worker = (Worker*)st_worker;
	if (worker && worker->slot == slot)
		return DeleteTimerInWorker(worker, id);
	DelTimerInfo* d = (DelTimerInfo*)malloc(sizeof(DelTimerInfo));
	d->id = id;
	d->cb = cbDeleted;
	d->parm = parm;
	if (!_BlPostTaskToWorker(slot, (BlTaskCallback)OnDelTimer, d)) {
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
	worker->timerQ.emplace(BTimer{ t, cb, parm });
	return true;
}

void _BlWaitExited() {
	for (size_t i = 0; i < s_numWorkers; ++i) {
		if (s_workers[i].thread.get_id() != std::thread::id())
			s_workers[i].thread.join();
		for (auto& it : s_workers[i].timers)
			free(it.second);
	}
	delete[] s_workers;
	s_workers = nullptr;
	s_exitApplication = false;
	s_numWorkers = 0;
}
