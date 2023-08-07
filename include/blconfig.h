#pragma once

#define SUPPORT_AF_UNIX 1

#define BL_MAX_SOCKADDR_STR 112

#if defined(_MSC_VER) || (defined(__INTEL_COMPILER) && defined(_WIN32))
   #if defined(_M_X64)
      #define BL_BITNESS 64
   #else
      #define BL_BITNESS 32
   #endif
#elif defined(__clang__) || defined(__INTEL_COMPILER) || defined(__GNUC__)
   #if defined(__x86_64)
      #define BL_BITNESS 64
   #else
      #define BL_BITNESS 32
   #endif
#endif

#ifdef thread_local
    #define BL_THREAD_LOCAL thread_local
#elif __STDC_VERSION__ >= 201112 && !defined(__STDC_NO_THREADS__)
    #define BL_THREAD_LOCAL _Thread_local
#elif defined(_WIN32) && (defined(_MSC_VER) || defined(__ICL) ||  defined(__DMC__) ||  defined(__BORLANDC__))
    #define BL_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__SUNPRO_C) || defined(__xlC__)
    #define BL_THREAD_LOCAL __thread
#else
    #error Need support thread local variable
#endif

#if defined(_WIN32)
	#include "os/windows/blconfig_windows.h"
#elif defined(__linux__)
    #include "os/linux/blconfig_linux.h"
#else
	#error "Unsupported OS"
#endif
