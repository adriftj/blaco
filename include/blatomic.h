#pragma once

#include "blconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
This header defines five atomic operations:

BlFetchAdd#(&var, what)
BlFetchAdd#Ex(&var, what, memory_order)
'Fetch and Add'
add 'what' to *var, and return the old value of *var
All memory orders are valid.

BlAddFetch#(&var, what)
BlAddFetch#Ex(&var, what, memory_order)
'Add and Fetch'
add 'what' to *var, and return the added value of *var
All memory orders are valid.

BlFas#(&var, what)
BlFas#Ex(&var, what, memory_order)
'Fetch And Store'
store 'what' in *var, and return the old value of *var
All memory orders are valid.

BlCas#(&var, &old, new)
BlCas#WeakEx(&var, &old, new, succ, fail)
BlCas#StrongEx(&var, &old, new, succ, fail)
'Compare And Swap'
if *var is equal to *old, then store 'new' in *var, and return TRUE
otherwise store *var in *old, and return FALSE
succ - the memory synchronization ordering for the read-modify-write
operation if the comparison succeeds. All memory orders are valid.
fail - the memory synchronization ordering for the load operation if the
comparison fails. Cannot be BLMO_RELEASE or
BLMO_ACQ_REL and cannot specify stronger ordering than succ.

The weak form is allowed to fail spuriously, that is, act as if *var != *old
even if they are equal. When a compare-and-exchange is in a loop, the weak
version will yield better performance on some platforms. When a weak
compare-and-exchange would require a loop and a strong one would not, the
strong one is preferable.

BlCas#V(&var, cmp, set)

BlLoad#(&var)
BlLoad#Ex(&var, memory_order)
return *var
Order must be one of BLMO_RELAXED, BLMO_CONSUME, BLMO_ACQUIRE, BLMO_SEQ_CST.

BlStore#(&var, what)
BlStore#Ex(&var, what, memory_order)
store 'what' in *var
Order must be one of BLMO_RELAXED, BLMO_RELEASE, BLMO_SEQ_CST.

'#' is substituted by a size suffix 32, 64, or Ptr(e.g. BlAdd32, BlFas64, BlCasPtr).

The first version orders memory accesses according to BLMO_SEQ_CST,
the second version (with Ex suffix) orders memory accesses according to
given memory order.

memory_order specifies how non-atomic memory accesses are to be ordered around an atomic operation:

BLMO_RELAXED - there are no constraints on reordering of memory
                        accesses around the atomic variable.
BLMO_CONSUME - no reads in the current thread dependent on the
                        value currently loaded can be reordered before this
                        load. This ensures that writes to dependent
                        variables in other threads that release the same
                        atomic variable are visible in the current thread.
                        On most platforms, this affects compiler
                        optimization only.
BLMO_ACQUIRE - no reads in the current thread can be reordered
                        before this load. This ensures that all writes in
                        other threads that release the same atomic variable
                        are visible in the current thread.
BLMO_RELEASE - no writes in the current thread can be reordered
                        after this store. This ensures that all writes in
                        the current thread are visible in other threads that
                        acquire the same atomic variable.
BLMO_ACQ_REL - no reads in the current thread can be reordered
                        before this load as well as no writes in the current
                        thread can be reordered after this store. The
                        operation is read-modify-write operation. It is
                        ensured that all writes in another threads that
                        release the same atomic variable are visible before
                        the modification and the modification is visible in
                        other threads that acquire the same atomic variable.
BLMO_SEQ_CST - The operation has the same semantics as
                        acquire-release operation, and additionally has
                        sequentially-consistent operation ordering.

We choose implementation as follows: on Windows using Visual C++ the native
implementation should be preferable. When using gcc we prefer the Solaris
implementation before the gcc because of stability preference, we choose gcc
builtins if available.
*/

#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__ || defined __APPLE__
#define BL_GCC_ATOMIC 1
#elif defined _MSC_VER
#define BL_GCC_ATOMIC 0
#else
#error "Unsupported compiler"
#endif

#if BL_GCC_ATOMIC
#include "os/linux/gcc_atomic.h"
#else
#include "os/windows/msvc_atomic.h"
#endif

#ifndef BLMO_SEQ_CST
#define BLMO_RELAXED
#define BLMO_CONSUME
#define BLMO_ACQUIRE
#define BLMO_RELEASE
#define BLMO_ACQ_REL
#define BLMO_SEQ_CST

#define BlStore32Ex(P, D, O) BlStore32((P), (D))
#define BlStore64Ex(P, D, O) BlStore64((P), (D))
#define BlStorePtrEx(P, D, O) BlStorePtr((P), (D))

#define BlLoad32Ex(P, O) BlLoad32((P))
#define BlLoad64Ex(P, O) BlLoad64((P))
#define BlLoadPtrEx(P, O) BlLoadPtr((P))

#define BlFas32Ex(P, D, O) BlFas32((P), (D))
#define BlFas64Ex(P, D, O) BlFas64((P), (D))
#define BlFasPtrEx(P, D, O) BlFasPtr((P), (D))

#define BlFetchAdd32Ex(P, A, O) BlFetchAdd32((P), (A))
#define BlFetchAdd64Ex(P, A, O) BlFetchAdd64((P), (A))

#define BlAddFetch32Ex(P, A, O) BlAddFetch32((P), (A))
#define BlAddFetch64Ex(P, A, O) BlAddFetch64((P), (A))

#define BlCas32WeakEx(P, E, D, S, F)  BlCas32((P), (E), (D))
#define BlCas64WeakEx(P, E, D, S, F)  BlCas64((P), (E), (D))
#define BlCasPtrWeakEx(P, E, D, S, F)  BlCasPtr((P), (E), (D))

#define BlCas32StrongEx(P, E, D, S, F) BlCas32((P), (E), (D))
#define BlCas64StrongEx(P, E, D, S, F) BlCas64((P), (E), (D))
#define BlCasPtrStrongEx(P, E, D, S, F) BlCasPtr((P), (E), (D))
#endif /* !defined(BLMO_SEQ_CST) */

#if BL_BITNESS==64
    #define BlLoadInt BlLoad64
    #define BlStoreInt BlStore64
    #define BlCasIntV BlCas64V
    #define BlFasInt BlFas64
    #define BlFetchAdd BlFetchAdd64
    #define BlAddFetch BlAddFetch64
#else
    #define BlLoadInt BlLoad32
    #define BlStoreInt BlStore32
    #define BlCasIntV BlCas32V
    #define BlFasInt BlFas32
    #define BlFetchAdd BlFetchAdd32
    #define BlAddFetch BlAddFetch32
#endif

#ifdef __cplusplus
}
#endif
