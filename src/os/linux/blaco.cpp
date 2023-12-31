#include <thread>
#include <set>
#include <tuple>
#include <queue>
#include <chrono>
#include <sys/resource.h>
#include <signal.h>
#include "blaio.h"
#include "../../timer_priv.h"

using Duration = std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

static unsigned s_numSqeEntries = 4096; // default

static void FatalErr(const char* sErr, const char* file, int line) {
	char errBuf[256];
	snprintf(errBuf, sizeof(errBuf), "%s(err=%d) in %s:%d", sErr, errno, file, line);
	//TODO: add log
	fprintf(stderr, "%s(err=%d) in %s:%d\n", sErr, errno, file, line);
	throw errBuf;
}

#define FATAL_ERR(sErr) FatalErr((sErr), __FILE__, __LINE__)

struct _BlPostTaskParm {
	BlTaskCallback cb;
	void* parm;
};

struct _BlPostTask_t {
	_BlAioBase base;
	size_t slot;
	_BlPostTaskParm task;
};

struct _BlRecvTask_t {
	_BlAioBase base;
	_BlPostTaskParm tasks[32];
};

struct _BlCancelIo_t {
	_BlAioBase base;
	int fd;
};

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
	io_uring ring;
	_BlAioBase* waitSqeHead;
	_BlAioBase* waitSqeTail;
	_BlRecvTask_t ioRecvTask;
	int fdPipe[2];
	size_t numFreeTasks;
	_BlPostTask_t* firstFreeTask; // use _BlPostTask_t.base.next as next link
	size_t numFreeCancels;
	_BlCancelIo_t* firstFreeCancel; // use _BlCancel_t.base.next as next link
	std::thread thread;
	std::priority_queue<OnceTimer> onceTimers;

	Worker() : waitSqeHead(nullptr), waitSqeTail(nullptr), numFreeTasks(0),
		firstFreeTask(nullptr), numFreeCancels(0), firstFreeCancel(nullptr) {}
};

Worker* s_workers = nullptr;
size_t g_numIoWorkers = 0;
size_t g_numOtherWorkers = 0;
BL_THREAD_LOCAL Worker* gt_worker = nullptr;

static void SetLimits() {
	struct rlimit rl;
	rl.rlim_cur = rl.rlim_max = 256 * 1024;
	if (0 != setrlimit(RLIMIT_NOFILE, &rl))
		FATAL_ERR("Can't set max open files");
}

static void _BlOnSqePostTask(_BlPostTask_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_write(sqe, s_workers[io->slot].fdPipe[1], &io->task, sizeof(io->task), 0);
	io_uring_sqe_set_data(sqe, io);
}

static void OnPostTaskCompleted(_BlPostTask_t* io) {
	Worker* worker = gt_worker;
	assert(worker);
	if (worker->numFreeTasks >= 1024)
		free(io);
	else {
		io->base.next = (_BlAioBase*)worker->firstFreeTask;
		worker->firstFreeTask = io;
		++worker->numFreeTasks;
	}
}

inline void _BlInitPostTask(_BlPostTask_t* io, size_t slot, BlTaskCallback cb, void* parm) {
	BlOnCompletedAio onCompleted = (BlOnCompletedAio)OnPostTaskCompleted;
	_BLAIOBASE_INIT(_BlOnSqePostTask, _BlOnCqeAio)
	io->slot = slot;
	io->task.cb = cb;
	io->task.parm = parm;
}

static void _BlOnSqeRecvTask(_BlRecvTask_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_read(sqe, gt_worker->fdPipe[0], io->tasks, sizeof(io->tasks), 0);
	io_uring_sqe_set_data(sqe, io);
}

static void _BlOnCqeRecvTask(_BlRecvTask_t* io, int r) {
	if (r < 0) {
		// TODO: add log
		return;
	}
	assert(r % sizeof(_BlPostTaskParm) == 0);
	int n = r / sizeof(_BlPostTaskParm);
	for (int i = 0; i < n; ++i) {
		_BlPostTaskParm* task = io->tasks + i;
		if(task->cb)
			task->cb(task->parm);
	}
}

inline void _BlInitRecvTask(_BlRecvTask_t* io) {
	BlOnCompletedAio onCompleted = nullptr;
	_BLAIOBASE_INIT(_BlOnSqeRecvTask, _BlOnCqeRecvTask)
}

static void _BlOnSqeCancelIo(_BlCancelIo_t* io, struct io_uring_sqe* sqe) {
	//io_uring_prep_cancel_fd(sqe, io->fd, 0); // TODO: flags should be IORING_ASYNC_CANCEL_ALL | IORING_ASYNC_CANCEL_FD ?
	io_uring_sqe_set_data(sqe, io);
}

static void OnCancelIoCompleted(_BlCancelIo_t* io) {
	Worker* worker = gt_worker;
	assert(worker);
	if (worker->numFreeCancels >= 1024)
		free(io);
	else {
		io->base.next = (_BlAioBase*)worker->firstFreeCancel;
		worker->firstFreeCancel = io;
		++worker->numFreeCancels;
	}
}

inline void _BlInitCancelIo(_BlCancelIo_t* io, int fd) {
	BlOnCompletedAio onCompleted = (BlOnCompletedAio)OnCancelIoCompleted;
	_BLAIOBASE_INIT(_BlOnSqeCancelIo, _BlOnCqeAio)
	io->fd = fd;
}

static void InitWorker(Worker& worker, size_t slot) {
	worker.slot = slot;
	int r = io_uring_queue_init(s_numSqeEntries, &worker.ring, 0);
	if (r < 0)
		FATAL_ERR("io_uring_queue_init failed");
	r = pipe2(worker.fdPipe, 0);
	if (r < 0)
		FATAL_ERR("Can't create pipe fds for worker");
}

inline int64_t GetCqeTimeOut(std::priority_queue<OnceTimer>* onceTimers) {
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
		toWait = INFINITE - 1;
	return toWait;
}

static void SubmitAndWaitCqe(struct io_uring* ring, struct io_uring_cqe** pCqe, std::priority_queue<OnceTimer>* onceTimers) {
	int r;
	int64_t toWait;
	struct __kernel_timespec ts;
	for (;;) {
		toWait = GetCqeTimeOut(onceTimers);
		io_uring_submit(ring);
		if (toWait >= INFINITE)
			r = io_uring_wait_cqe(ring, pCqe);
		else {
			ts.tv_sec = toWait / 1000;
			ts.tv_nsec = (toWait % 1000) * 1000000;
			r = io_uring_wait_cqe_timeout(ring, pCqe, &ts);
		}
		if (r >= 0)
			return;
		if (errno == ETIME)
			continue;

		int tries = 0;
		while (errno == EINTR) {
			if (toWait >= INFINITE)
				r = io_uring_wait_cqe(ring, pCqe);
			else
				r = io_uring_wait_cqe_timeout(ring, pCqe, &ts);
			if (r >= 0)
				return;
			++tries;
			if (tries > 64) {
				usleep(100000); // 100ms
				tries = 0;
			}
		}
		FATAL_ERR("io_uring_wait_cqe failed");
	}
}

static void _BlIoLoop(size_t slot) {
	Worker* worker = s_workers + slot;
	struct io_uring* ring = &worker->ring;
	struct io_uring_cqe* cqe;
	auto* onceTimers = &worker->onceTimers;
	_BlAioBase* lazyio;
	bool hasRecvTask;
	int r;

	gt_worker = worker;
	_BlInitRecvTask(&worker->ioRecvTask);
	_BlDoAio((_BlAioBase*)&worker->ioRecvTask);
	for (;;) {
		SubmitAndWaitCqe(ring, &cqe, onceTimers);
		hasRecvTask = false;
		for (;;) {
			lazyio = (_BlAioBase*)io_uring_cqe_get_data(cqe);
			assert(lazyio != nullptr);
			hasRecvTask = (lazyio == &worker->ioRecvTask.base);
			int res = cqe->res;
			io_uring_cqe_seen(ring, cqe);
			lazyio->onCqe(lazyio, res);
			r = io_uring_peek_cqe(ring, &cqe);
			if (r < 0)
				break;
		}
		if (g_exitApplication)
			break;
		if (hasRecvTask)
			_BlDoAio((_BlAioBase*)&worker->ioRecvTask);
	}

	io_uring_queue_exit(ring);
	close(worker->fdPipe[0]);
	close(worker->fdPipe[1]);
	for (_BlPostTask_t* p = worker->firstFreeTask; p; p = worker->firstFreeTask) {
		worker->firstFreeTask = (_BlPostTask_t*)p->base.next;
		free(p);
	}
	for (_BlCancelIo_t* p = worker->firstFreeCancel; p; p = worker->firstFreeCancel) {
		worker->firstFreeCancel = (_BlCancelIo_t*)p->base.next;
		free(p);
	}
	gt_worker = nullptr;
}

void BlIoLoop() {
	_BlIoLoop(0);
}

static void CheckWaitSqeAio() {
	Worker* worker = (Worker*)gt_worker;
	assert(worker);
	_BlAioBase* head;
	struct io_uring* ring = &worker->ring;
	while ((head = worker->waitSqeHead) != nullptr) {
		struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
		if (!sqe)
			return;
		worker->waitSqeHead = head->next;
		if (worker->waitSqeHead == nullptr)
			worker->waitSqeTail = nullptr;
		head->onSqe(head, sqe);
	}
}

static void BlockSignals() {
	sigset_t mask;
	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);
}

void BlInit(uint32_t options, size_t numIoWorkers, size_t numOtherWorkers) {
	SetLimits();
	BlockSignals();
	if (numIoWorkers == 0)
		numIoWorkers = g_numCpus;
	if (numOtherWorkers == 0)
		numOtherWorkers = 2*g_numCpus;
	g_numIoWorkers = numIoWorkers;
	g_numOtherWorkers = numOtherWorkers;
	size_t numWorkers = numIoWorkers + numOtherWorkers;
	s_workers = new Worker[numWorkers];
	bool useMain = (options & BL_INIT_USE_MAIN_THREAD_AS_WORKER);
	for (size_t i = 0; i < numWorkers; ++i) {
		InitWorker(s_workers[i], i);
		if (!useMain || i > 0)
			s_workers[i].thread = std::thread([i]() { _BlIoLoop(i); });
	}
	_BlInitTimerLoop();
}

static size_t s_nextIoWorker = 0;
static size_t s_nextOtherWorker = 0;

inline size_t PickupWorker(bool isIoTask) {
	size_t x;
	if (isIoTask) {
		x = s_nextIoWorker++;
		if (s_nextIoWorker >= g_numIoWorkers)
			s_nextIoWorker = 0;
		if (x >= g_numIoWorkers)
			x = 0;
	}
	else {
		x = s_nextOtherWorker++;
		if (s_nextOtherWorker >= g_numOtherWorkers)
			s_nextOtherWorker = 0;
		if (x >= g_numOtherWorkers)
			x = 0;
		x += g_numIoWorkers;
	}
	return x;
}

static void _PostTask(size_t slot, BlTaskCallback cb, void* parm) {
	Worker* worker = gt_worker;
	if (worker) { // inside _BlIoLoop(thread pool)
		_BlPostTask_t* ioTask = worker->firstFreeTask;
		if (ioTask) {
			--worker->numFreeTasks;
			worker->firstFreeTask = (_BlPostTask_t*)ioTask->base.next;
			ioTask->slot = slot;
			ioTask->task.cb = cb;
			ioTask->task.parm = parm;
		}
		else {
			ioTask = (_BlPostTask_t*)malloc(sizeof(_BlPostTask_t));
			if (!ioTask) {
				assert(false);
				// TODO: add log
				return;
			}
			_BlInitPostTask(ioTask, slot, cb, parm);
		}
		ioTask->base.coro = ioTask;
		_BlDoAio((_BlAioBase*)ioTask);
	}
	else {
		_BlPostTaskParm task;
		task.cb = cb;
		task.parm = parm;
		int r = write(s_workers[slot].fdPipe[1], &task, sizeof(task));
		if (r < 0) {
			// TODO: add log
			fprintf(stderr, "write pipe failed, err=%d\n", errno);
		}
	}
}

void BlPostTask(BlTaskCallback cb, void* parm, bool isIoTask) {
	size_t slot = PickupWorker(isIoTask);
	_PostTask(slot, cb, parm);
}

inline void CancelIoInWorker(Worker* worker, int fd) {
	_BlCancelIo_t* ioCancel = worker->firstFreeCancel;
	if (ioCancel) {
		--worker->numFreeCancels;
		worker->firstFreeCancel = (_BlCancelIo_t*)ioCancel->base.next;
		ioCancel->fd = fd;
	}
	else {
		ioCancel = (_BlCancelIo_t*)malloc(sizeof(_BlCancelIo_t));
		if (!ioCancel) {
			assert(false);
			// TODO: add log
			return;
		}
		_BlInitCancelIo(ioCancel, fd);
	}
	ioCancel->base.coro = ioCancel;
	_BlDoAio((_BlAioBase*)ioCancel);
}

static void OnCancelIoPosted(void* parm) {
	//CancelIoInWorker(gt_worker, (int)parm);
}

int BlCancelIo(int fd) {
	assert(false); // need io_uring/linux kernel new version
	Worker* worker = gt_worker;
	if (worker) // inside _BlIoLoop(thread pool)
		CancelIoInWorker(worker, fd);
	else {
		_BlPostTaskParm task;
		task.cb = OnCancelIoPosted;
		//task.parm = (void*)fd;
		size_t slot = PickupWorker(true);
		int r = write(s_workers[slot].fdPipe[1], &task, sizeof(task));
		if (r < 0) {
			// TODO: add log
			return -errno;
		}
	}
	return 0;
}

void BlExitNotify() {
	g_exitApplication = true;
	for (size_t i = 0; i < g_numIoWorkers + g_numOtherWorkers; ++i)
		_PostTask(i, nullptr, nullptr);
	_BlExitNotifyTimerLoop();
}

void BlWaitExited() {
	_BlWaitTimerLoopExited();
	for (size_t i = 0; i < g_numIoWorkers + g_numOtherWorkers; ++i) {
		if (s_workers[i].thread.get_id() != std::thread::id())
			s_workers[i].thread.join();
	}
	delete[] s_workers;
	s_workers = nullptr;
	g_exitApplication = false;
}

bool _BlDoAio(_BlAioBase* io) {
	Worker* worker = (Worker*)gt_worker;
	if (!worker) {
		io->ret = -ENOTSUP;
		// TODO: add log
		assert(false);
		return false;
	}
	if (!worker->waitSqeTail) {
		struct io_uring* ring = &worker->ring;
		struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
		if (sqe)
			io->onSqe(io, sqe);
		else {
			io->next = nullptr;
			worker->waitSqeHead = worker->waitSqeTail = io;
		}
	}
	else {
		io->next = nullptr;
		worker->waitSqeTail->next = io;
		worker->waitSqeTail = io;
	}
	return true;
}

inline void CheckWaitSqeAndCompleteIo(_BlAioBase* io) {
	CheckWaitSqeAio();
	if (io->onCompleted)
		io->onCompleted(io->coro);
}

void _BlOnCqeAio(_BlAioBase* io, int res) {
	io->ret = res;
	CheckWaitSqeAndCompleteIo(io);
}

void _BlOnSqeTcpAccept(BlTcpAccept_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_accept(sqe, io->listenSock, io->peer, io->peerLen, 0);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeTcpConnect(BlTcpConnect_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_connect(sqe, io->sock, io->addr, BlGetSockAddrLen(io->addr));
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeSockSend(BlSockSend_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_send(sqe, io->sock, io->buf, io->len, io->flags);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeSockSendXMsg(_BlSockSendRecvMsg* io, struct io_uring_sqe* sqe) {
	io_uring_prep_sendmsg(sqe, io->sock, &io->msghdr, io->flags);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeSockMustSend(BlSockMustSend_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_send(sqe, io->sock, io->buf, io->len, io->flags);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnCqeSockMustSend(BlSockMustSend_t* io, int r) {
	if (r > 0) {
		if (r == (int)io->len)
			io->base.ret = 0;
		else {
			io->len -= r;
			io->buf = ((char*)io->buf)+r;
			if (_BlDoAio((_BlAioBase*)io))
				return;
			assert(false); // TODO: add log
		}
	}
	else if (r == 0)
		io->base.ret = -E_PEER_CLOSED;
	else
		io->base.ret = r;
	CheckWaitSqeAndCompleteIo(&io->base);
}

void _BlOnCqeSockMustSendVec(BlSockMustSendVec_t* io, int r) {
	if (r > 0) {
		IoVecAdjustAfterIo(&io->msghdr.msg_iov, &io->msghdr.msg_iovlen, r);
		if (io->msghdr.msg_iovlen <= 0)
			io->base.ret = 0;
		else {
			if (_BlDoAio((_BlAioBase*)io))
				return;
			assert(false); // TODO: add log
		}
	}
	else if (r == 0)
		io->base.ret = -E_PEER_CLOSED;
	else
		io->base.ret = r;
	CheckWaitSqeAndCompleteIo(&io->base);
}


void _BlOnSqeSockRecv(BlSockRecv_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_recv(sqe, io->sock, io->buf, io->len, io->flags);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeSockRecvXMsg(_BlSockSendRecvMsg* io, struct io_uring_sqe* sqe) {
	io_uring_prep_recvmsg(sqe, io->sock, &io->msghdr, io->flags);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeSockMustRecv(BlSockMustRecv_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_recv(sqe, io->sock, io->buf, io->len, io->flags);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnCqeSockRecvFrom(BlSockRecvFrom_t* io, int r) {
	io->base.ret = r;
	if (io->addrLen)
		*io->addrLen = io->msghdr.msg_namelen;
	CheckWaitSqeAndCompleteIo(&io->base);
}

void _BlOnCqeSockRecvVecFrom(BlSockRecvVecFrom_t* io, int r) {
	io->base.ret = r;
	if (io->addrLen)
		*io->addrLen = io->msghdr.msg_namelen;
	CheckWaitSqeAndCompleteIo(&io->base);
}

void _BlOnCqeSockMustRecv(BlSockMustRecv_t* io, int r) {
	if (r > 0) {
		if (r == (int)io->len)
			io->base.ret = 0;
		else {
			io->len -= r;
			io->buf = ((char*)io->buf)+r;
			if (_BlDoAio((_BlAioBase*)io))
				return;
			assert(false); // TODO: add log
		}
	}
	else if (r == 0)
		io->base.ret = -E_PEER_CLOSED;
	else
		io->base.ret = r;
	CheckWaitSqeAndCompleteIo(&io->base);
}

void _BlOnCqeSockMustRecvVec(BlSockMustRecvVec_t* io, int r) {
	if (r > 0) {
		IoVecAdjustAfterIo(&io->msghdr.msg_iov, &io->msghdr.msg_iovlen, r);
		if (io->msghdr.msg_iovlen <= 0)
			io->base.ret = 0;
		else {
			if (_BlDoAio((_BlAioBase*)io))
				return;
			assert(false); // TODO: add log
		}
	}
	else if (r == 0)
		io->base.ret = -E_PEER_CLOSED;
	else
		io->base.ret = r;
	CheckWaitSqeAndCompleteIo(&io->base);
}


void _BlOnSqeTcpClose(BlTcpClose_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_close(sqe, io->sock);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeTcpShutdown(BlTcpShutdown_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_shutdown(sqe, io->sock, io->how);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeFileRead(BlFileRead_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_read(sqe, io->f, io->buf, io->bufLen, io->offset);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeFileReadVec(BlFileReadVec_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_readv(sqe, io->f, io->bufs, io->bufCnt, io->offset);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeFileWrite(BlFileWrite_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_write(sqe, io->f, io->buf, io->bufLen, io->offset);
	io_uring_sqe_set_data(sqe, io);
}

void _BlOnSqeFileWriteVec(BlFileWriteVec_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_writev(sqe, io->f, io->bufs, io->bufCnt, io->offset);
	io_uring_sqe_set_data(sqe, io);
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
