#include "blaio.h"

int BlSockSetKeepAliveVals(SOCKET sock, bool onoff, uint32_t keepAliveTime, uint32_t keepAliveInterval) {
	int ret;
	int count;
	ret = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (SOCKPARM)&onoff, sizeof(onoff));
	if (ret < 0)
		return ret;

	count = 1;
	ret = setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, (SOCKPARM)&count, sizeof(count));
	if (ret < 0)
		return ret;

	keepAliveTime = keepAliveTime / 1000;
	if (keepAliveTime == 0)
		keepAliveTime = 1;
#if !defined(IOS6)
	ret = setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, (SOCKPARM)&keepAliveTime, sizeof(keepAliveTime));
	if (ret < 0)
		return ret;
#endif

	keepAliveInterval = keepAliveInterval / 1000;
	if (keepAliveInterval == 0)
		keepAliveInterval = 1;
	ret = setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, (SOCKPARM)&keepAliveInterval, sizeof(keepAliveInterval));
	return ret;
}

