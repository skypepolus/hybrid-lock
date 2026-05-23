#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "hybrid_lock.h"

#define THREAD_COUNT 8
#define ITERATIONS_PER_THREAD 1000000
#define SPIN_THRESHOLD 500

// Shared resources
struct hybrid shared_lock;
long long shared_counter = 0;

void* stress_worker(void* arg) {
    // Silence the unused argument warning cleanly
    (void)arg;
    
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        // 1. Acquire the lock with our tuned deterministic user-space spin quota
        hybrid_lock(&shared_lock, SPIN_THRESHOLD);
        
        // 2. Critical Section: Mutate shared state
        shared_counter++;
        
        // 3. Release the lock, triggering the state-dampening logic if waiters exist
        hybrid_unlock(&shared_lock);
    }
    
    return NULL;
}

int main(void) {
    pthread_t threads[THREAD_COUNT];
    struct timespec start_time, end_time;
    
    printf("[+] Initializing portable hybrid lock...\n");
    hybrid_initial(&shared_lock);
    
    printf("[+] Spawning %d threads competing for %d iterations each...\n", 
           THREAD_COUNT, ITERATIONS_PER_THREAD);
           
    // Fixed typo: CLOCK_MONOTONONIC -> CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    // Spawn a wave of simultaneous threads
    for (long i = 0; i < THREAD_COUNT; i++) {
        if (pthread_create(&threads[i], NULL, stress_worker, (void*)i) != 0) {
            perror("[-] Failed to create thread");
            return EXIT_FAILURE;
        }
    }
    
    // Wait for all workers to complete execution
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Fixed typo: CLOCK_MONOTONONIC -> CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    // Calculate performance metrics
    double elapsed_sec = (end_time.tv_sec - start_time.tv_sec) + 
                         (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    long long expected_total = (long long)THREAD_COUNT * ITERATIONS_PER_THREAD;
    
    printf("\n=== STRESS TEST RESULTS ===\n");
    printf("Expected Counter Value: %lld\n", expected_total);
    printf("Actual Counter Value:   %lld\n", shared_counter);
    printf("Execution Time:         %.4f seconds\n", elapsed_sec);
    printf("Operations Per Second:  %.2f million/sec\n", (expected_total / elapsed_sec) / 1e6);
    
    // Validate structural integrity of the state machine
    if (shared_counter == expected_total) {
        printf("\n[SUCCESS] Invariants preserved! Zero race conditions detected.\n");
        return EXIT_SUCCESS;
    } else {
        printf("\n[FAILURE] Heap/State corruption detected! Data lost due to race condition.\n");
        return EXIT_FAILURE;
    }
}
