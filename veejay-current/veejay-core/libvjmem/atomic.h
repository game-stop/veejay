#ifndef VJATOMIC_H
#define VJATOMIC_H
#include <config.h>
#include <stdint.h>
#define HAVE_STRICT
#ifdef HAVE_STRICT
#include <assert.h>
#define ASSERT_ALIGNED(ptr, type) \
    assert(((uintptr_t)(ptr) % sizeof(type)) == 0 && "Pointer must be naturally aligned for atomic operations")
#else
#define ASSERT_ALIGNED(ptr, type) ((void)0)
#endif
static inline void atomic_store_double(volatile double *target, double value) {
    union {
        double d;
        uint64_t u;
    } converter;
    converter.d = value;
    __atomic_store_n((volatile uint64_t *)target, converter.u, __ATOMIC_RELEASE);
}

static inline double atomic_load_double(const volatile double *source) {
    union {
        uint64_t u;
        double d;
    } converter;
    converter.u = __atomic_load_n((const volatile uint64_t *)source, __ATOMIC_ACQUIRE);
    return converter.d;
}

static inline void atomic_store_int(volatile int *target, int value) {
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static inline int atomic_load_int(const volatile int *source) {
    return __atomic_load_n(source, __ATOMIC_ACQUIRE);
}

static inline uint32_t atomic_load_uint(const volatile uint32_t *source) {
    return __atomic_load_n(source, __ATOMIC_ACQUIRE);
}

static inline unsigned long atomic_exchange_int(volatile int *target, int value) {
    ASSERT_ALIGNED(target, int);
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static inline void atomic_store_long(volatile long *target, long value) {
    ASSERT_ALIGNED(target, long);
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static inline void atomic_store_ulong(volatile unsigned long *target, unsigned long value) {
    ASSERT_ALIGNED(target, unsigned long);
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static inline void atomic_store_uint(volatile unsigned int *target, unsigned int value) {
	ASSERT_ALIGNED(target,unsigned int);
	__atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static inline long atomic_load_long(const volatile long *source) {
    ASSERT_ALIGNED(source, long);
    return __atomic_load_n(source, __ATOMIC_ACQUIRE);
}

static inline long atomic_load_ulong(const volatile unsigned long *source) {
    ASSERT_ALIGNED(source, unsigned long);
    return __atomic_load_n(source, __ATOMIC_ACQUIRE);
}

static inline unsigned long atomic_exchange_long(volatile long *target, long value) {
    ASSERT_ALIGNED(target, long);
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static inline int atomic_add_fetch_old_int(volatile int *target, int delta) {
    ASSERT_ALIGNED(target, int);
    return __atomic_fetch_add(target, delta, __ATOMIC_SEQ_CST);
}

static inline long atomic_add_fetch_old_long(volatile long *target, long delta) {
    ASSERT_ALIGNED(target, long);
    return __atomic_fetch_add(target, delta, __ATOMIC_SEQ_CST);
}

static inline void atomic_store_long_sync(volatile long *target, long value) {
    ASSERT_ALIGNED(target, long);
    __atomic_store_n(target, value, __ATOMIC_SEQ_CST);
}

static inline long atomic_load_long_sync(const volatile long *source) {
    ASSERT_ALIGNED(source, long);
    return __atomic_load_n(source, __ATOMIC_SEQ_CST);
}

static inline unsigned long atomic_add_fetch_old_ulong(volatile unsigned long *target, unsigned long delta) {
    ASSERT_ALIGNED(target, unsigned long);
    return __atomic_fetch_add(target, delta, __ATOMIC_SEQ_CST);
}

static inline long atomic_add_and_fetch_long(volatile long *target, long delta) {
    ASSERT_ALIGNED(target, long);
    return __atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST);
}

static inline double atomic_exchange_double(volatile double *target, double new_value) {
    ASSERT_ALIGNED(target, double);
    union {
        double d;
        uint64_t u;
    } in, out;
    in.d = new_value;
    out.u = __atomic_exchange_n((volatile uint64_t *)target, in.u, __ATOMIC_SEQ_CST);
    return out.d;
}

static inline void* atomic_exchange_ptr(volatile uintptr_t *target, uintptr_t new_ptr) {
    ASSERT_ALIGNED(target, uintptr_t);
    uintptr_t old_val = __atomic_exchange_n(target, new_ptr, __ATOMIC_SEQ_CST);
    return (void *)old_val;
}

static inline long atomic_add_fetch_long(volatile long *target, long delta) {
    ASSERT_ALIGNED(target, long);
    return __atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_exchange_uint(volatile uint32_t *target, uint32_t value) {
    ASSERT_ALIGNED(target, uint32_t); 
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_add_fetch_uint(volatile uint32_t *target, uint32_t delta) {
    ASSERT_ALIGNED(target, uint32_t);
    return __atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST);
}

static inline unsigned long atomic_exchange_ulong(volatile unsigned long *target, unsigned long value) {
    ASSERT_ALIGNED(target, unsigned long);
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static inline unsigned long atomic_add_fetch_ulong(volatile unsigned long *target, unsigned long delta) {
    ASSERT_ALIGNED(target, unsigned long);
    return __atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST);
}

static inline void atomic_synchronize(void) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static inline void atomic_store_long_long(volatile long long *target, long long value) {
    ASSERT_ALIGNED(target, long long);
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static inline long long atomic_load_long_long(const volatile long long *source) {
    ASSERT_ALIGNED(source, long long);
    return __atomic_load_n(source, __ATOMIC_ACQUIRE);
}

static inline long long atomic_store_long_long_sync(volatile long long *target, long long value) {
    ASSERT_ALIGNED(target, long long);
    __atomic_store_n(target, value, __ATOMIC_SEQ_CST);
    return value;
}

static inline long long atomic_load_long_long_sync(const volatile long long *source) {
    ASSERT_ALIGNED(source, long long);
    return __atomic_load_n(source, __ATOMIC_SEQ_CST);
}

static inline long long atomic_add_fetch_old_long_long(volatile long long *target, long long delta) {
    ASSERT_ALIGNED(target, long long);
    return __atomic_fetch_add(target, delta, __ATOMIC_SEQ_CST);
}

static inline long long atomic_add_and_fetch_long_long(volatile long long *target, long long delta) {
    ASSERT_ALIGNED(target, long long);
    return __atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST);
}

static inline long long atomic_exchange_long_long(volatile long long *target, long long value) {
    ASSERT_ALIGNED(target, long long);
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static inline void atomic_publish(volatile int *target, long long value) {
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static inline int atomic_consume(volatile long long *target) {
    return __atomic_load_n(target, __ATOMIC_ACQUIRE);
}


#endif
