#pragma once

#define INLINE inline

#ifdef __cplusplus
extern "C" {
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>
#include <mstcpip.h>
#ifdef SUPPORT_AF_UNIX
#include <afunix.h>
#endif

typedef char* SOCKPARM;
typedef unsigned short sa_family_t;
typedef HANDLE BlEvent;

#define EAI_SYSTEM EAI_FAIL
#define E_PEER_CLOSED ERROR_NO_MORE_FILES
#define BL_INVALID_EVENT NULL

#define BLACO_USING_IOCP 1

#ifdef __cplusplus
}
#endif
