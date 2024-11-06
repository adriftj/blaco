#include <thread>
#include <set>
#include <unordered_map>
#include <tuple>
#include <queue>
#include <map>
#include <chrono>
#include <sys/resource.h>
#include <signal.h>
#include <semaphore.h>
#include "utility/defer.hpp"
#include "oslock.h"
#include "blaio.h"
#include "minicoro.h"
#include "../../timer_priv.h"

// TODO: {{ temporary solution: copy from liburing2.8
#define IORING_SETUP_COOP_TASKRUN	(1U << 8)
#define IORING_SETUP_SINGLE_ISSUER	(1U << 12)
#define IORING_SETUP_DEFER_TASKRUN	(1U << 13)

#define IORING_ASYNC_CANCEL_ALL	(1U << 0)
#define IORING_ASYNC_CANCEL_FD	(1U << 1)
static inline void io_uring_prep_cancel_fd(struct io_uring_sqe* sqe, int fd, unsigned int flags) {
	io_uring_prep_rw(IORING_OP_ASYNC_CANCEL, sqe, fd, NULL, 0, 0);
	sqe->cancel_flags = (__u32)flags | IORING_ASYNC_CANCEL_FD;
}

#define IORING_ACCEPT_MULTISHOT	(1U << 0)
static inline void io_uring_prep_multishot_accept(struct io_uring_sqe* sqe,
	int fd, struct sockaddr* addr,
	socklen_t* addrlen, int flags) {
	io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
	sqe->ioprio |= IORING_ACCEPT_MULTISHOT;
}
// TODO: }}

struct _BlPostTaskParm {
	BlTaskCallback cb;
	void* parm;
};

struct _BlPostTask_t {
	BlAioBase base;
	size_t slot;
	_BlPostTaskParm task;
};

struct _BlRecvTask_t {
	BlAioBase base;
	_BlPostTaskParm tasks[32];
};

struct _BlCancelIo_t {
	BlAioBase base;
	int fd;
	BlAioBase* io;
};

struct _BlMultishotAccept_t {
	BlAioBase base;
	int listenSock;
	BlOnSockAcceptFailure onFailure;
	BlOnSockAcceptLoopStopped onStopped;
};

struct Worker : public TimerMgr {
	io_uring ring;
	BlAioBase* waitSqeHead;
	BlAioBase* waitSqeTail;
	_BlRecvTask_t ioRecvTask;
	int fdPipe[2];
	size_t numFreeTasks;
	_BlPostTask_t* firstFreeTask; // use _BlPostTask_t.base.next as next link
	size_t numFreeCancels;
	_BlCancelIo_t* firstFreeCancel; // use _BlCancel_t.base.next as next link

	Worker() : waitSqeHead(nullptr), waitSqeTail(nullptr), numFreeTasks(0),
		firstFreeTask(nullptr), numFreeCancels(0), firstFreeCancel(nullptr) {}
};

BL_THREAD_LOCAL Worker* st_worker = nullptr;

static unsigned s_numSqeEntries = 4096; // default
static size_t s_numIoWorkers = 0;
static bool s_initSemaHasInitialized = false;
static sem_t s_initSema;
static size_t s_nextIoWorker = 0;
static size_t s_nextOtherWorker = 0;
static Worker* s_workers = nullptr;
static bool s_exitApplication = false;
static size_t s_numWorkers = 0;

struct SlotAcceptIo {
	size_t slot;
	_BlMultishotAccept_t* ioAccept;
};

struct AcceptLoop {
	int listenSock;
	size_t numAccepts;
	std::vector<SlotAcceptIo> slotAccepts;
};
static bl::Mutex s_lockAcceptLoop;
static std::map<UINT64, AcceptLoop> s_acceptLoops; // first: handle
static std::map<int, UINT64> s_acceptLoopSocks; // first: sock, second: handle of AcceptLoop
static UINT64 s_lastAcceptLoopHandle = 0;

static void FatalErr(const char* sErr, const char* file, int line) {
	char errBuf[256];
	snprintf(errBuf, sizeof(errBuf), "%s(err=%d) in %s:%d", sErr, errno, file, line);
	//TODO: add log
	fprintf(stderr, "%s(err=%d) in %s:%d\n", sErr, errno, file, line);
	throw errBuf;
}

#define FATAL_ERR(sErr) FatalErr((sErr), __FILE__, __LINE__)

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
	Worker* worker = st_worker;
	assert(worker);
	if (worker->numFreeTasks >= 1024)
		free(io);
	else {
		io->base.next = (BlAioBase*)worker->firstFreeTask;
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
	io_uring_prep_read(sqe, st_worker->fdPipe[0], io->tasks, sizeof(io->tasks), 0);
	io_uring_sqe_set_data(sqe, io);
}

static void _BlOnCqeRecvTask(_BlRecvTask_t* io, int r, uint32_t flags) {
	if (r < 0) {
		fprintf(stderr, "_BlOnCqeRecvTask: failed %d\n", r);
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
	if(io->io)
		io_uring_prep_cancel(sqe, io->io, IORING_ASYNC_CANCEL_ALL);
	else
		io_uring_prep_cancel_fd(sqe, io->fd, IORING_ASYNC_CANCEL_ALL);
	io_uring_sqe_set_data(sqe, io);
}

static void OnCancelIoCompleted(_BlCancelIo_t* io) {
	Worker* worker = st_worker;
	assert(worker);
	if (worker->numFreeCancels >= 1024)
		free(io);
	else {
		io->base.next = (BlAioBase*)worker->firstFreeCancel;
		worker->firstFreeCancel = io;
		++worker->numFreeCancels;
	}
}

inline void _BlInitCancelIo(_BlCancelIo_t* io, int fd, BlAioBase* aio) {
	BlOnCompletedAio onCompleted = (BlOnCompletedAio)OnCancelIoCompleted;
	_BLAIOBASE_INIT(_BlOnSqeCancelIo, _BlOnCqeAio)
	io->fd = fd;
	io->io = aio;
}

static void SubmitAndWaitCqe(struct io_uring* ring, struct io_uring_cqe** pCqe,
	std::priority_queue<OnceTimer>* onceTimerQ, TimerMap* timerMap, TimerQueue* timerQ) {
	int r;
	int64_t toWait;
	struct __kernel_timespec ts;
	for (;;) {
		toWait = GetWaitTime(onceTimerQ, timerMap, timerQ);
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
				usleep(10000); // 10ms
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
	BlAioBase* lazyio;
	bool hasRecvTask;
	int r;
	BlAioBase* head;

	worker->slot = slot;
	worker->nextTimerId = 0;

	io_uring_params params = {};
	params.flags |= IORING_SETUP_SINGLE_ISSUER; // since each thread has a ring, only this thread will touch this ring
	// the work-thread should process each incoming job sequentially -- there's probably little use in having queued
	// jobs available, e.g. between context switches
	// I'm don't feel confident in my understanding of these switches(it is noted that the computation here is negligible)
	params.flags |= IORING_SETUP_COOP_TASKRUN;
	params.flags |= IORING_SETUP_DEFER_TASKRUN;
	// "share async backend", but not SQ/CQ
	//params.flags |= IORING_SETUP_ATTACH_WQ;
	//params.wq_fd = root_fd;
	r = io_uring_queue_init_params(s_numSqeEntries, ring, &params);
	if (r < 0)
		FATAL_ERR("io_uring_queue_init failed");

	r = pipe2(worker->fdPipe, 0);
	if (r < 0)
		FATAL_ERR("Can't create pipe fds for worker");

	st_worker = worker;
	_BlInitRecvTask(&worker->ioRecvTask);
	_BlDoAio((BlAioBase*)&worker->ioRecvTask);
	sem_post(&s_initSema);
	for (;;) {
		SubmitAndWaitCqe(ring, &cqe, &worker->onceTimerQ, &worker->timerMap, &worker->timerQ);
		hasRecvTask = false;
		for (;;) {
			while ((head = worker->waitSqeHead) != nullptr) {
				struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
				if (!sqe)
					break;
				worker->waitSqeHead = head->next;
				if (worker->waitSqeHead == nullptr)
					worker->waitSqeTail = nullptr;
				head->onSqe(head, sqe);
			}

			lazyio = (BlAioBase*)io_uring_cqe_get_data(cqe);
			assert(lazyio != nullptr);
			hasRecvTask = (lazyio == &worker->ioRecvTask.base);
			int res = cqe->res;
			uint32_t flags = cqe->flags;
			io_uring_cqe_seen(ring, cqe);
			lazyio->onCqe(lazyio, res, flags);
			r = io_uring_peek_cqe(ring, &cqe);
			if (r < 0)
				break;
		}
		if (s_exitApplication)
			break;
		if (hasRecvTask)
			_BlDoAio((BlAioBase*)&worker->ioRecvTask);
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
	st_worker = nullptr;
	mco_thread_cleanup();
}

static void BlockSignals() {
	sigset_t mask;
	sigfillset(&mask);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

void BlInit(uint32_t options, size_t numIoWorkers, size_t numOtherWorkers) {
	SetLimits();
	BlockSignals();
	if (numIoWorkers == 0)
		numIoWorkers = BL_gNumCpus;
	if (numOtherWorkers == 0)
		numOtherWorkers = 2*BL_gNumCpus;
	s_numIoWorkers = numIoWorkers;
	size_t numWorkers = numIoWorkers + numOtherWorkers;
	s_numWorkers = numWorkers;
	if (numWorkers >= 4096)
		FATAL_ERR("Two many workers");
	if (!s_initSemaHasInitialized) {
		s_initSemaHasInitialized = true;
		if (sem_init(&s_initSema, 0, 0) != 0)
			FATAL_ERR("sem_init failed");
	}
	s_workers = new Worker[numWorkers];
	bool useMain = (options & BL_INIT_USE_MAIN_THREAD_AS_WORKER);
	for (size_t i = 0; i < numWorkers; ++i) {
		if (!useMain || i > 0)
			s_workers[i].thread = std::thread([i]() { _BlIoLoop(i); });
	}
	for (size_t i = 0; i < numWorkers; ++i)
		sem_wait(&s_initSema);
}

inline size_t PickupWorker(bool isIoTask) {
	size_t x;
	if (isIoTask) {
		x = s_nextIoWorker++;
		if (s_nextIoWorker >= s_numIoWorkers)
			s_nextIoWorker = 0;
		if (x >= s_numIoWorkers)
			x = 0;
	}
	else {
		size_t numOtherWorkers = s_numWorkers - s_numIoWorkers;
		x = s_nextOtherWorker++;
		if (s_nextOtherWorker >= numOtherWorkers)
			s_nextOtherWorker = 0;
		if (x >= numOtherWorkers)
			x = 0;
		x += s_numIoWorkers;
	}
	return x;
}

static bool _BlPostTaskToWorker(size_t slot, BlTaskCallback cb, void* parm) {
	Worker* worker = st_worker;
	if (worker) { // inside _BlIoLoop(thread pool)
		if (slot == worker->slot) {
			cb(parm);
			return true;
		}
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
				fprintf(stderr, "_BlPostTaskToWorker: out of memory\n");
				return false;
			}
			_BlInitPostTask(ioTask, slot, cb, parm);
		}
		ioTask->base.coro = ioTask;
		_BlDoAio((BlAioBase*)ioTask);
	}
	else {
		_BlPostTaskParm task;
		task.cb = cb;
		task.parm = parm;
		int r = write(s_workers[slot].fdPipe[1], &task, sizeof(task));
		if (r < 0) {
			fprintf(stderr, "_BlPostTaskToWorker: write pipe failed, err=%d\n", errno);
			return false;
		}
	}
	return true;
}

void _BlBroadcastIoTask(BlTaskCallback cb, void* parm, size_t currentSlot) {
	for (size_t slot = 0; slot < s_numIoWorkers; ++slot) {
		if (slot != currentSlot)
			_BlPostTaskToWorker(slot, cb, parm);
		else
			cb(parm);
	}
}

void BlPostTask(BlTaskCallback cb, void* parm, bool isIoTask) {
	size_t slot = PickupWorker(isIoTask);
	_BlPostTaskToWorker(slot, cb, parm);
}

#include "../../blaco_priv.h"

void BlExitNotify() {
	s_exitApplication = true;
	for (size_t i = 0; i < s_numWorkers; ++i)
		_BlPostTaskToWorker(i, nullptr, nullptr);
}

void BlWaitExited() {
	_BlWaitExited();
	s_numIoWorkers = 0;
}

inline void CancelIoInWorker(Worker* worker, int fd, BlAioBase* io) {
	_BlCancelIo_t* ioCancel = worker->firstFreeCancel;
	if (ioCancel) {
		--worker->numFreeCancels;
		worker->firstFreeCancel = (_BlCancelIo_t*)ioCancel->base.next;
		ioCancel->fd = fd;
		ioCancel->io = io;
	}
	else {
		ioCancel = (_BlCancelIo_t*)malloc(sizeof(_BlCancelIo_t));
		if (!ioCancel) {
			assert(false);
			fprintf(stderr, "CancelIoInWorker: out of memory\n");
			return;
		}
		_BlInitCancelIo(ioCancel, fd, io);
	}
	ioCancel->base.coro = ioCancel;
	_BlDoAio((BlAioBase*)ioCancel);
}

static void OnCancelIoFdPosted(void* parm) {
	CancelIoInWorker(st_worker, (intptr_t)parm, NULL);
}

static void OnCancelIoPosted(void* parm) {
	CancelIoInWorker(st_worker, 0, (BlAioBase*)parm);
}

int BlCancelIo(int fd, BlAioBase* io) {
	size_t currentSlot = st_worker ? st_worker->slot : (size_t)-1;
	if(io)
		_BlBroadcastIoTask(OnCancelIoPosted, io, currentSlot);
	else
		_BlBroadcastIoTask(OnCancelIoFdPosted, (void*)(intptr_t)fd, currentSlot);
	return 0;
}

bool _BlDoAio(BlAioBase* io) {
	Worker* worker = (Worker*)st_worker;
	if (!worker) {
		io->ret = -ENOTSUP;
		fprintf(stderr, "_BlDoAio: Bug? Be called out of thread pool.\n");
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

inline void CompleteIo(BlAioBase* io) {
	if (io->onCompleted)
		io->onCompleted(io->coro);
}

void _BlOnCqeAio(BlAioBase* io, int res, uint32_t flags) {
	io->ret = res;
	CompleteIo(io);
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

void _BlOnCqeSockMustSend(BlSockMustSend_t* io, int r, uint32_t flags) {
	if (r > 0) {
		if (r == (int)io->len)
			io->base.ret = 0;
		else {
			io->len -= r;
			io->buf = ((char*)io->buf)+r;
			if (_BlDoAio((BlAioBase*)io))
				return;
			assert(false);
		}
	}
	else if (r == 0)
		io->base.ret = -E_PEER_CLOSED;
	else
		io->base.ret = r;
	CompleteIo(&io->base);
}

void _BlOnCqeSockMustSendVec(BlSockMustSendVec_t* io, int r, uint32_t flags) {
	if (r > 0) {
		IoVecAdjustAfterIo(&io->msghdr.msg_iov, &io->msghdr.msg_iovlen, r);
		if (io->msghdr.msg_iovlen <= 0)
			io->base.ret = 0;
		else {
			if (_BlDoAio((BlAioBase*)io))
				return;
			assert(false);
		}
	}
	else if (r == 0)
		io->base.ret = -E_PEER_CLOSED;
	else
		io->base.ret = r;
	CompleteIo(&io->base);
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

void _BlOnCqeSockRecvFrom(BlSockRecvFrom_t* io, int r, uint32_t flags) {
	io->base.ret = r;
	if (io->addrLen)
		*io->addrLen = io->msghdr.msg_namelen;
	CompleteIo(&io->base);
}

void _BlOnCqeSockRecvVecFrom(BlSockRecvVecFrom_t* io, int r, uint32_t flags) {
	io->base.ret = r;
	if (io->addrLen)
		*io->addrLen = io->msghdr.msg_namelen;
	CompleteIo(&io->base);
}

void _BlOnCqeSockMustRecv(BlSockMustRecv_t* io, int r, uint32_t flags) {
	if (r > 0) {
		if (r == (int)io->len)
			io->base.ret = 0;
		else {
			io->len -= r;
			io->buf = ((char*)io->buf)+r;
			if (_BlDoAio((BlAioBase*)io))
				return;
			assert(false);
		}
	}
	else if (r == 0)
		io->base.ret = -E_PEER_CLOSED;
	else
		io->base.ret = r;
	CompleteIo(&io->base);
}

void _BlOnCqeSockMustRecvVec(BlSockMustRecvVec_t* io, int r, uint32_t flags) {
	if (r > 0) {
		IoVecAdjustAfterIo(&io->msghdr.msg_iov, &io->msghdr.msg_iovlen, r);
		if (io->msghdr.msg_iovlen <= 0)
			io->base.ret = 0;
		else {
			if (_BlDoAio((BlAioBase*)io))
				return;
			assert(false);
		}
	}
	else if (r == 0)
		io->base.ret = -E_PEER_CLOSED;
	else
		io->base.ret = r;
	CompleteIo(&io->base);
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

static void _BlOnSqeMultishotAccept(_BlMultishotAccept_t* io, struct io_uring_sqe* sqe) {
	io_uring_prep_multishot_accept(sqe, io->listenSock, NULL, NULL, 0);
	io_uring_sqe_set_data(sqe, io);
}

static void _BlOnCqeMultishotAccept(_BlMultishotAccept_t* io, int r, uint32_t flags) {
	if (r >= 0) {
		// accepted
		if (io->base.onCompleted) {
			BlSockAddr peer;
			socklen_t peerLen = sizeof(peer);
			BlSockGetPeerAddr(r, &peer.sa, &peerLen);
			((BlOnSockAccepted)io->base.onCompleted)(io->base.coro, r, &peer.sa);
			if (!(flags & IORING_CQE_F_MORE)) // else didn't need call _BlDoAio((BlAioBase*)io) because of multishot accept
				_BlDoAio((BlAioBase*)io);
		}
	}
	else {
		// failed
		if (io->onFailure)
			io->onFailure(io->base.coro, io->listenSock, -r);
		BlSockClose(io->listenSock);
		bool stopped = false;
		{
			bl::Mutex::Auto autoLock(s_lockAcceptLoop);
			auto it = s_acceptLoopSocks.find(io->listenSock);
			if (it != s_acceptLoopSocks.cend()) {
				auto it2 = s_acceptLoops.find(it->second);
				if (it2 != s_acceptLoops.end()) {
					size_t k = --(it2->second.numAccepts);
					if (k == 0) {
						s_acceptLoops.erase(it2);
						s_acceptLoopSocks.erase(it);
						stopped = true;
					}
				}
			}
		}
		BlOnSockAcceptLoopStopped onStopped = io->onStopped;
		void* coro = io->base.coro;
		delete io;
		if (stopped && onStopped)
			onStopped(coro);
	}
}

UINT64 BlSockStartAcceptLoop(int sock, sa_family_t family, size_t numConcurrentAccepts, BlOnSockAccepted onAccepted,
	BlOnSockAcceptFailure onFailure, BlOnSockAcceptLoopStopped onStopped, void* parm) {
	if (sock < 0) {
		BlSetLastError(EINVAL);
		return 0;
	}
	if (numConcurrentAccepts == 0)
		numConcurrentAccepts = BL_gNumCpus;

	std::vector<SlotAcceptIo> slotAccepts;
	for (size_t i = 0; i < numConcurrentAccepts; ++i) {
		auto* io = new _BlMultishotAccept_t;
		io->base.onSqe = (_BlOnSqe)_BlOnSqeMultishotAccept;
		io->base.onCqe = (_BlOnCqe)_BlOnCqeMultishotAccept;
		io->base.onCompleted = (BlOnCompletedAio)onAccepted;
		io->base.coro = parm;
		io->listenSock = sock;
		io->onFailure = onFailure;
		io->onStopped = onStopped;
		size_t slot = PickupWorker(true);
		slotAccepts.emplace_back(slot, io);
	}
	bool ok = false;
	BL_DEFER( if (!ok) {
		for (auto& slotAccept : slotAccepts)
			delete slotAccept.ioAccept;
	});

	UINT64 h = 0;
	{
		bl::Mutex::Auto autoLock(s_lockAcceptLoop);
		auto it = s_acceptLoopSocks.find(sock);
		if (it != s_acceptLoopSocks.cend()) {
			for (size_t i = 0; i < numConcurrentAccepts; ++i) {
			}
			BlSetLastError(EEXIST);
			return 0;
		}
		do {
			++s_lastAcceptLoopHandle;
			if (s_lastAcceptLoopHandle == 0)
				++s_lastAcceptLoopHandle;
		} while (s_acceptLoops.contains(s_lastAcceptLoopHandle));
		h = s_lastAcceptLoopHandle;
		s_acceptLoops.emplace(h, AcceptLoop{ sock, numConcurrentAccepts, slotAccepts });
		s_acceptLoopSocks.emplace(sock, h);
	}

	for (auto& slotAccept : slotAccepts)
		_BlPostTaskToWorker(slotAccept.slot, (BlTaskCallback)_BlDoAio, slotAccept.ioAccept);
	ok = true;
	return h;
}

bool BlSockStopAcceptLoop(UINT64 hLoop) {
	std::vector<SlotAcceptIo> slotAccepts;
	{
		bl::Mutex::Auto autoLock(s_lockAcceptLoop);
		auto it = s_acceptLoops.find(hLoop);
		if (it == s_acceptLoops.cend())
			return false;
		slotAccepts = it->second.slotAccepts;
	}
	Worker* worker = st_worker;
	size_t currentSlot = worker ? worker->slot : (size_t)-1;
	for (auto& slotAccept: slotAccepts) {
		if (slotAccept.slot != currentSlot)
			_BlPostTaskToWorker(slotAccept.slot, (BlTaskCallback)OnCancelIoPosted, slotAccept.ioAccept);
		else
			CancelIoInWorker(worker, 0, (BlAioBase*)slotAccept.ioAccept);
	}
	return true;
}
