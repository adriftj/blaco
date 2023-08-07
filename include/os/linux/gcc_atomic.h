#pragma once
#include <inttypes.h>

#define BLMO_RELAXED __ATOMIC_RELAXED
#define BLMO_CONSUME __ATOMIC_CONSUME
#define BLMO_ACQUIRE __ATOMIC_ACQUIRE
#define BLMO_RELEASE __ATOMIC_RELEASE
#define BLMO_ACQ_REL __ATOMIC_ACQ_REL
#define BLMO_SEQ_CST __ATOMIC_SEQ_CST

#define BlStore32Ex(P, D, O) __atomic_store_n((P), (D), (O))
#define BlStore64Ex(P, D, O) __atomic_store_n((P), (D), (O))
#define BlStorePtrEx(P, D, O) __atomic_store_n((P), (D), (O))

#define BlLoad32Ex(P, O) __atomic_load_n((P), (O))
#define BlLoad64Ex(P, O) __atomic_load_n((P), (O))
#define BlLoadPtrEx(P, O) __atomic_load_n((P), (O))

#define BlFas32Ex(P, D, O) __atomic_exchange_n((P), (D), (O))
#define BlFas64Ex(P, D, O) __atomic_exchange_n((P), (D), (O))
#define BlFasPtrEx(P, D, O) __atomic_exchange_n((P), (D), (O))

#define BlFetchAdd32Ex(P, A, O) __atomic_fetch_add((P), (A), (O))
#define BlFetchAdd64Ex(P, A, O) __atomic_fetch_add((P), (A), (O))

#define BlAddFetch32Ex(P, A, O) __atomic_add_fetch((P), (A), (O))
#define BlAddFetch64Ex(P, A, O) __atomic_add_fetch((P), (A), (O))

#define BlCas32WeakEx(P, E, D, S, F) \
  __atomic_compare_exchange_n((P), (E), (D), 1, (S), (F))
#define BlCas64WeakEx(P, E, D, S, F) \
  __atomic_compare_exchange_n((P), (E), (D), 1, (S), (F))
#define BlCasPtrWeakEx(P, E, D, S, F) \
  __atomic_compare_exchange_n((P), (E), (D), 1, (S), (F))

#define BlCas32StrongEx(P, E, D, S, F) \
  __atomic_compare_exchange_n((P), (E), (D), 0, (S), (F))
#define BlCas64StrongEx(P, E, D, S, F) \
  __atomic_compare_exchange_n((P), (E), (D), 0, (S), (F))
#define BlCasPtrStrongEx(P, E, D, S, F) \
  __atomic_compare_exchange_n((P), (E), (D), 0, (S), (F))

#define BlStore32(P, D) __atomic_store_n((P), (D), __ATOMIC_SEQ_CST)
#define BlStore64(P, D) __atomic_store_n((P), (D), __ATOMIC_SEQ_CST)
#define BlStorePtr(P, D) __atomic_store_n((P), (D), __ATOMIC_SEQ_CST)

#define BlLoad32(P) __atomic_load_n((P), __ATOMIC_SEQ_CST)
#define BlLoad64(P) __atomic_load_n((P), __ATOMIC_SEQ_CST)
#define BlLoadPtr(P) __atomic_load_n((P), __ATOMIC_SEQ_CST)

#define BlFas32(P, D) __atomic_exchange_n((P), (D), __ATOMIC_SEQ_CST)
#define BlFas64(P, D) __atomic_exchange_n((P), (D), __ATOMIC_SEQ_CST)
#define BlFasPtr(P, D) __atomic_exchange_n((P), (D), __ATOMIC_SEQ_CST)

#define BlFetchAdd32(P, A) __atomic_fetch_add((P), (A), __ATOMIC_SEQ_CST)
#define BlFetchAdd64(P, A) __atomic_fetch_add((P), (A), __ATOMIC_SEQ_CST)

#define BlAddFetch32(P, A) __atomic_add_fetch((P), (A), __ATOMIC_SEQ_CST)
#define BlAddFetch64(P, A) __atomic_add_fetch((P), (A), __ATOMIC_SEQ_CST)

#define BlCas32(P, E, D) \
  __atomic_compare_exchange_n((P), (E), (D), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define BlCas64(P, E, D) \
  __atomic_compare_exchange_n((P), (E), (D), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define BlCasPtr(P, E, D) \
  __atomic_compare_exchange_n((P), (E), (D), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

INLINE int32_t BlCas32V(int32_t volatile* a, int32_t cmp, int32_t set) {
    __atomic_compare_exchange_n(a, &cmp, set, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}

INLINE int64_t BlCas64V(int64_t volatile* a, int64_t cmp, int64_t set) {
    __atomic_compare_exchange_n(a, &cmp, set, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}

INLINE void* BlCasPtrV(void* volatile* a, void* cmp, void* set) {
    __atomic_compare_exchange_n(a, &cmp, set, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}

#define BlMemoryBarrier __sync_synchronize
