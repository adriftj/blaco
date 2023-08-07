#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>  /* MAXHOSTNAMELEN on Solaris */

#include <termios.h>


/* fs open() flags supported on this platform: */
#if defined(O_APPEND)
# define UV_FS_O_APPEND       O_APPEND
#else
# define UV_FS_O_APPEND       0
#endif
#if defined(O_CREAT)
# define UV_FS_O_CREAT        O_CREAT
#else
# define UV_FS_O_CREAT        0
#endif

#if defined(__linux__) && defined(__arm__)
# define UV_FS_O_DIRECT       0x10000
#elif defined(__linux__) && defined(__m68k__)
# define UV_FS_O_DIRECT       0x10000
#elif defined(__linux__) && defined(__mips__)
# define UV_FS_O_DIRECT       0x08000
#elif defined(__linux__) && defined(__powerpc__)
# define UV_FS_O_DIRECT       0x20000
#elif defined(__linux__) && defined(__s390x__)
# define UV_FS_O_DIRECT       0x04000
#elif defined(__linux__) && defined(__x86_64__)
# define UV_FS_O_DIRECT       0x04000
#elif defined(O_DIRECT)
# define UV_FS_O_DIRECT       O_DIRECT
#else
# define UV_FS_O_DIRECT       0
#endif

#if defined(O_DIRECTORY)
# define UV_FS_O_DIRECTORY    O_DIRECTORY
#else
# define UV_FS_O_DIRECTORY    0
#endif
#if defined(O_DSYNC)
# define UV_FS_O_DSYNC        O_DSYNC
#else
# define UV_FS_O_DSYNC        0
#endif
#if defined(O_EXCL)
# define UV_FS_O_EXCL         O_EXCL
#else
# define UV_FS_O_EXCL         0
#endif
#if defined(O_EXLOCK)
# define UV_FS_O_EXLOCK       O_EXLOCK
#else
# define UV_FS_O_EXLOCK       0
#endif
#if defined(O_NOATIME)
# define UV_FS_O_NOATIME      O_NOATIME
#else
# define UV_FS_O_NOATIME      0
#endif
#if defined(O_NOCTTY)
# define UV_FS_O_NOCTTY       O_NOCTTY
#else
# define UV_FS_O_NOCTTY       0
#endif
#if defined(O_NOFOLLOW)
# define UV_FS_O_NOFOLLOW     O_NOFOLLOW
#else
# define UV_FS_O_NOFOLLOW     0
#endif
#if defined(O_NONBLOCK)
# define UV_FS_O_NONBLOCK     O_NONBLOCK
#else
# define UV_FS_O_NONBLOCK     0
#endif
#if defined(O_RDONLY)
# define UV_FS_O_RDONLY       O_RDONLY
#else
# define UV_FS_O_RDONLY       0
#endif
#if defined(O_RDWR)
# define UV_FS_O_RDWR         O_RDWR
#else
# define UV_FS_O_RDWR         0
#endif
#if defined(O_SYMLINK)
# define UV_FS_O_SYMLINK      O_SYMLINK
#else
# define UV_FS_O_SYMLINK      0
#endif
#if defined(O_SYNC)
# define UV_FS_O_SYNC         O_SYNC
#else
# define UV_FS_O_SYNC         0
#endif
#if defined(O_TRUNC)
# define UV_FS_O_TRUNC        O_TRUNC
#else
# define UV_FS_O_TRUNC        0
#endif
#if defined(O_WRONLY)
# define UV_FS_O_WRONLY       O_WRONLY
#else
# define UV_FS_O_WRONLY       0
#endif

/* fs open() flags supported on other platforms: */
#define UV_FS_O_FILEMAP       0
#define UV_FS_O_RANDOM        0
#define UV_FS_O_SHORT_LIVED   0
#define UV_FS_O_SEQUENTIAL    0
#define UV_FS_O_TEMPORARY     0
