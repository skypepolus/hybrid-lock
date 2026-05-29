# hybrid-lock

A hyper-optimized, header-only, cache-isolated hybrid spin-semaphore lock designed specifically for low-latency systems, micro-critical sections, and core-mapped memory allocators.

---

## Overview

`hybrid-lock` is engineered to bridge the performance gap between raw user-space spinning and operating system synchronization primitives. Traditional mutexes drop threads to sleep too early, introducing devastating context-switch penalties for short-lived critical sections. Conversely, naive spinlocks thrash the hardware system bus under heavy contention.

This library delivers an elegant, single-word state engine that natively optimizes for both extremes: resolving low-contention encounters instantly in user-space, while dynamically dampening execution loops as concurrency spikes to protect hardware cache lines.

---

## Key Features

* **Header-Only Layout:** Declared entirely as `static inline` primitives, granting the compiler full visibility to fuse locking operations directly into surrounding code, eliminating function-call overhead.
* **Single-Word State Machine:** Uses a single `_Atomic int` to simultaneously track lock acquisition and active waiter counts, maximizing register efficiency.
* **Queue-Aware Spin Truncation (Self-Dampening):** Threads spinning in user-space monitor the global state. The exact microsecond a trailing thread drops to an OS sleep and increments the waiter count, all other spinning threads instantly abort their loops to prevent system-bus thrashing.
* **Hardware Cache Isolation:** Explicitly padded to 64-byte alignments (`__attribute__((aligned(64)))`) to guarantee that the atomic variables sit completely isolated on their own L1/L2 cache lines, eliminating performance-killing False Sharing.
* **Cross-Platform Portability:** Automatically adapts to host environments, utilizing Grand Central Dispatch semaphores on Apple Darwin, POSIX semaphores on Linux, and optimizing hardware spin hints across x86 (`PAUSE`), ARM (`YIELD`), and WebAssembly sandboxes.

---

## API Reference

All synchronization operations are atomic, branchless on fast-paths, and fully thread-safe.

| Function Signature | Execution Type | Description |
| :--- | :--- | :--- |
| `void hybrid_initial(struct hybrid* lock)` | Bounded O(1) | Initializes the lock state word and constructs the underlying OS backplane primitive. |
| `int hybrid_try(struct hybrid* lock)` | Strict Non-Blocking O(1) | Attempts a single atomic acquisition. Returns 1 on success, or 0 immediately if the lock is held or contended. |
| `void hybrid_lock(struct hybrid* lock, int spin)` | Adaptive / Bounded O(log n) | Secures the lock. Spins in user-space for up to `spin` iterations, gracefully falling back to an OS sleep if the threshold expires or a queue forms. |
| `void hybrid_unlock(struct hybrid* lock)` | Fast-Path O(1) | Relinquishes the lock, executing a lightweight user-space release or signaling the OS semaphore if waiters are detected. |

---

## Mechanical Deep-Dive: Self-Dampening

Unlike traditional adaptive mutexes that blindly execute fixed loop iterations across all threads, `hybrid-lock` introduces a dynamic congestion sensor directly into the spin-evaluation loop:

```c
do
{
	int expected = 0;
	if(atomic_compare_exchange_strong_explicit(&lock->wait, &expected, 1, memory_order_acquire, memory_order_relaxed))
		return;
} while(spin-- > 0 
&& (platform_spin_pause(), 1) 
&& 1 >= atomic_load_explicit(&lock->wait, memory_order_relaxed));
```

When multiple threads concurrently storm the lock, the state shifts dynamically:
1. **Fast-Path:** Thread A secures the lock, driving the `wait` status to 1.
2. **Spin-Phase:** Thread B arrives, fails the fast path, and enters the `do-while` spin loop.
3. **Truncation Trigger:** Thread B exhausts its spin counter and commits to a kernel sleep, invoking `atomic_fetch_add` to bump `wait` to 2.
4. **Self-Dampening Cascade:** Threads C and D, still spinning in user space, read `wait == 2` on their next evaluation loop. They instantly break out of their spins and drop to the OS queue without executing any further atomic calculations.

---

## Integration & Usage

To integrate `hybrid-lock` into your architecture, save `hybrid_lock.h` into your project's include path and reference it directly.

```c
#include "hybrid_lock.h"
#include <stdio.h>

// Allocate and isolate the lock structure
struct hybrid local_mutex;

void critical_section_example(void) {
    // Acquire lock with a deterministic 500-cycle user-space spin ceiling
    hybrid_lock(&local_mutex, 500);

    /* --- Perform low-latency mutations here --- */

    // Release and signal potential waiters
    hybrid_unlock(&local_mutex);
}

int main(void) {
    // Initialize the synchronization subsystem
    hybrid_initial(&local_mutex);
    
    critical_section_example();
    return 0;
}
```

### Compilation

When targeting Linux, ensure your toolchain links against the POSIX threads and real-time extensions libraries:

```bash
gcc -O3 -Wall main.c -o main -lpthread
```

When targeting WebAssembly, the library automatically translates hardware hints into standard web-safe constraints, ensuring compile-time compatibility out-of-the-box.

---

## License

This project is licensed under the Apache License, Version 2.0. See the `LICENSE` file for details.
