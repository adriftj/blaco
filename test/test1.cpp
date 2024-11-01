#include "doctest.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <ctime>

#include "blaco.hpp"

bl::task<void> test1(int x) {
	std::cout << "Running coroutine " << x << " in the thread id=" << std::this_thread::get_id() << std::endl;
	co_return;
}

TEST_CASE("test init and go") {
	BlInit(0, 0, 0);
	bl::go(test1(1).get_handle(), true);
	bl::go(test1(2).get_handle(), false);
	bl::go(test1(3).get_handle(), true);
	bl::go(test1(4).get_handle(), false);
	bl::go(test1(5).get_handle(), true);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	BlExitNotify();
	BlWaitExited();
}

void taskcallback1(std::string* str) {
	std::cout << "Got string[" << *str << "] in the thread id=" << std::this_thread::get_id() << std::endl;
	delete str;
}

TEST_CASE("test post task") {
	BlInit(0, 0, 0);
	BlPostTask((BlTaskCallback)taskcallback1, new std::string("Hoha I/O!"), true);
	BlPostTask((BlTaskCallback)taskcallback1, new std::string("Hoha other!"), false);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	BlExitNotify();
	BlWaitExited();
}

bl::task<void> echo(SOCKET sock) {
	char buf[1024];
	size_t n = sizeof(buf);
	size_t nTotalRead = 0, nTotalSend = 0;
	for (;;) {
		int nRead = co_await bl::SockRecv(sock, buf, n);
		REQUIRE(nRead >= 0);
		if (nRead == 0) // peer closed
			break;
		nTotalRead += nRead;

		std::cout << "Server recv " << nRead << " bytes [" << std::string_view(buf, nRead) << "]" << std::endl;
		auto errSend = co_await bl::SockMustSend(sock, buf, nRead);
		REQUIRE(errSend == 0);
		nTotalSend += nRead;
		std::cout << "Server send " << nRead << " bytes ok" << std::endl;
	}
	co_await bl::TcpClose(sock);
	REQUIRE(nTotalSend == nTotalRead);
}

const char* echo_test_unix_server_path = "./echo_test_unix";

SOCKET echoserver(sa_family_t family, int n) {
	std::cout << "Echo server started in thread " << std::this_thread::get_id() << std::endl;
	SOCKET sockListen;
	if (family == AF_INET || family == AF_INET6)
		sockListen = BlTcpNewServer(bl::SockAddr(28629, family==AF_INET6), true, 100);
	else {
		REQUIRE(family == AF_UNIX);
		sockListen = BlUnixNewServer(echo_test_unix_server_path, true, 100);
	}
	if (sockListen == INVALID_SOCKET)
		std::cout << "Failed to create server socket:err=" << BlGetLastError() << std::endl;
	REQUIRE(sockListen != INVALID_SOCKET);
	bl::TcpStartServer(sockListen, family,
		[](SOCKET sock, const struct sockaddr& peer, bl::FnTcpServerLog fnLog) {
			bl::SockAddr aPeer(peer);
			std::cout << "Accept a connection from: " << aPeer.to_str() << std::endl;
			return echo(sock);
		},
		[](const char* s, int r) {
			std::cout << s << "[" << r << "]" << std::endl;
		},
		n);
	return sockListen;
}

bl::task<void> echoclient(sa_family_t family, int i) {
	std::cout << "Client " << i << " started in the thread " << std::this_thread::get_id() << std::endl;
	SOCKET sock;
	bl::SockAddr srvAddr;
	if (family == AF_INET || family == AF_INET6) {
		sock = BlSockTcp(family == AF_INET6);
		srvAddr = family == AF_INET ?
			bl::SockAddr("127.0.0.1", 28629) : bl::SockAddr("::1", 28629);
	}
	else {
		REQUIRE(family == AF_UNIX);
		sock = BlSockUnix();
		srvAddr = bl::SockAddr(echo_test_unix_server_path);
	}
	REQUIRE(sock != INVALID_SOCKET);
	std::cout << "Connecting " << srvAddr.to_str() << std::endl;
	auto err = co_await bl::TcpConnect(sock, srvAddr);
	if (err != 0) {
		std::cout << "Connect error: " << err << std::endl;
		REQUIRE(err == 0);
		co_return;
	}

	char buf[128];
	sprintf(buf, "Hello from client %d", i);
	int nn = strlen(buf);
	auto errSend = co_await bl::SockMustSend(sock, buf, nn);
	REQUIRE(errSend == 0);

	char buf2[128];
	auto errRead = co_await bl::SockMustRecv(sock, buf2, nn);
	REQUIRE(errRead == 0);
	std::cout << "Client recv[" << std::string_view(buf2, nn) << "]" << std::endl;
	REQUIRE(memcmp(buf, buf2, nn) == 0);

	co_await bl::TcpClose(sock);
}

TEST_CASE("test echo server ipv4") {
	BlInit(0, 0, 0);
	SOCKET sockListen = echoserver(AF_INET, 2);
	for (int i = 0; i < 2; ++i)
		bl::go(echoclient(AF_INET, i).get_handle());
	std::this_thread::sleep_for(std::chrono::seconds(1));
	//REQUIRE(BlCancelIo(sockListen) == 0);
	BlExitNotify();
	BlWaitExited();
	BlSockClose(sockListen);
}

TEST_CASE("test echo server ipv6") {
	BlInit(0, 0, 0);
	SOCKET sockListen = echoserver(AF_INET6, 2);
	for (int i = 0; i < 2; ++i)
		bl::go(echoclient(AF_INET6, i).get_handle());
	std::this_thread::sleep_for(std::chrono::seconds(1));
	//REQUIRE(BlCancelIo(sockListen) == 0);
	BlExitNotify();
	BlWaitExited();
	BlSockClose(sockListen);
}

TEST_CASE("test echo server unix domain socket") {
	BlInit(0, 0, 0);
	SOCKET sockListen = echoserver(AF_UNIX, 2);
	for (int i = 0; i < 2; ++i)
		bl::go(echoclient(AF_UNIX, i).get_handle());
	std::this_thread::sleep_for(std::chrono::seconds(1));
	//REQUIRE(BlCancelIo(sockListen) == 0);
	BlExitNotify();
	BlWaitExited();
	BlSockClose(sockListen);
}

bl::task<int, false> corotest1() {
	co_await bl::goIo();
	std::this_thread::sleep_for(std::chrono::seconds(1));
	std::cout << "corotest1: after sleep" << std::endl;
	co_return 1111;
}

bl::task<int, false> corotest2() {
	co_await bl::goOther();
	std::this_thread::sleep_for(std::chrono::seconds(1));
	std::cout << "corotest2: after sleep" << std::endl;
	co_return 22;
}

bl::task<void, false> corotest() {
	int x1 = co_await corotest1();
	std::cout << "corotest: corotest1 return " << x1 << std::endl;
	REQUIRE(x1 == 1111);
	int x2 = co_await corotest2();
	std::cout << "corotest: corotest2 return " << x2 << std::endl;
	REQUIRE(x2 == 22);
}

TEST_CASE("test co_await coroutine") {
	BlInit(0, 0, 0);
	corotest();
	std::this_thread::sleep_for(std::chrono::seconds(3));
	BlExitNotify();
	BlWaitExited();
}

bl::task<void> udp_receiver() {
	SOCKET sockR = BlUdpNewPeerReceiver(bl::SockAddr(43217), true);
	REQUIRE(sockR != INVALID_SOCKET);
	char buf[128];
	size_t n = sizeof(buf);
	bl::SockAddr addr(0,true);
	auto nRecv = co_await bl::SockRecvFrom(sockR, addr, buf, n);
	REQUIRE(nRecv > 0);
	std::cout << "Udp_receiver: received " << nRecv << " bytes from " << addr.to_str() << std::endl;
	auto nSend = co_await bl::SockSendTo(sockR, addr, buf, nRecv);
	REQUIRE(nSend >= 0);
	REQUIRE(nSend == nRecv);
	REQUIRE(0 == BlSockClose(sockR));
}

bl::task<void> udp_sender() {
	SOCKET sockS = BlUdpNewPeerSender(bl::SockAddr("127.0.0.1", 43217));
	REQUIRE(sockS != INVALID_SOCKET);
	char buf[128] = "Haha, this is sender speak!";
	size_t n = strlen(buf);
	int rev = BlSockPoll(sockS, POLLWRNORM, 0);
	REQUIRE(rev == POLLWRNORM);
	auto nSend = co_await bl::SockSend(sockS, buf, n);
	REQUIRE(nSend == n);
	char buf2[128];
	size_t n2 = sizeof(buf2);
	co_await bl::CoSleep(100);
	rev = BlSockPoll(sockS, POLLRDNORM, 0);
	REQUIRE((rev & (POLLERR | POLLRDNORM)) != 0);
	auto nRecv = co_await bl::SockRecv(sockS, buf2, n2);
	REQUIRE(nRecv == n);
	REQUIRE(memcmp(buf, buf2, n) == 0);
	REQUIRE(0 == BlSockClose(sockS));
	std::cout << "Udp_sender: received same content I have sent." << std::endl;
}

TEST_CASE("test udp 1") {
	BlInit(0, 0, 0);
	bl::go(udp_receiver().get_handle());
	bl::go(udp_sender().get_handle());
	std::this_thread::sleep_for(std::chrono::seconds(1));
	BlExitNotify();
	BlWaitExited();
}

bl::task<void> udp_receiver2() {
	SOCKET sockR = BlUdpNewPeerReceiver(bl::SockAddr(43217), true);
	REQUIRE(sockR != INVALID_SOCKET);
	char buf[128];
	bl::SockAddr addr(0, true);
	iovec bufs[2] = { { buf, 16 }, { buf+32, sizeof(buf)-32 }};
	auto nRecv = co_await bl::SockRecvVecFrom(sockR, addr, bufs, 2);
	REQUIRE(nRecv > 0);
	std::cout << "Udp_receiver2: received " << nRecv << " bytes from " << addr.to_str() << std::endl;
	bufs[1].iov_len = nRecv - bufs[0].iov_len;
	auto nSend = co_await bl::SockSendVecTo(sockR, addr, bufs, 2);
	REQUIRE(nSend == nRecv);
	REQUIRE(0 == BlSockClose(sockR));
}

static void CollectIoVec(char* dst, iovec* bufs, size_t bufCnt, size_t nTotal) {
	for (size_t i = 0; nTotal>0 && i < bufCnt; ++i) {
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

bl::task<void> udp_sender2() {
	SOCKET sockS = BlUdpNewPeerSender(bl::SockAddr("127.0.0.1", 43217));
	REQUIRE(sockS != INVALID_SOCKET);
	char buf1[] = "Haha, this is sender speak(part1)!";
	size_t n1 = strlen(buf1);
	char buf2[] = "Using scatter/gather i/o with iovec data here(part2)";
	size_t n2 = strlen(buf2);
	size_t n = n1 + n2;
	int rev = BlSockPoll(sockS, POLLWRNORM, 0);
	REQUIRE(rev == POLLWRNORM);
	iovec bufs[2] = { { buf1, n1 }, { buf2, n2 } };
	auto nSend = co_await bl::SockSendVec(sockS, bufs, 2);
	REQUIRE(nSend == n);
	char buf3[3];
	char buf4[7];
	char buf5[87];
	size_t n3 = sizeof(buf3);
	size_t n4 = sizeof(buf4);
	size_t n5 = sizeof(buf5);
	iovec bufs2[3] = { {buf3, n3}, {buf4, n4}, {buf5, n5} };
	co_await bl::CoSleep(100);
	rev = BlSockPoll(sockS, POLLRDNORM, 0);
	REQUIRE((rev & (POLLERR | POLLRDNORM)) != 0);
	auto nRecv = co_await bl::SockRecvVec(sockS, bufs2, 3);
	REQUIRE(nRecv == n);
	char tbuf1[128];
	char tbuf2[128];
	CollectIoVec(tbuf1, bufs, 2, n);
	CollectIoVec(tbuf2, bufs2, 3, nRecv);
	REQUIRE(memcmp(tbuf1, tbuf2, n) == 0);
	REQUIRE(0 == BlSockClose(sockS));
	std::cout << "Udp_sender2: received same content I have sent." << std::endl;
}

TEST_CASE("test udp 2") {
	BlInit(0, 0, 0);
	bl::go(udp_receiver2().get_handle());
	bl::go(udp_sender2().get_handle());
	std::this_thread::sleep_for(std::chrono::seconds(1));
	BlExitNotify();
	BlWaitExited();
}

TEST_CASE("test multicast address") {
	bl::SockAddr addr1("223.34.129.47", 0);
	REQUIRE(addr1.is_multicast() == false);
	bl::SockAddr addr2("239.34.129.47", 0);
	REQUIRE(addr2.is_multicast() == true);
	bl::SockAddr addr3("ff00::", 0);
	REQUIRE(addr3.is_multicast() == true);
	bl::SockAddr addr4("dead::", 0);
	REQUIRE(addr4.is_multicast() == false);
	std::cout << "Tested ok is_multicast()" << std::endl;
}

bl::task<void> udp_receiver3() {
	SOCKET sockR = BlUdpNewPeerReceiverEx(bl::SockAddr("224.34.129.38", 43217), nullptr, true);
	REQUIRE(sockR != INVALID_SOCKET);
	char buf[128];
	size_t n = sizeof(buf);
	bl::SockAddr addr(0, true);
	auto nRecv = co_await bl::SockRecvFrom(sockR, addr, buf, n);
	REQUIRE(nRecv > 0);
	std::cout << "Udp_receiver3: received " << nRecv << " bytes from " << addr.to_str() << std::endl;
	auto nSend = co_await bl::SockSendTo(sockR, addr, buf, nRecv);
	REQUIRE(nSend >= 0);
	REQUIRE(nSend == nRecv);
	REQUIRE(0 == BlSockClose(sockR));
	std::cout << "socket closed ok." << std::endl;
}

bl::task<void> udp_sender3() {
	SOCKET sockS = BlSockUdp(false);
	REQUIRE(sockS != INVALID_SOCKET);
	REQUIRE(0 == BlSockSetMulticastTtl(sockS, false, 128));
	REQUIRE(0==BlSockSetMulticastLoop(sockS, false, 1));
	char buf[128] = "Haha, this is sender speak!";
	size_t n = strlen(buf);
	//int rev = BlSockPoll(sockS, POLLWRNORM, 0);
	//REQUIRE(rev == POLLWRNORM);
	auto nSend = co_await bl::SockSendTo(sockS, bl::SockAddr("224.34.129.38", 43217), buf, n);
	REQUIRE(nSend == n);
	char buf2[128];
	size_t n2 = sizeof(buf2);
	co_await bl::CoSleep(100);
	int rev = BlSockPoll(sockS, POLLRDNORM, 0);
	REQUIRE((rev & (POLLERR | POLLRDNORM)) != 0);
	auto nRecv = co_await bl::SockRecv(sockS, buf2, n2);
	REQUIRE(nRecv == n);
	REQUIRE(memcmp(buf, buf2, n) == 0);
	REQUIRE(0 == BlSockClose(sockS));
	std::cout << "Udp_sender3: received same content I have sent." << std::endl;
}

TEST_CASE("test udp 3") {
	BlInit(0, 0, 0);
	bl::go(udp_receiver3().get_handle());
	bl::go(udp_sender3().get_handle());
	std::this_thread::sleep_for(std::chrono::seconds(1));
	BlExitNotify();
	BlWaitExited();
}

void OnTimer(const char* s) {
	time_t t = time(nullptr);
	tm* tm = gmtime(&t);
	std::cout << s << " @" << tm->tm_hour
		<< ":" << tm->tm_min << ":" << tm->tm_sec << std::endl;
}

void OnTimerDeleted(const char* s) {
	time_t t = time(nullptr);
	tm* tm = gmtime(&t);
	std::cout << s << " deleted @" << tm->tm_hour
		<< ":" << tm->tm_min << ":" << tm->tm_sec << std::endl;
}

TEST_CASE("test timer") {
	BlInit(0, 0, 0);
	REQUIRE(0 != BlAddTimer(3000, -2000, (BlTaskCallback)OnTimer, (void*)"Haha,Timer1", true));
	REQUIRE(0 != BlAddTimer(4000, 2000, (BlTaskCallback)OnTimer, (void*)"Haha,Timer2", false));
	uint64_t id3 = BlAddTimer(2000, 0, (BlTaskCallback)OnTimer, (void*)"Haha,Timer3", true);
	REQUIRE(0 != id3);
	std::this_thread::sleep_for(std::chrono::seconds(11));
	int r = BlDelTimer(id3, (BlTaskCallback)OnTimerDeleted, (void*)"timer3");
	std::cout << "BlDelTimer(id3) returns " << r << std::endl;
	REQUIRE(r>=0);
	std::this_thread::sleep_for(std::chrono::seconds(9));
	BlExitNotify();
	BlWaitExited();
}

static const char* fName = "./aaaabbbb.txt";
static const char* str1 = "Writing some xco data";
static const uint32_t str1len = strlen(str1);
static const char* str3 = " some xco HaloKittyTulip flo";
static const uint32_t str3len = strlen(str3);

bl::task<void> ReadWriteFile() {
	int f = BlFileOpen(fName, BLFILE_O_CREAT | BLFILE_O_RDWR, 0666);
	REQUIRE(f >= 0);
	auto nWrite = co_await bl::FileWrite(f, 0, str1, str1len);
	REQUIRE(nWrite == str1len);
	char buf[128];
	auto nRead = co_await bl::FileRead(f, 0, buf, str1len);
	REQUIRE(nRead == str1len);
	REQUIRE(0 == memcmp(buf, str1, str1len));
	struct iovec bufs1[] = { {(char*)"Halo", 4}, {(char*)"Kitty", 5}, {(char*)"Tulip flowers", 13} };
	nWrite = co_await bl::FileWriteVec(f, 17, bufs1, 3);
	REQUIRE(nWrite == 22);
	char buf21[6], buf22[3], buf23[2], buf24[4], buf25[13];
	struct iovec bufs2[] = { {buf21, sizeof(buf21)}, {buf22, sizeof(buf22)},
		{buf23, sizeof(buf23)}, {buf24, sizeof(buf24)}, {buf25, sizeof(buf25)} };
	nRead = co_await bl::FileReadVec(f, 7, bufs2, 5);
	REQUIRE(nRead == 28);
	CollectIoVec(buf, bufs2, 5, nRead);
	REQUIRE(0 == memcmp(buf, str3, str3len));
	BlFileClose(f);
	std::cout << "ReadWriteFile: read same content I have written." << std::endl;
}

TEST_CASE("test file") {
	BlInit(0, 0, 0);
	unlink(fName);
	bl::go(ReadWriteFile().get_handle());
	std::this_thread::sleep_for(std::chrono::seconds(1));
	BlExitNotify();
	BlWaitExited();
}
