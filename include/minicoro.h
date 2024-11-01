/*
Minimal asymmetric stackful cross-platform coroutine library in pure C.
minicoro - v0.1.3 - 27/Jan/2022
Eduardo Bart - edub4rt@gmail.com
https://github.com/edubart/minicoro

Modified by Alvin Jiang, 10/May/2023

Minicoro is single file library for using asymmetric coroutines in C.
The API is inspired by Lua coroutines but with C use in mind.

# Features

- Stackful asymmetric coroutines.
- Supports nesting coroutines (resuming a coroutine from another coroutine).
- Supports custom allocators.
- Storage system to allow passing values between yield and resume.
- Customizable stack size.
- Coroutine API design inspired by Lua with C use in mind.
- Yield across any C function.
- Made to work in multithread applications.
- Cross platform.
- Minimal, self contained and no external dependencies.
- Readable sources and documented.
- Implemented via assembly. # ucontext or fibers support was removed by Alvin.
- Lightweight and very efficient.
- Works in most C89 compilers.
- Error prone API, returning proper error codes on misuse.
- Support running with Valgrind, ASan (AddressSanitizer) and TSan (ThreadSanitizer).

# Supported Platforms

Most platforms are supported through different methods:

| Platform     | Assembly Method  | Fallback Method(removed by Alvin)     |
|--------------|------------------|---------------------------------------|
| Android      | ARM/ARM64        | N/A                                   |
| iOS          | ARM/ARM64        | N/A                                   |
| Windows      | x86_64           | Windows fibers                        |
| Linux        | x86_64/i686      | ucontext                              |
| Mac OS X     | x86_64/ARM/ARM64 | ucontext                              |
| WebAssembly  | N/A              | Emscripten fibers / Binaryen asyncify |
| Raspberry Pi | ARM              | ucontext                              |
| RISC-V       | rv64/rv32        | ucontext                              |

The assembly method is used by default if supported by the compiler and CPU,
otherwise ucontext or fiber method is used as a fallback.

The assembly method is very efficient, it just take a few cycles
to create, resume, yield or destroy a coroutine.

# Caveats

- Don't use coroutines with C++ exceptions, this is not supported.
- When using C++ RAII (i.e. destructors) you must resume the coroutine until it dies to properly execute all destructors.
- To use in multithread applications, you must compile with C compiler that supports `thread_local` qualifier.
- Some unsupported sanitizers for C may trigger false warnings when using coroutines.
- The `mco_coro` object is not thread safe, you should lock each coroutine into a thread.
- Stack space is fixed, it cannot grow. By default it has about 56KB of space, this can be changed on coroutine creation.
- Take care to not cause stack overflows (run out of stack space), otherwise your program may crash or not, the behavior is undefined.
- On WebAssembly you must compile with Emscripten flag `-s ASYNCIFY=1`.
- The WebAssembly Binaryen asyncify method can be used when explicitly enabled,
you may want to do this only to use minicoro with WebAssembly native interpreters
(no Web browser). This method is confirmed to work well with Emscripten toolchain,
however it fails on other WebAssembly toolchains like WASI SDK.

# Introduction

A coroutine represents an independent "green" thread of execution.
Unlike threads in multithread systems, however,
a coroutine only suspends its execution by explicitly calling a yield function.

You create a coroutine by calling `mco_create`.
Its sole argument is a `mco_desc` structure with a description for the coroutine.
The `mco_create` function only creates a new coroutine and returns a handle to it, it does not start the coroutine.

You execute a coroutine by calling `mco_resume`.
When calling a resume function the coroutine starts its execution by calling its body function.
After the coroutine starts running, it runs until it terminates or yields.

A coroutine yields by calling `mco_yield`.
When a coroutine yields, the corresponding resume returns immediately,
even if the yield happens inside nested function calls (that is, not in the main function).
The next time you resume the same coroutine, it continues its execution from the point where it yielded.

To associate a persistent value with the coroutine,
you can  optionally set `user_data` on its creation and later retrieve with `mco_get_user_data`.

To pass values between resume and yield,
you can optionally use `mco_push` and `mco_pop` APIs,
they are intended to pass temporary values using a LIFO style buffer.
The storage system can also be used to send and receive initial values on coroutine creation or before it finishes.

# Usage

This library is just have 2 files:
minicoro.h
minicoro.c

## Minimal Example

The following simple example demonstrates on how to use the library:

```c
#include "minicoro.h"
#include <stdio.h>

// Coroutine entry function.
void coro_entry(mco_coro* co) {
  printf("coroutine 1\n");
  mco_yield();
  printf("coroutine 2\n");
}

int main() {
  // First initialize a `desc` object through `mco_desc_init`.
  mco_desc desc = mco_desc_init(coro_entry, 0);
  // Configure `desc` fields when needed (e.g. customize user_data or allocation functions).
  desc.user_data = NULL;
  // Call `mco_create` with the output coroutine pointer and `desc` pointer.
  mco_coro* co;
  mco_result res = mco_create(&co, &desc);
  assert(res == MCO_SUCCESS);
  // The coroutine should be now in suspended state.
  assert(mco_status(co) == MCO_SUSPENDED);
  // Call `mco_resume` to start for the first time, switching to its context.
  res = mco_resume(co); // Should print "coroutine 1".
  assert(res == MCO_SUCCESS);
  // We get back from coroutine context in suspended state (because it's unfinished).
  assert(mco_status(co) == MCO_SUSPENDED);
  // Call `mco_resume` to resume for a second time.
  res = mco_resume(co); // Should print "coroutine 2".
  assert(res == MCO_SUCCESS);
  // The coroutine finished and should be now dead.
  assert(mco_status(co) == MCO_DEAD);
  // Call `mco_destroy` to destroy the coroutine.
  res = mco_destroy(co);
  assert(res == MCO_SUCCESS);
  return 0;
}
```

_NOTE_: In case you don't want to use the minicoro allocator system you should
allocate a coroutine object yourself using `mco_desc.coro_size` and call `mco_init`,
then later to destroy call `mco_uninit` and deallocate it.

## Yielding from anywhere

You can yield the current running coroutine from anywhere.

## Passing data between yield and resume

The library has the storage interface to assist passing data between yield and resume.
It's usage is straightforward,
use `mco_push` to send data before a `mco_resume` or `mco_yield`,
then later use `mco_pop` after a `mco_resume` or `mco_yield` to receive data.
Take care to not mismatch a push and pop, otherwise these functions will return
an error.

## Error handling

The library return error codes in most of its API in case of misuse or system error,
the user is encouraged to handle them properly.

## Library customization

The following can be defined to change the library behavior:

- `MCO_API`                   - Public API qualifier. Default is `extern`.
- `MCO_MIN_STACK_SIZE`        - Minimum stack size when creating a coroutine. Default is 32768.
- `MCO_DEFAULT_STORAGE_SIZE`  - Size of coroutine storage buffer. Default is 1024.
- `MCO_DEFAULT_STACK_SIZE`    - Default stack size when creating a coroutine. Default is 57344.
- `MCO_MALLOC`                - Default allocation function. Default is `malloc`.
- `MCO_FREE`                  - Default deallocation function. Default is `free`.
- `MCO_DEBUG`                 - Enable debug mode, logging any runtime error to stdout. Defined automatically unless `NDEBUG` or `MCO_NO_DEBUG` is defined.
- `MCO_NO_DEBUG`              - Disable debug mode.
- `MCO_NO_MULTITHREAD`        - Disable multithread usage. Multithread is supported when `thread_local` is supported.
- `MCO_NO_DEFAULT_ALLOCATORS` - Disable the default allocator using `MCO_MALLOC` and `MCO_FREE`.
- `MCO_USE_VALGRIND`          - Define if you want run with valgrind to fix accessing memory errors.

# Note on multithreading asynchonized I/O

In MT(multithreading) aynchronized I/O like Windows IOCP environment, one thread emit async I/O then
call mco_yield_mt, another thread will call mco_resume_mt after I/O completed, as following:
        Thread 1                  |         Thread 2
          ...                     |           ...
       coro = mco_running();      |
       start_async_io(IO, coro);  |
                                  |    {IO, coro} = received_aync_io_completed_event();
       mco_yield_mt(); // A       |    mco_resume_mt(coro); // B
         ...                      |           ...
Then, a race condition will take place. if A is executed earlier than B, there isn't any problem, `coro`
is be yield then be resumed normally. But if B is executed earlier than A, if mco_yield_mt and mco_resume_mt
do anything same as mco_yield and mco_resume, `coro` will be resumed first, BUT it's is running now and no
saved context for resuming! But don't worry, mco_yield_mt and mco_resume_mt DOES notice this race condition and
handle it appropriately.

# License

Your choice of either Public Domain or MIT No Attribution, see end of file.
*/


#ifndef MINICORO_H
#define MINICORO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Public API qualifier. */
#ifndef MCO_API
#define MCO_API extern
#endif

/* Size of coroutine storage buffer. */
#ifndef MCO_DEFAULT_STORAGE_SIZE
#define MCO_DEFAULT_STORAGE_SIZE 1024
#endif

#include <stddef.h> /* for size_t */

/* ---------------------------------------------------------------------------------------------- */

/* Coroutine states. */
typedef enum mco_state {
  MCO_DEAD = 0,  /* The coroutine has finished normally or was uninitialized before finishing. */
  MCO_NORMAL,    /* The coroutine is active but not running (that is, it has resumed another coroutine). */
  MCO_RUNNING,   /* The coroutine is active and running. */
  MCO_SUSPENDED  /* The coroutine is suspended (in a call to yield, or it has not started running yet). */
} mco_state;

/* Coroutine result codes. */
/* WARNING! if you modified this enum, you MUST modify
   the implementation of function mco_strerr in minicoro.c!!! */
typedef enum mco_result {
  MCO_SUCCESS = 0,
  MCO_GENERIC_ERROR,
  MCO_INVALID_POINTER,
  MCO_INVALID_COROUTINE,
  MCO_INVALID_STATUS,
  MCO_MAKE_CONTEXT_ERROR,
  MCO_SWITCH_CONTEXT_ERROR,
  MCO_OUT_OF_MEMORY,
  MCO_INVALID_ARGUMENTS,
  MCO_INVALID_OPERATION,
  MCO_STACK_OVERFLOW,
} mco_result;

const char* mco_strerr(mco_result r);

/* Coroutine structure. */
typedef struct mco_coro mco_coro;
struct mco_coro {
  void* context;
  mco_state state;
  void (*func)(mco_coro* co);
  mco_coro* prev_co;
  void* user_data;
  void* allocator_data;
  void (*free_cb)(void* ptr, void* allocator_data);
  void* stack_base; /* Stack base address, can be used to scan memory in a garbage collector. */
  size_t stack_size;
  unsigned char* storage;
  size_t bytes_stored;
  size_t storage_size;
  void* asan_prev_stack; /* Used by address sanitizer. */
  void* tsan_prev_fiber; /* Used by thread sanitizer. */
  void* tsan_fiber; /* Used by thread sanitizer. */
  size_t magic_number; /* Used to check stack overflow. */
};

/* Structure used to initialize a coroutine. */
typedef struct mco_desc {
  void (*func)(mco_coro* co); /* Entry point function for the coroutine. */
  void* user_data;            /* Coroutine user data, can be get with `mco_get_user_data`. */
  /* Custom allocation interface. */
  void* (*malloc_cb)(size_t size, void* allocator_data); /* Custom allocation function. */
  void  (*free_cb)(void* ptr, void* allocator_data);     /* Custom deallocation function. */
  void* allocator_data;       /* User data pointer passed to `malloc`/`free` allocation functions. */
  size_t storage_size;        /* Coroutine storage size, to be used with the storage APIs. */
  /* These must be initialized only through `mco_init_desc`. */
  size_t coro_size;           /* Coroutine structure size. */
  size_t stack_size;          /* Coroutine stack size. */
} mco_desc;

/* Coroutine functions. */
MCO_API mco_desc mco_desc_init(void (*func)(mco_coro* co), size_t stack_size);  /* Initialize description of a coroutine. When stack size is 0 then MCO_DEFAULT_STACK_SIZE is used. */
MCO_API mco_result mco_init(mco_coro* co, const mco_desc* desc);                /* Initialize the coroutine. */
MCO_API mco_result mco_uninit(mco_coro* co);                                    /* Uninitialize the coroutine, may fail if it's not dead or suspended. */
MCO_API mco_result mco_create(mco_coro** out_co, const mco_desc* desc);         /* Allocates and initializes a new coroutine. */
MCO_API mco_result mco_destroy(mco_coro* co);                                   /* Uninitialize and deallocate the coroutine, may fail if it's not dead or suspended. */
MCO_API mco_result mco_resume(mco_coro* co);                                    /* Starts or continues the execution of the coroutine. */
MCO_API mco_result mco_yield();                                                 /* Suspends the execution of running coroutine. */
MCO_API mco_result mco_resume_mt(mco_coro* co);                                 /* Starts or continues the execution of the coroutine. */
MCO_API mco_result mco_yield_mt();                                              /* Suspends the execution of running coroutine. */
MCO_API mco_state mco_status(const mco_coro* co);                               /* Returns the status of the coroutine. */
MCO_API const void* mco_get_user_data(const mco_coro* co);                      /* Get coroutine user data supplied on coroutine creation. */

/* Storage interface functions, used to pass values between yield and resume. */
MCO_API void* mco_push(mco_coro* co, size_t len, size_t align); /* Push bytes to the coroutine storage. Use to send values between yield and resume. */
MCO_API void* mco_pop(mco_coro* co, size_t len, size_t align);  /* Pop bytes from the coroutine storage. Use to get values between yield and resume. */
MCO_API void* mco_peek(mco_coro* co, size_t len, size_t align); /* Like `mco_pop` but it does not consumes the storage. */
MCO_API size_t mco_get_bytes_stored(mco_coro* co);              /* Get the available bytes that can be retrieved with a `mco_pop`. */
MCO_API size_t mco_get_storage_size(mco_coro* co);              /* Get the total storage size. */

/* Misc functions. */
MCO_API mco_coro* mco_running(void);                        /* Returns the running coroutine for the current thread. */

#ifdef __cplusplus
}
#endif

#endif /* MINICORO_H */
