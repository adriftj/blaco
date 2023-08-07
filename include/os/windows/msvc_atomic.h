#pragma once

#include <windows.h>
#include <inttypes.h>

INLINE int BlCas32(int32_t volatile *a, int32_t *cmp, int32_t set) {
    int32_t initial_cmp= *cmp;
    int32_t initial_a= InterlockedCompareExchange((volatile LONG*)a, set, initial_cmp);
    int ret = (initial_a == initial_cmp);
    if (!ret)
        *cmp = initial_a;
    return ret;
}

INLINE int BlCas64(int64_t volatile *a, int64_t *cmp, int64_t set) {
    int64_t initial_cmp = *cmp;
    int64_t initial_a= InterlockedCompareExchange64((volatile LONGLONG*)a, (LONGLONG)set, (LONGLONG)initial_cmp);
    int ret = (initial_a == initial_cmp);
    if (!ret)
        *cmp= initial_a;
    return ret;
}

INLINE int BlCasPtr(void * volatile *a, void **cmp, void *set) {
    void *initial_cmp = *cmp;
    void *initial_a = InterlockedCompareExchangePointer(a, set, initial_cmp);
    int ret = (initial_a == initial_cmp);
    if (!ret)
        *cmp= initial_a;
    return ret;
}

INLINE int32_t BlCas32V(int32_t volatile* a, int32_t cmp, int32_t set) {
    return InterlockedCompareExchange((volatile LONG*)a, set, cmp);
}

INLINE int64_t BlCas64V(int64_t volatile* a, int64_t cmp, int64_t set) {
    return InterlockedCompareExchange64((volatile LONGLONG*)a, (LONGLONG)set, (LONGLONG)cmp);
}

INLINE void* BlCasPtrV(void* volatile* a, void* cmp, void* set) {
    return InterlockedCompareExchangePointer(a, set, cmp);
}

INLINE int32_t BlFetchAdd32(int32_t volatile *a, int32_t v) {
    return (int32_t)InterlockedExchangeAdd((volatile LONG*)a, v);
}

INLINE int64_t BlFetchAdd64(int64_t volatile *a, int64_t v) {
    return (int64_t)InterlockedExchangeAdd64((volatile LONGLONG*)a, (LONGLONG)v);
}

INLINE int32_t BlAddFetch32(int32_t volatile *a, int32_t v) {
    return (int32_t)InterlockedAdd((volatile LONG*)a, v);
}

INLINE int64_t BlAddFetch64(int64_t volatile *a, int64_t v) {
    return (int64_t)InterlockedAdd64((volatile LONGLONG*)a, (LONGLONG)v);
}

/*
Reads and writes to aligned 32-bit variables are atomic operations.
Reads and writes to aligned 64-bit variables are atomic on 64-bit Windows.
Reads and writes to 64-bit values are not guaranteed to be atomic on 32-bit Windows.
see https://msdn.microsoft.com/en-us/library/windows/desktop/ms684122(v=vs.85).aspx
*/
INLINE int32_t BlLoad32(int32_t volatile *a) {
    int32_t value = *a;
    MemoryBarrier();
    return value;
}

INLINE int64_t BlLoad64(int64_t volatile *a) {
#ifdef _M_X64
    int64_t value = *a;
    MemoryBarrier();
    return value;
#else
    return (int64_t)InterlockedCompareExchange64((volatile LONGLONG *) a, 0, 0);
#endif
}

INLINE void* BlLoadPtr(void * volatile *a) {
    void *value = *a;
    MemoryBarrier();
    return value;
}

INLINE int32_t BlFas32(int32_t volatile *a, int32_t v) {
    return (int32_t)InterlockedExchange((volatile LONG*)a, v);
}

INLINE int64_t BlFas64(int64_t volatile *a, int64_t v) {
    return (int64_t)InterlockedExchange64((volatile LONGLONG*)a, v);
}

INLINE void * BlFasPtr(void * volatile *a, void * v) {
    return InterlockedExchangePointer(a, v);
}

INLINE void BlStore32(int32_t volatile *a, int32_t v) {
    MemoryBarrier();
    *a = v;
}

INLINE void BlStore64(int64_t volatile *a, int64_t v) {
#ifdef _M_X64
    MemoryBarrier();
    *a = v;
#else
    InterlockedExchange64((volatile LONGLONG *)a, v);
#endif
}

INLINE void BlMemoryBarrier() {
#if BL_BITNESS==64
    MemoryBarrier();
#else
    __asm mfence;
#endif
}

INLINE void BlStorePtr(void * volatile *a, void *v) {
    BlMemoryBarrier();
    *a = v;
}
