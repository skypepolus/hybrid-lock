#ifndef __lock_h__
#define __lock_h__

#include <stdatomic.h>
#include <errno.h>
#include <assert.h>

/* 1. Cross-Platform OS Primitive Selection */
#if defined(__APPLE__)
    #include <dispatch/dispatch.h>
    typedef dispatch_semaphore_t platform_sem_t;
#else
    #include <semaphore.h>
    typedef sem_t platform_sem_t;
#endif

/* 2. Cross-Platform CPU Spin Hint Selection */
static inline void platform_spin_pause(void) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield" ::: "memory");
#else
    /* WebAssembly / Generic Fallback: Compiler Memory Barrier */
    __asm__ __volatile__("" ::: "memory");
#endif
}

struct hybrid
{
	_Atomic int wait; /* number of waiting threads */
	platform_sem_t sem;
} __attribute__((aligned(64)));

static inline void hybrid_initial(struct hybrid* lock)
{
	lock->wait = 0;
#if defined(__APPLE__)
	lock->sem = dispatch_semaphore_create(0);
#else
	sem_init(&lock->sem, 0, 0); /* Initialize POSIX semaphore (shared=0, value=0) */
#endif
}

static inline unsigned hybrid_try(struct hybrid* lock)
{
	int expected;
	int desired;
	if(0 == (expected = atomic_load_explicit(&lock->wait, memory_order_relaxed))
	&& atomic_compare_exchange_strong_explicit(&lock->wait, &expected, desired = 1, memory_order_acquire, memory_order_relaxed))
		return 1; /* Success */
	return 0; /* Code busy/held */
}

static inline void hybrid_lock(struct hybrid* lock, int spin)
{
	do
	{
		int expected;
		int desired;
		if(0 == (expected = atomic_load_explicit(&lock->wait, memory_order_relaxed))
		&& atomic_compare_exchange_strong_explicit(&lock->wait, &expected, desired = 1, memory_order_acquire, memory_order_relaxed))
			return;
	} while(spin-- > 0 
	&& (platform_spin_pause(), 1) 
	&& 1 >= atomic_load_explicit(&lock->wait, memory_order_relaxed));

	if(0 < atomic_fetch_add_explicit(&lock->wait, 1, memory_order_relaxed)) {
		/* threads may wait */
#if defined(__APPLE__)
		dispatch_semaphore_wait(lock->sem, DISPATCH_TIME_FOREVER);
#else
		/* Prevent spurious wakeups from Linux OS user signals (EINTR) */
		while (sem_wait(&lock->sem) == -1 && errno == EINTR);
#endif
	}
	atomic_load_explicit(&lock->wait, memory_order_acquire);
}

static inline void hybrid_unlock(struct hybrid* lock)
{
	if(1 < atomic_fetch_sub_explicit(&lock->wait, 1, memory_order_release)) {
		/* one or more threads may be waiting */
#if defined(__APPLE__)
		dispatch_semaphore_signal(lock->sem);
#else
		sem_post(&lock->sem);
#endif
	}
}

#endif /* __lock_h__ */
