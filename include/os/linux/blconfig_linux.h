#pragma once

#ifdef __cplusplus
#define INLINE inline
#else
	#if defined(__GNUC__)
		#if defined(__STRICT_ANSI__)
			#define INLINE __inline__ __attribute__((always_inline))
		#else
			#define INLINE inline __attribute__((always_inline))
		#endif
	#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#ifdef SUPPORT_AF_UNIX
#include <sys/un.h>
#endif

typedef char BlPathChar;
typedef char* SOCKPARM;
typedef int BlEvent;
typedef uint64_t UINT64;
#define INFINITE ((uint32_t)-1) 
#define E_PEER_CLOSED ENOSPC
#define BL_INVALID_EVENT ((int)-1)

#define BLACO_USING_IOURING 1

#ifdef __cplusplus
}
#endif
