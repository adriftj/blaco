#include "blatomic.h"
#include "minicoro.h"

#ifdef __cplusplus
extern "C" {
#endif

const char* mco_strerr(mco_result r) {
    static const char* errs[] = {
        "SUCCESS",
        "GENERIC_ERROR",
        "INVALID_POINTER",
        "INVALID_COROUTINE",
        "INVALID_STATUS",
        "MAKE_CONTEXT_ERROR",
        "SWITCH_CONTEXT_ERROR",
        "OUT_OF_MEMORY",
        "INVALID_ARGUMENTS",
        "INVALID_OPERATION",
        "STACK_OVERFLOW"
    };
    if (r >= 0 && r <= sizeof(errs) / sizeof(errs[0]))
        return errs[r];
    return "NO_THIS_ERROR_NUMBER";
}

/* Minimum stack size when creating a coroutine. */
#ifndef MCO_MIN_STACK_SIZE
#define MCO_MIN_STACK_SIZE 32768
#endif

/* Default stack size when creating a coroutine. */
#ifndef MCO_DEFAULT_STACK_SIZE
#define MCO_DEFAULT_STACK_SIZE 57344 /* Don't use multiples of 64K to avoid D-cache aliasing conflicts. */
#endif

/* Number used only to assist checking for stack overflows. */
#define MCO_MAGIC_NUMBER 0x7E3CB1A9

#define _MCO_UNUSED(x) (void)(x)

#if !defined(MCO_NO_DEBUG) && !defined(NDEBUG) && !defined(MCO_DEBUG)
#define MCO_DEBUG
#endif

#ifndef MCO_LOG
  #ifdef MCO_DEBUG
    #include <stdio.h>
    #define MCO_LOG(s) puts(s)
  #else
    #define MCO_LOG(s)
  #endif
#endif

#ifndef MCO_ASSERT
  #ifdef MCO_DEBUG
    #include <assert.h>
    #define MCO_ASSERT(c) assert(c)
  #else
    #define MCO_ASSERT(c)
  #endif
#endif

#ifndef MCO_THREAD_LOCAL
  #ifdef MCO_NO_MULTITHREAD
    #define MCO_THREAD_LOCAL
  #else
    #ifdef thread_local
      #define MCO_THREAD_LOCAL thread_local
    #elif __STDC_VERSION__ >= 201112 && !defined(__STDC_NO_THREADS__)
      #define MCO_THREAD_LOCAL _Thread_local
    #elif defined(_WIN32) && (defined(_MSC_VER) || defined(__ICL) ||  defined(__DMC__) ||  defined(__BORLANDC__))
      #define MCO_THREAD_LOCAL __declspec(thread)
    #elif defined(__GNUC__) || defined(__SUNPRO_C) || defined(__xlC__)
      #define MCO_THREAD_LOCAL __thread
    #else /* No thread local support, `mco_running` will be thread unsafe. */
      #define MCO_THREAD_LOCAL
      #define MCO_NO_MULTITHREAD
    #endif
  #endif
#endif

#ifndef MCO_FORCE_INLINE
  #ifdef _MSC_VER
    #define MCO_FORCE_INLINE __forceinline
  #elif defined(__GNUC__)
    #if defined(__STRICT_ANSI__)
      #define MCO_FORCE_INLINE __inline__ __attribute__((always_inline))
    #else
      #define MCO_FORCE_INLINE inline __attribute__((always_inline))
    #endif
  #elif defined(__BORLANDC__) || defined(__DMC__) || defined(__SC__) || defined(__WATCOMC__) || defined(__LCC__) ||  defined(__DECC)
    #define MCO_FORCE_INLINE __inline
  #else /* No inline support. */
    #define MCO_FORCE_INLINE
  #endif
#endif

#ifndef MCO_NO_INLINE
  #ifdef __GNUC__
    #define MCO_NO_INLINE __attribute__((noinline))
  #elif defined(_MSC_VER)
    #define MCO_NO_INLINE __declspec(noinline)
  #else
    #define MCO_NO_INLINE
  #endif
#endif

#ifndef MCO_NO_DEFAULT_ALLOCATORS
#ifndef MCO_MALLOC
  #include <stdlib.h>
  #define MCO_MALLOC malloc
  #define MCO_FREE free
#endif
static void* mco_malloc(size_t size, void* allocator_data) {
  _MCO_UNUSED(allocator_data);
  return MCO_MALLOC(size);
}
static void mco_free(void* ptr, void* allocator_data) {
  _MCO_UNUSED(allocator_data);
  MCO_FREE(ptr);
}
#endif /* MCO_NO_DEFAULT_ALLOCATORS */

#if defined(__has_feature)
  #if __has_feature(address_sanitizer)
    #define _MCO_USE_ASAN
  #endif
  #if __has_feature(thread_sanitizer)
    #define _MCO_USE_TSAN
  #endif
#endif
#if defined(__SANITIZE_ADDRESS__)
  #define _MCO_USE_ASAN
#endif
#if defined(__SANITIZE_THREAD__)
  #define _MCO_USE_TSAN
#endif
#ifdef _MCO_USE_ASAN
void __sanitizer_start_switch_fiber(void** fake_stack_save, const void *bottom, size_t size);
void __sanitizer_finish_switch_fiber(void* fake_stack_save, const void **bottom_old, size_t *size_old);
#endif
#ifdef _MCO_USE_TSAN
void* __tsan_get_current_fiber(void);
void* __tsan_create_fiber(unsigned flags);
void __tsan_destroy_fiber(void* fiber);
void __tsan_switch_to_fiber(void* fiber, unsigned flags);
#endif

#include <string.h> /* For memcpy and memset. */

/* Utility for aligning addresses. */
static MCO_FORCE_INLINE size_t _mco_align_forward(size_t addr, size_t align) {
  return (addr + (align-1)) & ~(align-1);
}

/* Variable holding the current running coroutine per thread. */
static MCO_THREAD_LOCAL mco_coro* mco_current_co = NULL;

static MCO_FORCE_INLINE mco_coro* _mco_prepare_jumpin(mco_coro* co) {
  /* Set the old coroutine to normal state and update it. */
  mco_coro* prev_co = mco_running(); /* Must access through `mco_running`. */
  MCO_ASSERT(co->prev_co == NULL);
  co->prev_co = prev_co;
  if(prev_co) {
    MCO_ASSERT(prev_co->state == MCO_RUNNING);
    prev_co->state = MCO_NORMAL;
  }
  mco_current_co = co;
#ifdef _MCO_USE_ASAN
  if(prev_co) {
    void* bottom_old = NULL;
    size_t size_old = 0;
    __sanitizer_finish_switch_fiber(prev_co->asan_prev_stack, (const void**)&bottom_old, &size_old);
    prev_co->asan_prev_stack = NULL;
  }
  __sanitizer_start_switch_fiber(&co->asan_prev_stack, co->stack_base, co->stack_size);
#endif
#ifdef _MCO_USE_TSAN
  co->tsan_prev_fiber = __tsan_get_current_fiber();
  __tsan_switch_to_fiber(co->tsan_fiber, 0);
#endif
  return prev_co;
}

static MCO_FORCE_INLINE mco_coro* _mco_prepare_jumpout(mco_coro* co) {
  /* Switch back to the previous running coroutine. */
  /* MCO_ASSERT(mco_running() == co); */
  mco_coro* prev_co = co->prev_co;
  co->prev_co = NULL;
  if(prev_co) {
    /* MCO_ASSERT(prev_co->state == MCO_NORMAL); */
    prev_co->state = MCO_RUNNING;
  }
  mco_current_co = prev_co;
#ifdef _MCO_USE_ASAN
  void* bottom_old = NULL;
  size_t size_old = 0;
  __sanitizer_finish_switch_fiber(co->asan_prev_stack, (const void**)&bottom_old, &size_old);
  co->asan_prev_stack = NULL;
  if(prev_co) {
    __sanitizer_start_switch_fiber(&prev_co->asan_prev_stack, bottom_old, size_old);
  }
#endif
#ifdef _MCO_USE_TSAN
  void* tsan_prev_fiber = co->tsan_prev_fiber;
  co->tsan_prev_fiber = NULL;
  __tsan_switch_to_fiber(tsan_prev_fiber, 0);
#endif
  return prev_co;
}

static void _mco_jumpin(mco_coro* co);
static void _mco_jumpout(mco_coro* co);

static MCO_NO_INLINE void _mco_main(mco_coro* co) {
  co->func(co); /* Run the coroutine function. */
  co->state = MCO_DEAD; /* Coroutine finished successfully, set state to dead. */
  _mco_jumpout(co); /* Jump back to the old context .*/
}

#if defined(__x86_64__) || defined(_M_X64)

#ifdef _WIN32

typedef struct _mco_ctxbuf {
  void *rip, *rsp, *rbp, *rbx, *r12, *r13, *r14, *r15, *rdi, *rsi;
  void* xmm[20]; /* xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15 */
  void* fiber_storage;
  void* dealloc_stack;
  void* stack_limit;
  void* stack_base;
} _mco_ctxbuf;

#if defined(__GNUC__)
#define _MCO_ASM_BLOB __attribute__((section(".text")))
#elif defined(_MSC_VER)
#define _MCO_ASM_BLOB __declspec(allocate(".text"))
#pragma section(".text")
#endif

_MCO_ASM_BLOB static unsigned char _mco_wrap_main_code[] = {
  0x4c, 0x89, 0xe9,                                       /* mov    %r13,%rcx */
  0x41, 0xff, 0xe4,                                       /* jmpq   *%r12 */
  0xc3,                                                   /* retq */
  0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90    /* nop */
};

#define _MCO_SWITCH_CODE_SAVE_CTXBUF \
0x48, 0x89, 0x01,                                     /* mov    %rax,(%rcx)         */\
0x48, 0x89, 0x61, 0x08,                               /* mov    %rsp,0x8(%rcx)      */\
0x48, 0x89, 0x69, 0x10,                               /* mov    %rbp,0x10(%rcx)     */\
0x48, 0x89, 0x59, 0x18,                               /* mov    %rbx,0x18(%rcx)     */\
0x4c, 0x89, 0x61, 0x20,                               /* mov    %r12,0x20(%rcx)     */\
0x4c, 0x89, 0x69, 0x28,                               /* mov    %r13,0x28(%rcx)     */\
0x4c, 0x89, 0x71, 0x30,                               /* mov    %r14,0x30(%rcx)     */\
0x4c, 0x89, 0x79, 0x38,                               /* mov    %r15,0x38(%rcx)     */\
0x48, 0x89, 0x79, 0x40,                               /* mov    %rdi,0x40(%rcx)     */\
0x48, 0x89, 0x71, 0x48,                               /* mov    %rsi,0x48(%rcx)     */\
0x0f, 0x11, 0x71, 0x50,                               /* movups %xmm6,0x50(%rcx)    */\
0x0f, 0x11, 0x79, 0x60,                               /* movups %xmm7,0x60(%rcx)    */\
0x44, 0x0f, 0x11, 0x41, 0x70,                         /* movups %xmm8,0x70(%rcx)    */\
0x44, 0x0f, 0x11, 0x89, 0x80, 0x00, 0x00, 0x00,       /* movups %xmm9,0x80(%rcx)    */\
0x44, 0x0f, 0x11, 0x91, 0x90, 0x00, 0x00, 0x00,       /* movups %xmm10,0x90(%rcx)   */\
0x44, 0x0f, 0x11, 0x99, 0xa0, 0x00, 0x00, 0x00,       /* movups %xmm11,0xa0(%rcx)   */\
0x44, 0x0f, 0x11, 0xa1, 0xb0, 0x00, 0x00, 0x00,       /* movups %xmm12,0xb0(%rcx)   */\
0x44, 0x0f, 0x11, 0xa9, 0xc0, 0x00, 0x00, 0x00,       /* movups %xmm13,0xc0(%rcx)   */\
0x44, 0x0f, 0x11, 0xb1, 0xd0, 0x00, 0x00, 0x00,       /* movups %xmm14,0xd0(%rcx)   */\
0x44, 0x0f, 0x11, 0xb9, 0xe0, 0x00, 0x00, 0x00,       /* movups %xmm15,0xe0(%rcx)   */\
0x65, 0x4c, 0x8b, 0x14, 0x25, 0x30, 0x00, 0x00, 0x00, /* mov    %gs:0x30,%r10       */\
0x49, 0x8b, 0x42, 0x20,                               /* mov    0x20(%r10),%rax     */\
0x48, 0x89, 0x81, 0xf0, 0x00, 0x00, 0x00,             /* mov    %rax,0xf0(%rcx)     */\
0x49, 0x8b, 0x82, 0x78, 0x14, 0x00, 0x00,             /* mov    0x1478(%r10),%rax   */\
0x48, 0x89, 0x81, 0xf8, 0x00, 0x00, 0x00,             /* mov    %rax,0xf8(%rcx)     */\
0x49, 0x8b, 0x42, 0x10,                               /* mov    0x10(%r10),%rax     */\
0x48, 0x89, 0x81, 0x00, 0x01, 0x00, 0x00,             /* mov    %rax,0x100(%rcx)    */\
0x49, 0x8b, 0x42, 0x08,                               /* mov    0x8(%r10),%rax      */\
0x48, 0x89, 0x81, 0x08, 0x01, 0x00, 0x00,             /* mov    %rax,0x108(%rcx)    */
// above 164 bytes

#define _MCO_SWITCH_CODE_LOAD_CTXBUF \
0x48, 0x8b, 0x82, 0x08, 0x01, 0x00, 0x00,             /* mov    0x108(%rdx),%rax    */\
0x49, 0x89, 0x42, 0x08,                               /* mov    %rax,0x8(%r10)      */\
0x48, 0x8b, 0x82, 0x00, 0x01, 0x00, 0x00,             /* mov    0x100(%rdx),%rax    */\
0x49, 0x89, 0x42, 0x10,                               /* mov    %rax,0x10(%r10)     */\
0x48, 0x8b, 0x82, 0xf8, 0x00, 0x00, 0x00,             /* mov    0xf8(%rdx),%rax     */\
0x49, 0x89, 0x82, 0x78, 0x14, 0x00, 0x00,             /* mov    %rax,0x1478(%r10)   */\
0x48, 0x8b, 0x82, 0xf0, 0x00, 0x00, 0x00,             /* mov    0xf0(%rdx),%rax     */\
0x49, 0x89, 0x42, 0x20,                               /* mov    %rax,0x20(%r10)     */\
0x44, 0x0f, 0x10, 0xba, 0xe0, 0x00, 0x00, 0x00,       /* movups 0xe0(%rdx),%xmm15   */\
0x44, 0x0f, 0x10, 0xb2, 0xd0, 0x00, 0x00, 0x00,       /* movups 0xd0(%rdx),%xmm14   */\
0x44, 0x0f, 0x10, 0xaa, 0xc0, 0x00, 0x00, 0x00,       /* movups 0xc0(%rdx),%xmm13   */\
0x44, 0x0f, 0x10, 0xa2, 0xb0, 0x00, 0x00, 0x00,       /* movups 0xb0(%rdx),%xmm12   */\
0x44, 0x0f, 0x10, 0x9a, 0xa0, 0x00, 0x00, 0x00,       /* movups 0xa0(%rdx),%xmm11   */\
0x44, 0x0f, 0x10, 0x92, 0x90, 0x00, 0x00, 0x00,       /* movups 0x90(%rdx),%xmm10   */\
0x44, 0x0f, 0x10, 0x8a, 0x80, 0x00, 0x00, 0x00,       /* movups 0x80(%rdx),%xmm9    */\
0x44, 0x0f, 0x10, 0x42, 0x70,                         /* movups 0x70(%rdx),%xmm8    */\
0x0f, 0x10, 0x7a, 0x60,                               /* movups 0x60(%rdx),%xmm7    */\
0x0f, 0x10, 0x72, 0x50,                               /* movups 0x50(%rdx),%xmm6    */\
0x48, 0x8b, 0x72, 0x48,                               /* mov    0x48(%rdx),%rsi     */\
0x48, 0x8b, 0x7a, 0x40,                               /* mov    0x40(%rdx),%rdi     */\
0x4c, 0x8b, 0x7a, 0x38,                               /* mov    0x38(%rdx),%r15     */\
0x4c, 0x8b, 0x72, 0x30,                               /* mov    0x30(%rdx),%r14     */\
0x4c, 0x8b, 0x6a, 0x28,                               /* mov    0x28(%rdx),%r13     */\
0x4c, 0x8b, 0x62, 0x20,                               /* mov    0x20(%rdx),%r12     */\
0x48, 0x8b, 0x5a, 0x18,                               /* mov    0x18(%rdx),%rbx     */\
0x48, 0x8b, 0x6a, 0x10,                               /* mov    0x10(%rdx),%rbp     */\
0x48, 0x8b, 0x62, 0x08,                               /* mov    0x8(%rdx),%rsp      */
// above 152 bytes

_MCO_ASM_BLOB static unsigned char _mco_switch_code1[] = {
  0x48, 0x8d, 0x05, 0x3e, 0x01, 0x00, 0x00, /* lea    0x13e(%rip),%rax // rax <- offset L1 */
  _MCO_SWITCH_CODE_SAVE_CTXBUF
  _MCO_SWITCH_CODE_LOAD_CTXBUF
  0xff, 0x22,                               /* jmpq *(%rdx)               */
  0xc3,                                     /* L1: retq                   */
  0x90, 0x90                                /* nop                        */
};

_MCO_ASM_BLOB static unsigned char _mco_switch_code2[] = {
  0x48, 0x8d, 0x05, 0x4f, 0x01, 0x00, 0x00, /* lea 0x14f(%rip),%rax // rax <- offset L1 */
  _MCO_SWITCH_CODE_SAVE_CTXBUF

  0xb8, 0x03, 0x00, 0x00, 0x00,             /* mov $3,%eax //3==MCO_SUSPENDED*/
  0x41, 0x87, 0x00,                         /* xchg %eax,(%r8)               */
  0x83, 0xf8, 0x03,                         /* cmp %eax, $3                  */
  0x0f, 0x84, 0x9c, 0x00, 0x00, 0x00,       /* je L2                         */

  _MCO_SWITCH_CODE_LOAD_CTXBUF
  0xff, 0x22,                               /* jmpq *(%rdx)               */
  0x31, 0xc0,                               /* L1: xor %eax, %eax         */
  0xc3,                                     /* L2: retq                   */
  0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90  /* nop                        */
};

void (*_mco_wrap_main)(void) = (void(*)(void))(void*)_mco_wrap_main_code;
void (*_mco_switch1)(_mco_ctxbuf* from, _mco_ctxbuf* to) = (void(*)(_mco_ctxbuf* from, _mco_ctxbuf* to))(void*)_mco_switch_code1;
int (*_mco_switch2)(_mco_ctxbuf* from, _mco_ctxbuf* to, mco_state* p) = (int(*)(_mco_ctxbuf * from, _mco_ctxbuf * to, mco_state*))(void*)_mco_switch_code2;

static mco_result _mco_makectx(mco_coro* co, _mco_ctxbuf* ctx, void* stack_base, size_t stack_size) {
  stack_size = stack_size - 32; /* Reserve 32 bytes for the shadow space. */
  void** stack_high_ptr = (void**)((size_t)stack_base + stack_size - sizeof(size_t));
  stack_high_ptr[0] = (void*)(0xdeaddeaddeaddead);  /* Dummy return address. */
  ctx->rip = (void*)(_mco_wrap_main);
  ctx->rsp = (void*)(stack_high_ptr);
  ctx->r12 = (void*)(_mco_main);
  ctx->r13 = (void*)(co);
  void* stack_top = (void*)((size_t)stack_base + stack_size);
  ctx->stack_base = stack_top;
  ctx->stack_limit = stack_base;
  ctx->dealloc_stack = stack_base;
  return MCO_SUCCESS;
}

#else /* not _WIN32 */

typedef struct _mco_ctxbuf {
  void *rip, *rsp, *rbp, *rbx, *r12, *r13, *r14, *r15;
} _mco_ctxbuf;

void _mco_wrap_main(void);
void _mco_switch1(_mco_ctxbuf* from, _mco_ctxbuf* to);
int _mco_switch2(_mco_ctxbuf* from, _mco_ctxbuf* to, mco_state* p);

__asm__(
  ".text\n"
#ifdef __MACH__ /* Mac OS X assembler */
  ".globl __mco_wrap_main\n"
  "__mco_wrap_main:\n"
#else /* Linux assembler */
  ".globl _mco_wrap_main\n"
  ".type _mco_wrap_main @function\n"
  ".hidden _mco_wrap_main\n"
  "_mco_wrap_main:\n"
#endif
  "  movq %r13, %rdi\n"
  "  jmpq *%r12\n"
#ifndef __MACH__
  ".size _mco_wrap_main, .-_mco_wrap_main\n"
#endif
);

#define _MCO_SWICTH_SAVE_CTXBUF \
"  movq %rax, (%rdi)\n"  \
"  movq %rsp, 8(%rdi)\n" \
"  movq %rbp, 16(%rdi)\n"\
"  movq %rbx, 24(%rdi)\n"\
"  movq %r12, 32(%rdi)\n"\
"  movq %r13, 40(%rdi)\n"\
"  movq %r14, 48(%rdi)\n"\
"  movq %r15, 56(%rdi)\n"

#define _MCO_SWITCH_LOAD_CTXBUF \
"  movq 56(%rsi), %r15\n"\
"  movq 48(%rsi), %r14\n"\
"  movq 40(%rsi), %r13\n"\
"  movq 32(%rsi), %r12\n"\
"  movq 24(%rsi), %rbx\n"\
"  movq 16(%rsi), %rbp\n"\
"  movq 8(%rsi), %rsp\n" \
"  jmpq *(%rsi)\n"

__asm__(
  ".text\n"
#ifdef __MACH__ /* Mac OS assembler */
  ".globl __mco_switch1\n"
  "__mco_switch1:\n"
#else /* Linux assembler */
  ".globl _mco_switch1\n"
  ".type _mco_switch1 @function\n"
  ".hidden _mco_switch1\n"
  "_mco_switch1:\n"
#endif
  "  leaq 0x3d(%rip), %rax\n"
  _MCO_SWICTH_SAVE_CTXBUF
  _MCO_SWITCH_LOAD_CTXBUF
  "  ret\n"
#ifndef __MACH__
  ".size _mco_switch1, .-_mco_switch1\n"
#endif
);

__asm__(
    ".text\n"
#ifdef __MACH__ /* Mac OS assembler */
    ".globl __mco_switch2\n"
    "__mco_switch2:\n"
#else /* Linux assembler */
    ".globl _mco_switch2\n"
    ".type _mco_switch2 @function\n"
    ".hidden _mco_switch2\n"
    "_mco_switch2:\n"
#endif
    "  leaq 0x49(%rip), %rax\n" // 0x3d==offset L1
    _MCO_SWICTH_SAVE_CTXBUF

    "  mov $3, %eax\n" /* 3==MCO_SUSPENDED */
    "  xchg %eax, (%rdx)\n"
    "  cmp $3, %eax\n"
    "  je L2\n"

    _MCO_SWITCH_LOAD_CTXBUF
    "L1: xor %eax, %eax\n"
    "L2: ret\n"
#ifndef __MACH__
    ".size _mco_switch2, .-_mco_switch2\n"
#endif
);

static mco_result _mco_makectx(mco_coro* co, _mco_ctxbuf* ctx, void* stack_base, size_t stack_size) {
  stack_size = stack_size - 128; /* Reserve 128 bytes for the Red Zone space (System V AMD64 ABI). */
  void** stack_high_ptr = (void**)((size_t)stack_base + stack_size - sizeof(size_t));
  stack_high_ptr[0] = (void*)(0xdeaddeaddeaddead);  /* Dummy return address. */
  ctx->rip = (void*)(_mco_wrap_main);
  ctx->rsp = (void*)(stack_high_ptr);
  ctx->r12 = (void*)(_mco_main);
  ctx->r13 = (void*)(co);
  return MCO_SUCCESS;
}

#endif /* not _WIN32 */

#elif defined(__riscv)

typedef struct _mco_ctxbuf {
  void* s[12]; /* s0-s11 */
  void* ra;
  void* pc;
  void* sp;
#ifdef __riscv_flen
#if __riscv_flen == 64
  double fs[12]; /* fs0-fs11 */
#elif __riscv_flen == 32
  float fs[12]; /* fs0-fs11 */
#endif
#endif /* __riscv_flen */
} _mco_ctxbuf;

void _mco_wrap_main(void);
void _mco_switch1(_mco_ctxbuf* from, _mco_ctxbuf* to);
int _mco_switch2(_mco_ctxbuf* from, _mco_ctxbuf* to, mco_state* p);

__asm__(
  ".text\n"
  ".globl _mco_wrap_main\n"
  ".type _mco_wrap_main @function\n"
  ".hidden _mco_wrap_main\n"
  "_mco_wrap_main:\n"
  "  mv a0, s0\n"
  "  jr s1\n"
  ".size _mco_wrap_main, .-_mco_wrap_main\n"
);

__asm__(
  ".text\n"
  ".globl _mco_switch\n"
  ".type _mco_switch @function\n"
  ".hidden _mco_switch\n"
  "_mco_switch:\n"
  #if __riscv_xlen == 64
    "  sd s0, 0x00(a0)\n"
    "  sd s1, 0x08(a0)\n"
    "  sd s2, 0x10(a0)\n"
    "  sd s3, 0x18(a0)\n"
    "  sd s4, 0x20(a0)\n"
    "  sd s5, 0x28(a0)\n"
    "  sd s6, 0x30(a0)\n"
    "  sd s7, 0x38(a0)\n"
    "  sd s8, 0x40(a0)\n"
    "  sd s9, 0x48(a0)\n"
    "  sd s10, 0x50(a0)\n"
    "  sd s11, 0x58(a0)\n"
    "  sd ra, 0x60(a0)\n"
    "  sd ra, 0x68(a0)\n" /* pc */
    "  sd sp, 0x70(a0)\n"
    #ifdef __riscv_flen
    #if __riscv_flen == 64
    "  fsd fs0, 0x78(a0)\n"
    "  fsd fs1, 0x80(a0)\n"
    "  fsd fs2, 0x88(a0)\n"
    "  fsd fs3, 0x90(a0)\n"
    "  fsd fs4, 0x98(a0)\n"
    "  fsd fs5, 0xa0(a0)\n"
    "  fsd fs6, 0xa8(a0)\n"
    "  fsd fs7, 0xb0(a0)\n"
    "  fsd fs8, 0xb8(a0)\n"
    "  fsd fs9, 0xc0(a0)\n"
    "  fsd fs10, 0xc8(a0)\n"
    "  fsd fs11, 0xd0(a0)\n"
    "  fld fs0, 0x78(a1)\n"
    "  fld fs1, 0x80(a1)\n"
    "  fld fs2, 0x88(a1)\n"
    "  fld fs3, 0x90(a1)\n"
    "  fld fs4, 0x98(a1)\n"
    "  fld fs5, 0xa0(a1)\n"
    "  fld fs6, 0xa8(a1)\n"
    "  fld fs7, 0xb0(a1)\n"
    "  fld fs8, 0xb8(a1)\n"
    "  fld fs9, 0xc0(a1)\n"
    "  fld fs10, 0xc8(a1)\n"
    "  fld fs11, 0xd0(a1)\n"
    #else
    #error "Unsupported RISC-V FLEN"
    #endif
    #endif /* __riscv_flen */
    "  ld s0, 0x00(a1)\n"
    "  ld s1, 0x08(a1)\n"
    "  ld s2, 0x10(a1)\n"
    "  ld s3, 0x18(a1)\n"
    "  ld s4, 0x20(a1)\n"
    "  ld s5, 0x28(a1)\n"
    "  ld s6, 0x30(a1)\n"
    "  ld s7, 0x38(a1)\n"
    "  ld s8, 0x40(a1)\n"
    "  ld s9, 0x48(a1)\n"
    "  ld s10, 0x50(a1)\n"
    "  ld s11, 0x58(a1)\n"
    "  ld ra, 0x60(a1)\n"
    "  ld a2, 0x68(a1)\n" /* pc */
    "  ld sp, 0x70(a1)\n"
    "  jr a2\n"
  #elif __riscv_xlen == 32
    "  sw s0, 0x00(a0)\n"
    "  sw s1, 0x04(a0)\n"
    "  sw s2, 0x08(a0)\n"
    "  sw s3, 0x0c(a0)\n"
    "  sw s4, 0x10(a0)\n"
    "  sw s5, 0x14(a0)\n"
    "  sw s6, 0x18(a0)\n"
    "  sw s7, 0x1c(a0)\n"
    "  sw s8, 0x20(a0)\n"
    "  sw s9, 0x24(a0)\n"
    "  sw s10, 0x28(a0)\n"
    "  sw s11, 0x2c(a0)\n"
    "  sw ra, 0x30(a0)\n"
    "  sw ra, 0x34(a0)\n" /* pc */
    "  sw sp, 0x38(a0)\n"
    #ifdef __riscv_flen
    #if __riscv_flen == 64
    "  fsd fs0, 0x3c(a0)\n"
    "  fsd fs1, 0x44(a0)\n"
    "  fsd fs2, 0x4c(a0)\n"
    "  fsd fs3, 0x54(a0)\n"
    "  fsd fs4, 0x5c(a0)\n"
    "  fsd fs5, 0x64(a0)\n"
    "  fsd fs6, 0x6c(a0)\n"
    "  fsd fs7, 0x74(a0)\n"
    "  fsd fs8, 0x7c(a0)\n"
    "  fsd fs9, 0x84(a0)\n"
    "  fsd fs10, 0x8c(a0)\n"
    "  fsd fs11, 0x94(a0)\n"
    "  fld fs0, 0x3c(a1)\n"
    "  fld fs1, 0x44(a1)\n"
    "  fld fs2, 0x4c(a1)\n"
    "  fld fs3, 0x54(a1)\n"
    "  fld fs4, 0x5c(a1)\n"
    "  fld fs5, 0x64(a1)\n"
    "  fld fs6, 0x6c(a1)\n"
    "  fld fs7, 0x74(a1)\n"
    "  fld fs8, 0x7c(a1)\n"
    "  fld fs9, 0x84(a1)\n"
    "  fld fs10, 0x8c(a1)\n"
    "  fld fs11, 0x94(a1)\n"
    #elif __riscv_flen == 32
    "  fsw fs0, 0x3c(a0)\n"
    "  fsw fs1, 0x40(a0)\n"
    "  fsw fs2, 0x44(a0)\n"
    "  fsw fs3, 0x48(a0)\n"
    "  fsw fs4, 0x4c(a0)\n"
    "  fsw fs5, 0x50(a0)\n"
    "  fsw fs6, 0x54(a0)\n"
    "  fsw fs7, 0x58(a0)\n"
    "  fsw fs8, 0x5c(a0)\n"
    "  fsw fs9, 0x60(a0)\n"
    "  fsw fs10, 0x64(a0)\n"
    "  fsw fs11, 0x68(a0)\n"
    "  flw fs0, 0x3c(a1)\n"
    "  flw fs1, 0x40(a1)\n"
    "  flw fs2, 0x44(a1)\n"
    "  flw fs3, 0x48(a1)\n"
    "  flw fs4, 0x4c(a1)\n"
    "  flw fs5, 0x50(a1)\n"
    "  flw fs6, 0x54(a1)\n"
    "  flw fs7, 0x58(a1)\n"
    "  flw fs8, 0x5c(a1)\n"
    "  flw fs9, 0x60(a1)\n"
    "  flw fs10, 0x64(a1)\n"
    "  flw fs11, 0x68(a1)\n"
    #else
    #error "Unsupported RISC-V FLEN"
    #endif
    #endif /* __riscv_flen */
    "  lw s0, 0x00(a1)\n"
    "  lw s1, 0x04(a1)\n"
    "  lw s2, 0x08(a1)\n"
    "  lw s3, 0x0c(a1)\n"
    "  lw s4, 0x10(a1)\n"
    "  lw s5, 0x14(a1)\n"
    "  lw s6, 0x18(a1)\n"
    "  lw s7, 0x1c(a1)\n"
    "  lw s8, 0x20(a1)\n"
    "  lw s9, 0x24(a1)\n"
    "  lw s10, 0x28(a1)\n"
    "  lw s11, 0x2c(a1)\n"
    "  lw ra, 0x30(a1)\n"
    "  lw a2, 0x34(a1)\n" /* pc */
    "  lw sp, 0x38(a1)\n"
    "  jr a2\n"
  #else
    #error "Unsupported RISC-V XLEN"
  #endif /* __riscv_xlen */
  ".size _mco_switch, .-_mco_switch\n"
);

static mco_result _mco_makectx(mco_coro* co, _mco_ctxbuf* ctx, void* stack_base, size_t stack_size) {
  ctx->s[0] = (void*)(co);
  ctx->s[1] = (void*)(_mco_main);
  ctx->pc = (void*)(_mco_wrap_main);
#if __riscv_xlen == 64
  ctx->ra = (void*)(0xdeaddeaddeaddead);
#elif __riscv_xlen == 32
  ctx->ra = (void*)(0xdeaddead);
#endif
  ctx->sp = (void*)((size_t)stack_base + stack_size);
  return MCO_SUCCESS;
}

#elif defined(__i386) || defined(__i386__)

typedef struct _mco_ctxbuf {
  void *eip, *esp, *ebp, *ebx, *esi, *edi;
} _mco_ctxbuf;

void _mco_switch1(_mco_ctxbuf* from, _mco_ctxbuf* to);
int _mco_switch2(_mco_ctxbuf* from, _mco_ctxbuf* to, mco_state* p);

__asm__(
#ifdef __DJGPP__ /* DOS compiler */
  "__mco_switch:\n"
#else
  ".text\n"
  ".globl _mco_switch\n"
  ".type _mco_switch @function\n"
  ".hidden _mco_switch\n"
  "_mco_switch:\n"
#endif
  "  call 1f\n"
  "  1:\n"
  "  popl %ecx\n"
  "  addl $(2f-1b), %ecx\n"
  "  movl 4(%esp), %eax\n"
  "  movl 8(%esp), %edx\n"
  "  movl %ecx, (%eax)\n"
  "  movl %esp, 4(%eax)\n"
  "  movl %ebp, 8(%eax)\n"
  "  movl %ebx, 12(%eax)\n"
  "  movl %esi, 16(%eax)\n"
  "  movl %edi, 20(%eax)\n"
  "  movl 20(%edx), %edi\n"
  "  movl 16(%edx), %esi\n"
  "  movl 12(%edx), %ebx\n"
  "  movl 8(%edx), %ebp\n"
  "  movl 4(%edx), %esp\n"
  "  jmp *(%edx)\n"
  "  2:\n"
  "  ret\n"
#ifndef __DJGPP__
  ".size _mco_switch, .-_mco_switch\n"
#endif
);

static mco_result _mco_makectx(mco_coro* co, _mco_ctxbuf* ctx, void* stack_base, size_t stack_size) {
  void** stack_high_ptr = (void**)((size_t)stack_base + stack_size - 16 - 1*sizeof(size_t));
  stack_high_ptr[0] = (void*)(0xdeaddead);  /* Dummy return address. */
  stack_high_ptr[1] = (void*)(co);
  ctx->eip = (void*)(_mco_main);
  ctx->esp = (void*)(stack_high_ptr);
  return MCO_SUCCESS;
}

#elif defined(__ARM_EABI__)

typedef struct _mco_ctxbuf {
#ifndef __SOFTFP__
  void* f[16];
#endif
  void *d[4]; /* d8-d15 */
  void *r[4]; /* r4-r11 */
  void *lr;
  void *sp;
} _mco_ctxbuf;

void _mco_wrap_main(void);
void _mco_switch1(_mco_ctxbuf* from, _mco_ctxbuf* to);
int _mco_switch2(_mco_ctxbuf* from, _mco_ctxbuf* to, mco_state* p);

__asm__(
  ".text\n"
#ifdef __APPLE__
  ".globl __mco_switch\n"
  "__mco_switch:\n"
#else
  ".globl _mco_switch\n"
  ".type _mco_switch #function\n"
  ".hidden _mco_switch\n"
  "_mco_switch:\n"
#endif
#ifndef __SOFTFP__
  "  vstmia r0!, {d8-d15}\n"
#endif
  "  stmia r0, {r4-r11, lr}\n"
  "  str sp, [r0, #9*4]\n"
#ifndef __SOFTFP__
  "  vldmia r1!, {d8-d15}\n"
#endif
  "  ldr sp, [r1, #9*4]\n"
  "  ldmia r1, {r4-r11, pc}\n"
#ifndef __APPLE__
  ".size _mco_switch, .-_mco_switch\n"
#endif
);

__asm__(
  ".text\n"
#ifdef __APPLE__
  ".globl __mco_wrap_main\n"
  "__mco_wrap_main:\n"
#else
  ".globl _mco_wrap_main\n"
  ".type _mco_wrap_main #function\n"
  ".hidden _mco_wrap_main\n"
  "_mco_wrap_main:\n"
#endif
  "  mov r0, r4\n"
  "  mov ip, r5\n"
  "  mov lr, r6\n"
  "  bx ip\n"
#ifndef __APPLE__
  ".size _mco_wrap_main, .-_mco_wrap_main\n"
#endif
);

static mco_result _mco_makectx(mco_coro* co, _mco_ctxbuf* ctx, void* stack_base, size_t stack_size) {
  ctx->d[0] = (void*)(co);
  ctx->d[1] = (void*)(_mco_main);
  ctx->d[2] = (void*)(0xdeaddead); /* Dummy return address. */
  ctx->lr = (void*)(_mco_wrap_main);
  ctx->sp = (void*)((size_t)stack_base + stack_size);
  return MCO_SUCCESS;
}

#elif defined(__aarch64__)

typedef struct _mco_ctxbuf {
  void *x[12]; /* x19-x30 */
  void *sp;
  void *lr;
  void *d[8]; /* d8-d15 */
} _mco_ctxbuf;

void _mco_wrap_main(void);
void _mco_switch1(_mco_ctxbuf* from, _mco_ctxbuf* to);
int _mco_switch2(_mco_ctxbuf* from, _mco_ctxbuf* to, mco_state* p);

__asm__(
  ".text\n"
#ifdef __APPLE__
  ".globl __mco_switch\n"
  "__mco_switch:\n"
#else
  ".globl _mco_switch\n"
  ".type _mco_switch #function\n"
  ".hidden _mco_switch\n"
  "_mco_switch:\n"
#endif

  "  mov x10, sp\n"
  "  mov x11, x30\n"
  "  stp x19, x20, [x0, #(0*16)]\n"
  "  stp x21, x22, [x0, #(1*16)]\n"
  "  stp d8, d9, [x0, #(7*16)]\n"
  "  stp x23, x24, [x0, #(2*16)]\n"
  "  stp d10, d11, [x0, #(8*16)]\n"
  "  stp x25, x26, [x0, #(3*16)]\n"
  "  stp d12, d13, [x0, #(9*16)]\n"
  "  stp x27, x28, [x0, #(4*16)]\n"
  "  stp d14, d15, [x0, #(10*16)]\n"
  "  stp x29, x30, [x0, #(5*16)]\n"
  "  stp x10, x11, [x0, #(6*16)]\n"
  "  ldp x19, x20, [x1, #(0*16)]\n"
  "  ldp x21, x22, [x1, #(1*16)]\n"
  "  ldp d8, d9, [x1, #(7*16)]\n"
  "  ldp x23, x24, [x1, #(2*16)]\n"
  "  ldp d10, d11, [x1, #(8*16)]\n"
  "  ldp x25, x26, [x1, #(3*16)]\n"
  "  ldp d12, d13, [x1, #(9*16)]\n"
  "  ldp x27, x28, [x1, #(4*16)]\n"
  "  ldp d14, d15, [x1, #(10*16)]\n"
  "  ldp x29, x30, [x1, #(5*16)]\n"
  "  ldp x10, x11, [x1, #(6*16)]\n"
  "  mov sp, x10\n"
  "  br x11\n"
#ifndef __APPLE__
  ".size _mco_switch, .-_mco_switch\n"
#endif
);

__asm__(
  ".text\n"
#ifdef __APPLE__
  ".globl __mco_wrap_main\n"
  "__mco_wrap_main:\n"
#else
  ".globl _mco_wrap_main\n"
  ".type _mco_wrap_main #function\n"
  ".hidden _mco_wrap_main\n"
  "_mco_wrap_main:\n"
#endif
  "  mov x0, x19\n"
  "  mov x30, x21\n"
  "  br x20\n"
#ifndef __APPLE__
  ".size _mco_wrap_main, .-_mco_wrap_main\n"
#endif
);

static mco_result _mco_makectx(mco_coro* co, _mco_ctxbuf* ctx, void* stack_base, size_t stack_size) {
  ctx->x[0] = (void*)(co);
  ctx->x[1] = (void*)(_mco_main);
  ctx->x[2] = (void*)(0xdeaddeaddeaddead); /* Dummy return address. */
  ctx->sp = (void*)((size_t)stack_base + stack_size);
  ctx->lr = (void*)(_mco_wrap_main);
  return MCO_SUCCESS;
}

#else

#error "Unsupported architecture for assembly method."

#endif /* ARCH */

#ifdef MCO_USE_VALGRIND
#include <valgrind/valgrind.h>
#endif

typedef struct _mco_context {
#ifdef MCO_USE_VALGRIND
  unsigned int valgrind_stack_id;
#endif
  _mco_ctxbuf ctx;
} _mco_context;

MCO_THREAD_LOCAL _mco_ctxbuf* _mco_main_ctxbuf = NULL;

MCO_FORCE_INLINE _mco_ctxbuf* _mco_get_main_ctxbuf() {
    if (!_mco_main_ctxbuf)
        _mco_main_ctxbuf = (_mco_ctxbuf*)malloc(sizeof(_mco_ctxbuf));
    return _mco_main_ctxbuf;
}

static void _mco_jumpin(mco_coro* co) {
  _mco_context* context = (_mco_context*)co->context;
  mco_coro* prev_co = _mco_prepare_jumpin(co);
  _mco_ctxbuf* ctxbuf = prev_co ? &((_mco_context*)prev_co->context)->ctx : _mco_get_main_ctxbuf();
  _mco_switch1(ctxbuf, &context->ctx); /* Do the context switch. */
}

static void _mco_jumpout(mco_coro* co) {
  _mco_context* context = (_mco_context*)co->context;
  mco_coro* prev_co = _mco_prepare_jumpout(co);
  MCO_ASSERT(prev_co != NULL || _mco_main_ctxbuf != NULL);
  _mco_ctxbuf* ctxbuf = prev_co ? &((_mco_context*)prev_co->context)->ctx : _mco_main_ctxbuf;
  _mco_switch1(&context->ctx, ctxbuf); /* Do the context switch. */
}

static mco_result _mco_create_context(mco_coro* co, const mco_desc* desc) {
  /* Determine the context and stack address. */
  size_t co_addr = (size_t)co;
  size_t context_addr = _mco_align_forward(co_addr + sizeof(mco_coro), 16);
  size_t storage_addr = _mco_align_forward(context_addr + sizeof(_mco_context), 16);
  size_t stack_addr = _mco_align_forward(storage_addr + desc->storage_size, 16);
  /* Initialize context. */
  _mco_context* context = (_mco_context*)context_addr;
  memset(context, 0, sizeof(_mco_context));
  /* Initialize storage. */
  unsigned char* storage = (unsigned char*)storage_addr;
  memset(storage, 0, desc->storage_size);
  /* Initialize stack. */
  void *stack_base = (void*)stack_addr;
  size_t stack_size = desc->stack_size;
  /* Make the context. */
  mco_result res = _mco_makectx(co, &context->ctx, stack_base, stack_size);
  if(res != MCO_SUCCESS) {
    return res;
  }
#ifdef MCO_USE_VALGRIND
  context->valgrind_stack_id = VALGRIND_STACK_REGISTER(stack_addr, stack_addr + stack_size);
#endif
  co->context = context;
  co->stack_base = stack_base;
  co->stack_size = stack_size;
  co->storage = storage;
  co->storage_size = desc->storage_size;
  return MCO_SUCCESS;
}

static void _mco_destroy_context(mco_coro* co) {
#ifdef MCO_USE_VALGRIND
  _mco_context* context = (_mco_context*)co->context;
  if(context && context->valgrind_stack_id != 0) {
    VALGRIND_STACK_DEREGISTER(context->valgrind_stack_id);
    context->valgrind_stack_id = 0;
  }
#else
  _MCO_UNUSED(co);
#endif
}

static MCO_FORCE_INLINE void _mco_init_desc_sizes(mco_desc* desc, size_t stack_size) {
  desc->coro_size = _mco_align_forward(sizeof(mco_coro), 16) +
                    _mco_align_forward(sizeof(_mco_context), 16) +
                    _mco_align_forward(desc->storage_size, 16) +
                    stack_size + 16;
  desc->stack_size = stack_size; /* This is just a hint, it won't be the real one. */
}


mco_desc mco_desc_init(void (*func)(mco_coro* co), size_t stack_size) {
  if(stack_size != 0) {
    /* Stack size should be at least `MCO_MIN_STACK_SIZE`. */
    if(stack_size < MCO_MIN_STACK_SIZE)
      stack_size = MCO_MIN_STACK_SIZE;
  }
  else
    stack_size = MCO_DEFAULT_STACK_SIZE;
  stack_size = _mco_align_forward(stack_size, 16); /* Stack size should be aligned to 16 bytes. */
  mco_desc desc;
  memset(&desc, 0, sizeof(mco_desc));
#ifndef MCO_NO_DEFAULT_ALLOCATORS
  /* Set default allocators. */
  desc.malloc_cb = mco_malloc;
  desc.free_cb = mco_free;
#endif
  desc.func = func;
  desc.storage_size = MCO_DEFAULT_STORAGE_SIZE;
  _mco_init_desc_sizes(&desc, stack_size);
  return desc;
}

static mco_result _mco_validate_desc(const mco_desc* desc) {
  if(!desc) {
    MCO_LOG("coroutine description is NULL");
    return MCO_INVALID_ARGUMENTS;
  }
  if(!desc->func) {
    MCO_LOG("coroutine function in invalid");
    return MCO_INVALID_ARGUMENTS;
  }
  if(desc->stack_size < MCO_MIN_STACK_SIZE) {
    MCO_LOG("coroutine stack size is too small");
    return MCO_INVALID_ARGUMENTS;
  }
  if(desc->coro_size < sizeof(mco_coro)) {
    MCO_LOG("coroutine size is invalid");
    return MCO_INVALID_ARGUMENTS;
  }
  return MCO_SUCCESS;
}

mco_result mco_init(mco_coro* co, const mco_desc* desc) {
  if(!co) {
    MCO_LOG("attempt to initialize an invalid coroutine");
    return MCO_INVALID_COROUTINE;
  }
  memset(co, 0, sizeof(mco_coro));
  /* Validate coroutine description. */
  mco_result res = _mco_validate_desc(desc);
  if(res != MCO_SUCCESS)
    return res;
  /* Create the coroutine. */
  res = _mco_create_context(co, desc);
  if(res != MCO_SUCCESS)
    return res;
  co->state = MCO_SUSPENDED; /* We initialize in suspended state. */
  co->free_cb = desc->free_cb;
  co->allocator_data = desc->allocator_data;
  co->func = desc->func;
  co->user_data = desc->user_data;
#ifdef _MCO_USE_TSAN
  co->tsan_fiber = __tsan_create_fiber(0);
#endif
  co->magic_number = MCO_MAGIC_NUMBER;
  return MCO_SUCCESS;
}

mco_result mco_uninit(mco_coro* co) {
  if(!co) {
    MCO_LOG("attempt to uninitialize an invalid coroutine");
    return MCO_INVALID_COROUTINE;
  }
  /* Cannot uninitialize while running. */
  if(!(co->state == MCO_SUSPENDED || co->state == MCO_DEAD)) {
    MCO_LOG("attempt to uninitialize a coroutine that is not dead or suspended");
    return MCO_INVALID_OPERATION;
  }
  /* The coroutine is now dead and cannot be used anymore. */
  co->state = MCO_DEAD;
#ifdef _MCO_USE_TSAN
  if(co->tsan_fiber != NULL) {
    __tsan_destroy_fiber(co->tsan_fiber);
    co->tsan_fiber = NULL;
  }
#endif
  _mco_destroy_context(co);
  return MCO_SUCCESS;
}

mco_result mco_create(mco_coro** out_co, const mco_desc* desc) {
  /* Validate input. */
  if(!out_co) {
    MCO_LOG("coroutine output pointer is NULL");
    return MCO_INVALID_POINTER;
  }
  if(!desc || !desc->malloc_cb || !desc->free_cb) {
    *out_co = NULL;
    MCO_LOG("coroutine allocator description is not set");
    return MCO_INVALID_ARGUMENTS;
  }
  /* Allocate the coroutine. */
  mco_coro* co = (mco_coro*)desc->malloc_cb(desc->coro_size, desc->allocator_data);
  if(!co) {
    MCO_LOG("coroutine allocation failed");
    *out_co = NULL;
    return MCO_OUT_OF_MEMORY;
  }
  /* Initialize the coroutine. */
  mco_result res = mco_init(co, desc);
  if(res != MCO_SUCCESS) {
    desc->free_cb(co, desc->allocator_data);
    *out_co = NULL;
    return res;
  }
  *out_co = co;
  return MCO_SUCCESS;
}

mco_result mco_destroy(mco_coro* co) {
  if(!co) {
    MCO_LOG("attempt to destroy an invalid coroutine");
    return MCO_INVALID_COROUTINE;
  }
  /* Uninitialize the coroutine first. */
  mco_result res = mco_uninit(co);
  if(res != MCO_SUCCESS)
    return res;
  /* Free the coroutine. */
  if(!co->free_cb) {
    MCO_LOG("attempt destroy a coroutine that has no free callback");
    return MCO_INVALID_POINTER;
  }
  co->free_cb(co, co->allocator_data);
  return MCO_SUCCESS;
}

mco_result mco_resume(mco_coro* co) {
    if (!co) {
        MCO_LOG("attempt to resume an invalid coroutine");
        return MCO_INVALID_COROUTINE;
    }
    if (co->state != MCO_SUSPENDED) { /* Can only resume coroutines that are suspended. */
        MCO_LOG("attempt to resume a coroutine that is not suspended");
        return MCO_INVALID_STATUS;
    }
    co->state = MCO_RUNNING; /* The coroutine is now running. */
    _mco_jumpin(co);
    return MCO_SUCCESS;
}

mco_result mco_resume_mt(mco_coro* co) {
    if (!co) {
        MCO_LOG("attempt to resume an invalid coroutine");
        return MCO_INVALID_COROUTINE;
    }
    mco_state oldState = (mco_state)BlFas32((int32_t volatile*)&co->state, MCO_SUSPENDED);
    if (oldState == MCO_SUSPENDED) {
        // state was set by mco_yield_mt first, see 'Note on multithreading asynchonized I/O' in minicoro.h.
        co->state = MCO_RUNNING; /* The coroutine is now running. */
        _mco_jumpin(co);
    }
    else if (oldState != MCO_RUNNING)
        return MCO_INVALID_STATUS;
    return MCO_SUCCESS;
}

mco_result mco_yield() {
    mco_coro* co = mco_running();
    if (!co) {
        MCO_LOG("attempt to yield an invalid coroutine");
        return MCO_INVALID_COROUTINE;
    }

    /* This check happens when the stack overflow already happened, but better later than never. */
    volatile size_t dummy;
    size_t stack_addr = (size_t)&dummy;
    size_t stack_min = (size_t)co->stack_base;
    size_t stack_max = stack_min + co->stack_size;
    if (co->magic_number != MCO_MAGIC_NUMBER || stack_addr < stack_min || stack_addr > stack_max) { /* Stack overflow. */
        MCO_LOG("coroutine stack overflow, try increasing the stack size");
        return MCO_STACK_OVERFLOW;
    }

    if (co->state != MCO_RUNNING) {  /* Can only yield coroutines that are running. */
        MCO_LOG("attempt to yield a coroutine that is not running");
        return MCO_INVALID_STATUS;
    }
    co->state = MCO_SUSPENDED; /* The coroutine is now suspended. */
    _mco_jumpout(co);
    return MCO_SUCCESS;
}

mco_result mco_yield_mt() {
    mco_coro* co = mco_running();
    if (!co) {
        MCO_LOG("attempt to yield an invalid coroutine");
        return MCO_INVALID_COROUTINE;
    }

    /* This check happens when the stack overflow already happened, but better later than never. */
    volatile size_t dummy;
    size_t stack_addr = (size_t)&dummy;
    size_t stack_min = (size_t)co->stack_base;
    size_t stack_max = stack_min + co->stack_size;
    if (co->magic_number != MCO_MAGIC_NUMBER || stack_addr < stack_min || stack_addr > stack_max) { /* Stack overflow. */
        MCO_LOG("coroutine stack overflow, try increasing the stack size");
        return MCO_STACK_OVERFLOW;
    }

    if (co->state != MCO_RUNNING) {
        // set to MCO_SUSPENDED by mco_resume_mt first, see 'Note on multithreading asynchonized I/O' in minicoro.h.
        MCO_ASSERT(co->state == MCO_SUSPENDED);
        co->state = MCO_RUNNING;
        return MCO_SUCCESS;
    }

    mco_coro* prev_co = _mco_prepare_jumpout(co);
    MCO_ASSERT(prev_co != NULL || _mco_main_ctxbuf != NULL);
    _mco_ctxbuf* ctxbuf = prev_co ? &((_mco_context*)prev_co->context)->ctx : _mco_main_ctxbuf;
    if (_mco_switch2(&((_mco_context*)co->context)->ctx, ctxbuf, &co->state)) { // Handle race condition, see 'Note on multithreading asynchonized I/O' in minicoro.h.
        // Needn't to switch the context, rollback to current coroutine
        co->state = MCO_RUNNING;
        mco_current_co = co;
        if (prev_co) {
            prev_co->state = MCO_NORMAL;
            co->prev_co = prev_co;
        }
    } // else being resumed
    return MCO_SUCCESS;
}

mco_state mco_status(const mco_coro* co) {
  return co? co->state: MCO_DEAD;
}

const void* mco_get_user_data(const mco_coro* co) {
  return co? co->user_data: NULL;
}

void* mco_push(mco_coro* co, size_t len, size_t align) {
  if (!co) {
    MCO_LOG("attempt to use an invalid coroutine");
    return NULL;
  }
  if (len <= 0) {
    MCO_LOG("attemp to mco_push 0 bytes");
    return NULL;
  }
  size_t start = _mco_align_forward(co->bytes_stored, align);
  size_t bytes_stored = start + len;
  if(bytes_stored > co->storage_size) {
    MCO_LOG("attempt to push too many bytes into coroutine storage");
    return NULL;
  }
  co->bytes_stored = bytes_stored;
  return co->storage + start;
}

void* mco_pop(mco_coro* co, size_t len, size_t align) {
  if(!co) {
    MCO_LOG("attempt to use an invalid coroutine");
    return NULL;
  }
  if (len <= 0) {
    MCO_LOG("attemp to mco_pop 0 bytes");
    return NULL;
  }
  if(len > co->bytes_stored) {
    MCO_LOG("attempt to pop too many bytes from coroutine storage");
    return NULL;
  }
  size_t bytes_stored = ((co->bytes_stored - len) & ~(align-1));
  co->bytes_stored = bytes_stored;
  return co->storage+bytes_stored;
}

void* mco_peek(mco_coro* co, size_t len, size_t align) {
  if (!co) {
    MCO_LOG("attempt to use an invalid coroutine");
    return NULL;
  }
  if (len <= 0) {
    MCO_LOG("attemp to mco_peek 0 bytes");
    return NULL;
  }
  if(len > co->bytes_stored) {
    MCO_LOG("attempt to peek too many bytes from coroutine storage");
    return NULL;
  }
  size_t bytes_stored = ((co->bytes_stored - len) & ~(align - 1));
  return co->storage + bytes_stored;
}

size_t mco_get_bytes_stored(mco_coro* co) {
  return co? co->bytes_stored: 0;
}

size_t mco_get_storage_size(mco_coro* co) {
  return co? co->storage_size: 0;
}

#if MCO_NO_MULTITHREAD
/*
Some compilers such as MSVC aggressively optimize the use of TLS by caching loads.
Since fiber code can migrate between threads it’s possible for the load to be stale.
You should tell compiler prevent this from happening.
For MSVC, add compiler option /GT(disable cache static thread local storage variable).
There is no problem with GCC and Clang.
*/
MCO_FORCE_INLINE mco_coro* mco_running(void) {
  return mco_current_co;
}
#else
static MCO_NO_INLINE mco_coro* _mco_running(void) {
    return mco_current_co;
}
mco_coro* mco_running(void) {
    /*
    Compilers aggressively optimize the use of TLS by caching loads.
    Since fiber code can migrate between threads it’s possible for the load to be stale.
    To prevent this from happening we avoid inline functions.
    */
    mco_coro* (* volatile func)(void) = _mco_running;
    return func();
}
#endif

void mco_thread_cleanup(void) {
    assert(mco_running() == NULL);
    if (_mco_main_ctxbuf) {
        free(_mco_main_ctxbuf);
        _mco_main_ctxbuf = NULL;
    }
}

#ifdef __cplusplus
}
#endif
