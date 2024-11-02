#include "doctest.h"

#include <thread>
#include <chrono>
#include <iostream>

#include "sockaddr.h"
#include "mcoaio.h"

void coro_entry2(mco_coro* co2) {
	REQUIRE(mco_running() == co2);
	REQUIRE(mco_status(co2) == MCO_RUNNING);
	mco_coro* co;
	mco_coro** pco = (mco_coro**)mco_pop(co2, sizeof(co), sizeof(co));
	REQUIRE(pco != NULL);
	co = *pco;
	REQUIRE(mco_get_bytes_stored(co2) == 0);
	REQUIRE(mco_status(co) == MCO_NORMAL);
	REQUIRE(mco_get_bytes_stored(co2) == 0);
	printf("hello 2\n");
	REQUIRE(mco_yield() == MCO_SUCCESS);
	printf("world! 2\n");
}

static int dummy_user_data = 0;

void coro_entry(mco_coro* co) {
	mco_coro* co2;

	/* Startup checks */
	REQUIRE(mco_get_user_data(co) == &dummy_user_data);
	REQUIRE(mco_running() == co);
	REQUIRE(mco_status(co) == MCO_RUNNING);

	/* Get storage 1 */
	REQUIRE(mco_get_bytes_stored(co) == 6);
	char* p = (char*)mco_peek(co, mco_get_bytes_stored(co), 1);
	REQUIRE(p != NULL);
	REQUIRE(strcmp(p, "hello") == 0);
	p = (char*)mco_pop(co, mco_get_bytes_stored(co), 1);
	REQUIRE(p != NULL);
	puts(p);

	/* Set storage 1 */
	int* q = (int*)mco_push(co, sizeof(int), sizeof(int));
	REQUIRE(q != NULL);
	*q = 1;

	/* Yield 1 */
	REQUIRE(mco_yield() == MCO_SUCCESS);

	/* Get storage 2 */
	REQUIRE(mco_get_bytes_stored(co) == 7);
	p = (char*)mco_pop(co, mco_get_bytes_stored(co), 1);
	REQUIRE(p != NULL);
	REQUIRE(strcmp(p, "world!") == 0);
	puts(p);

	/* Set storage 2 */
	q = (int*)mco_push(co, sizeof(int), sizeof(int));
	REQUIRE(q != NULL);
	*q = 2;

	/* Nested coroutine test */
	mco_desc desc = mco_desc_init(coro_entry2, 0);
	REQUIRE(mco_create(&co2, &desc) == MCO_SUCCESS);
	mco_coro** pco = (mco_coro**)mco_push(co2, sizeof(co), sizeof(co));
	REQUIRE(pco != NULL);
	*pco = co;
	REQUIRE(mco_resume(co2) == MCO_SUCCESS);
	REQUIRE(mco_resume(co2) == MCO_SUCCESS);
	REQUIRE(mco_get_bytes_stored(co2) == 0);
	REQUIRE(mco_status(co2) == MCO_DEAD);
	REQUIRE(mco_status(co) == MCO_RUNNING);
	REQUIRE(mco_running() == co);
	REQUIRE(mco_destroy(co2) == MCO_SUCCESS);
}

TEST_CASE("test minicoro") {
	mco_coro* co;

	/* Create coroutine */
	mco_desc desc = mco_desc_init(coro_entry, 0);
	desc.user_data = &dummy_user_data;
	REQUIRE(mco_create(&co, &desc) == MCO_SUCCESS);
	REQUIRE(mco_status(co) == MCO_SUSPENDED);

	/* Set storage 1 */
	const char first_word[] = "hello";
	char* p = (char*)mco_push(co, sizeof(first_word), 1);
	REQUIRE(p != NULL);
	memcpy(p, first_word, sizeof(first_word));

	/* Resume 1 */
	REQUIRE(mco_resume(co) == MCO_SUCCESS);
	REQUIRE(mco_status(co) == MCO_SUSPENDED);

	/* Get storage 1 */
	int* q = (int*)mco_pop(co, sizeof(int), sizeof(int));
	REQUIRE(q != NULL);
	REQUIRE(*q == 1);

	/* Set storage 2 */
	const char second_word[] = "world!";
	p = (char*)mco_push(co, sizeof(second_word), 1);
	REQUIRE(p != NULL);
	memcpy(p, second_word, sizeof(second_word));

	/* Resume 2 */
	REQUIRE(mco_resume(co) == MCO_SUCCESS);
	REQUIRE(mco_status(co) == MCO_DEAD);

	/* Get storage 2 */
	q = (int*)mco_pop(co, sizeof(int), sizeof(int));
	REQUIRE(q != NULL);
	REQUIRE(*q == 2);

	/* Destroy */
	REQUIRE(mco_destroy(co) == MCO_SUCCESS);
	printf("Test minicoro ok.\n");
}

static void Log(void* /*logger*/, const char* s, int r) {
	fprintf(stderr, "[%d]%s\n", r, s);
}

static void echo(SOCKET sock, const struct sockaddr* peer, void*, const McoTcpServerOptions* opts) {
	char buf[1024];
	size_t n = sizeof(buf);
	REQUIRE(0 == BlSockAddr2Str(peer, buf, n, false));
	std::cout << "Accept a connection from: " << buf << std::endl;
	size_t nTotalRead = 0, nTotalSend = 0;
	int nRead;
	int err;
	for (;;) {
		nRead = McoSockRecv(sock, buf, n, 0);
		if (nRead < 0) {
			std::cerr << "echo: recv err[" << nRead << "]" << std::endl;
			break;
		}
		if (nRead == 0) // peer closed
			break;
		nTotalRead += nRead;

		std::cout << "Server recv " << nRead << " bytes [" << std::string_view(buf, nRead) << "]" << std::endl;
		err = McoSockMustSend(sock, buf, nRead, 0);
		REQUIRE(err == 0);
		nTotalSend += nRead;
		std::cout << "Server send " << nRead << " bytes ok" << std::endl;
	}
	McoTcpClose(sock);
	if (nTotalSend != nTotalRead)
		std::cerr << "echo: num bytes of send[" << nTotalSend << "] != num bytes of recv[" << nTotalRead << "]" << std::endl;
}

const char* echo_test_unix_server_path2 = "./echo_test_unix";

static SOCKET echoserver(sa_family_t family, size_t n) {
	std::cout << "Echo server started in thread " << std::this_thread::get_id() << std::endl;
	SOCKET sockListen;
	if (family == AF_INET || family == AF_INET6) {
		BlSockAddr addr;
		BlGenSockAddrPort(28629, family == AF_INET6, &addr.sa);
		sockListen = BlTcpNewServer(&addr.sa, true, 100);
	}
	else {
		REQUIRE(family == AF_UNIX);
		sockListen = BlUnixNewServer(echo_test_unix_server_path2, true, 100);
	}
	REQUIRE(sockListen != INVALID_SOCKET);
	auto opts = new McoTcpServerOptions{n, 0, Log, NULL};
	McoTcpStartServer(sockListen, family, echo, NULL, opts);
	return sockListen;
}

struct EchoClientParm {
	sa_family_t family;
	int i;
};

static void CoEchoClient(mco_coro* coro) {
	struct EchoClientParm* parm = (struct EchoClientParm*)coro->storage;
	sa_family_t family = parm->family;
	int i = parm->i;
	std::cout << "Client " << i << " started in the thread " << std::this_thread::get_id() << std::endl;
	SOCKET sock;
	BlSockAddr srvAddr;
	if (family == AF_INET || family == AF_INET6) {
		sock = BlSockTcp(family == AF_INET6);
		BlParseSockAddr2(family == AF_INET ? "127.0.0.1" : "::1", -1, 28629,
			&srvAddr.sa, sizeof(srvAddr));
	}
	else {
		REQUIRE(family == AF_UNIX);
		sock = BlSockUnix();
		BlGenUnixSockAddr(&srvAddr, echo_test_unix_server_path2);
	}
	REQUIRE(sock != INVALID_SOCKET);
	char buf[128];
	BlSockAddr2Str(&srvAddr.sa, buf, sizeof(buf), false);
	std::cout << "Connecting " << buf << std::endl;
	int err = McoTcpConnect(sock, &srvAddr.sa);
	if (err != 0) {
		std::cout << "Connect error: " << err << std::endl;
		REQUIRE(err == 0);
		return;
	}

	sprintf(buf, "Hello from mco client %d", i);
	int nn = strlen(buf);
	err = McoSockMustSend(sock, buf, nn, 0);
	REQUIRE(err == 0);

	char buf2[128];
	err = McoSockMustRecv(sock, buf2, nn, 0);
	REQUIRE(err == 0);
	std::cout << "Client recv[" << std::string_view(buf2, nn) << "]" << std::endl;
	REQUIRE(memcmp(buf, buf2, nn) == 0);

	McoTcpClose(sock);
}

static mco_coro* echoclient(sa_family_t family, int i) {
	mco_desc desc = mco_desc_init(CoEchoClient, 0);
	mco_coro* coro;
	mco_result r = mco_create(&coro, &desc);
	REQUIRE(r == MCO_SUCCESS);
	struct EchoClientParm* parm = (struct EchoClientParm*)coro->storage;
	parm->family = family;
	parm->i = i;
	McoSchedule(coro, true);
	return coro;
}

TEST_CASE("test mco echo server ipv4") {
	BlInit(0, 0, 0);
	SOCKET sockListen = echoserver(AF_INET, 2);
	mco_coro* coros[2];
	for (int i = 0; i < 2; ++i)
		coros[i] = echoclient(AF_INET, i);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	REQUIRE(BlCancelIo(sockListen, NULL) == 0);
	for (int i = 0; i < 2; ++i)
		mco_destroy(coros[i]);
	BlExitNotify();
	BlWaitExited();
	BlSockClose(sockListen);
}

TEST_CASE("test mco echo server ipv6") {
	BlInit(0, 0, 0);
	SOCKET sockListen = echoserver(AF_INET6, 2);
	mco_coro* coros[2];
	for (int i = 0; i < 2; ++i)
		coros[i] = echoclient(AF_INET6, i);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	REQUIRE(BlCancelIo(sockListen, NULL) == 0);
	for (int i = 0; i < 2; ++i)
		mco_destroy(coros[i]);
	BlExitNotify();
	BlWaitExited();
	BlSockClose(sockListen);
}

TEST_CASE("test mco echo server unix domain socket") {
	BlInit(0, 0, 0);
	SOCKET sockListen = echoserver(AF_UNIX, 2);
	mco_coro* coros[2];
	for (int i = 0; i < 2; ++i)
		coros[i] = echoclient(AF_UNIX, i);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	REQUIRE(BlCancelIo(sockListen, NULL) == 0);
	for (int i = 0; i < 2; ++i)
		mco_destroy(coros[i]);
	BlExitNotify();
	BlWaitExited();
	BlSockClose(sockListen);
}

static void udp_receiver(mco_coro*) {
	BlSockAddr addr;
	BlGenSockAddrPort(43217, false, &addr.sa);
	SOCKET sockR = BlUdpNewPeerReceiver(&addr.sa, true);
	REQUIRE(sockR != INVALID_SOCKET);
	char buf[128];
	size_t n = sizeof(buf);
	int nRecv, nSend;
	socklen_t addrLen = sizeof(addr);
	nRecv = McoSockRecvFrom(sockR, &addr.sa, &addrLen, buf, n, 0);
	REQUIRE(nRecv > 0);
	char sAddr[128];
	BlSockAddr2Str(&addr.sa, sAddr, sizeof(sAddr), false);
	std::cout << "Mco udp_receiver: received " << nRecv << " bytes from " << sAddr << std::endl;
	nSend = McoSockSendTo(sockR, &addr.sa, buf, nRecv, 0);
	REQUIRE(nSend == nRecv);
	REQUIRE(0 == BlSockClose(sockR));
	std::cout << "Mco udp receiver: sock closed" << std::endl;
}

static void udp_sender(mco_coro*) {
	BlSockAddr srvAddr;
	BlParseSockAddr2("127.0.0.1", -1, 43217, &srvAddr.sa, sizeof(srvAddr));
	SOCKET sockS = BlUdpNewPeerSender(&srvAddr.sa);
	REQUIRE(sockS != INVALID_SOCKET);
	char buf[128] = "Haha, this is sender speak!";
	size_t n = strlen(buf);
	int rev = BlSockPoll(sockS, POLLWRNORM, 0);
	REQUIRE(rev == POLLWRNORM);
	int nSend = McoSockSend(sockS, buf, n, 0);
	REQUIRE(nSend == n);
	char buf2[128];
	size_t n2 = sizeof(buf2);
	McoSleep(10);
	rev = BlSockPoll(sockS, POLLRDNORM, 0);
	REQUIRE((rev & (POLLERR | POLLRDNORM)) != 0);
	int nRecv = McoSockRecvFrom(sockS, nullptr, nullptr, buf2, n2, 0);
	REQUIRE(nRecv == n);
	REQUIRE(memcmp(buf, buf2, n) == 0);
	REQUIRE(0 == BlSockClose(sockS));
	std::cout << "Mco udp_sender: received same content I have sent." << std::endl;
}

TEST_CASE("test mco udp 1") {
	BlInit(0, 0, 0);

	mco_desc desc1 = mco_desc_init(udp_receiver, 0);
	mco_coro* coroRecv;
	int r = mco_create(&coroRecv, &desc1);
	REQUIRE(r == MCO_SUCCESS);
	McoSchedule(coroRecv, true);

	mco_desc desc2 = mco_desc_init(udp_sender, 0);
	mco_coro* coroSend;
	r = mco_create(&coroSend, &desc2);
	REQUIRE(r == MCO_SUCCESS);
	McoSchedule(coroSend, true);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	BlExitNotify();
	BlWaitExited();
}

static void udp_receiver2(mco_coro*) {
	BlSockAddr addr;
	BlGenSockAddrPort(43217, false, &addr.sa);
	SOCKET sockR = BlUdpNewPeerReceiver(&addr.sa, true);
	REQUIRE(sockR != INVALID_SOCKET);
	char buf[128];
	iovec bufs[2] = { { buf, 16 }, { buf + 32, sizeof(buf) - 32 } };
	socklen_t addrLen = sizeof(addr);
	int nRecv = McoSockRecvVecFrom(sockR, &addr.sa, &addrLen, bufs, 2, 0);
	REQUIRE(nRecv > 0);
	char sAddr[128];
	BlSockAddr2Str(&addr.sa, sAddr, sizeof(sAddr), false);
	std::cout << "Mco udp_receiver2: received " << nRecv << " bytes from " << sAddr << std::endl;
	bufs[1].iov_len = nRecv - bufs[0].iov_len;
	int nSend = McoSockSendVecTo(sockR, &addr.sa, bufs, 2, 0);
	REQUIRE(nSend == nRecv);
	REQUIRE(0 == BlSockClose(sockR));
	std::cout << "Mco udp receiver2: sock closed" << std::endl;
}

static void CollectIoVec(char* dst, iovec* bufs, size_t bufCnt, size_t nTotal) {
	for (size_t i = 0; nTotal > 0 && i < bufCnt; ++i) {
		if (nTotal <= bufs[i].iov_len) {
			memcpy(dst, bufs[i].iov_base, nTotal);
			nTotal = 0;
		}
		else {
			memcpy(dst, bufs[i].iov_base, bufs[i].iov_len);
			nTotal -= bufs[i].iov_len;
			dst += bufs[i].iov_len;
		}
	}
	REQUIRE(nTotal == 0);
}

static void udp_sender2(mco_coro*) {
	BlSockAddr srvAddr;
	BlParseSockAddr2("127.0.0.1", -1, 43217, &srvAddr.sa, sizeof(srvAddr));
	SOCKET sockS = BlUdpNewPeerSender(&srvAddr.sa);
	REQUIRE(sockS != INVALID_SOCKET);
	char buf1[] = "Haha, this is mco sender speak(part1)!";
	size_t n1 = strlen(buf1);
	char buf2[] = "Using mco scatter/gather i/o with iovec data here(part2)";
	size_t n2 = strlen(buf2);
	size_t n = n1 + n2;
	int rev = BlSockPoll(sockS, POLLWRNORM, 0);
	REQUIRE(rev == POLLWRNORM);
	iovec bufs[2] = { { buf1, n1 }, { buf2, n2 } };
	int nSend = McoSockSendVec(sockS, bufs, 2, 0);
	REQUIRE(nSend == n);
	char buf3[3];
	char buf4[7];
	char buf5[87];
	size_t n3 = sizeof(buf3);
	size_t n4 = sizeof(buf4);
	size_t n5 = sizeof(buf5);
	iovec bufs2[3] = { {buf3, n3}, {buf4, n4}, {buf5, n5} };
	McoSleep(10);
	rev = BlSockPoll(sockS, POLLRDNORM, 0);
	REQUIRE((rev & (POLLERR|POLLRDNORM)) != 0);
	int nRecv = McoSockRecvVec(sockS, bufs2, 3, 0);
	REQUIRE(nRecv == n);
	char tbuf1[128];
	char tbuf2[128];
	CollectIoVec(tbuf1, bufs, 2, n);
	CollectIoVec(tbuf2, bufs2, 3, nRecv);
	REQUIRE(memcmp(tbuf1, tbuf2, n) == 0);
	REQUIRE(0 == BlSockClose(sockS));
	std::cout << "Mco udp_sender2: received same content I have sent." << std::endl;
}

TEST_CASE("test mco udp 2") {
	BlInit(0, 0, 0);

	mco_desc desc1 = mco_desc_init(udp_receiver2, 0);
	mco_coro* coroRecv;
	int r = mco_create(&coroRecv, &desc1);
	REQUIRE(r == MCO_SUCCESS);
	McoSchedule(coroRecv, true);

	mco_desc desc2 = mco_desc_init(udp_sender2, 0);
	mco_coro* coroSend;
	r = mco_create(&coroSend, &desc2);
	REQUIRE(r == MCO_SUCCESS);
	McoSchedule(coroSend, true);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	BlExitNotify();
	BlWaitExited();
}

static const char* fName = "./aaaabbbb.txt";
static const char* str2 = "Writing some mco data";
static const uint32_t str2len = strlen(str2);
static const char* str3 = " some mco HaloKittyTulip flo";
static const uint32_t str3len = strlen(str3);

static void ReadWriteFile(mco_coro*) {
	int f = BlFileOpen(fName, BLFILE_O_CREAT | BLFILE_O_RDWR, 0666);
	REQUIRE(f >= 0);
	int nWrite, nRead;
	nWrite = McoFileWrite(f, 0, str2, str2len);
	REQUIRE(nWrite == str2len);
	char buf[128];
	nRead = McoFileRead(f, 0, buf, sizeof(buf));
	REQUIRE(nRead == str2len);
	REQUIRE(0 == memcmp(buf, str2, str2len));
	struct iovec bufs1[] = { {(char*)"Halo", 4}, {(char*)"Kitty", 5}, {(char*)"Tulip flowers", 13}};
	nWrite = McoFileWriteVec(f, 17, bufs1, 3);
	REQUIRE(nWrite == 22);
	char buf21[6], buf22[3], buf23[2], buf24[4], buf25[13];
	struct iovec bufs2[] = { {buf21, sizeof(buf21)}, {buf22, sizeof(buf22)},
		{buf23, sizeof(buf23)}, {buf24, sizeof(buf24)}, {buf25, sizeof(buf25)}};
	nRead = McoFileReadVec(f, 7, bufs2, 5);
	REQUIRE(nRead == 28);
	CollectIoVec(buf, bufs2, 5, nRead);
	REQUIRE(0 == memcmp(buf, str3, str3len));
	BlFileClose(f);
	std::cout << "McoReadWriteFile: read same content I have written." << std::endl;
}

TEST_CASE("test mco file") {
	BlInit(0, 0, 0);
	unlink(fName);
	mco_desc desc = mco_desc_init(ReadWriteFile, 0);
	mco_coro* coro;
	int r = mco_create(&coro, &desc);
	REQUIRE(r == MCO_SUCCESS);
	McoSchedule(coro, true);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	BlExitNotify();
	BlWaitExited();
}
