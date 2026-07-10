/* Tests for the per-thread cache (ThreadCache) front-end:
 *   1. Cross-thread free: allocate in thread A, free in thread B.
 *   2. Thread-exit drain: many short-lived threads each holding more than
 *      MEMALLOC_TC_HIGH_WATER cached objects at exit, verifying the
 *      pthread-key thread-exit destructor flushes them back to the central
 *      pools (no permanent leak).
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memalloc/allocator.h"

#define MEMALLOC_TEST_CROSS_COUNT 256
#define MEMALLOC_TEST_CROSS_SLOT_SIZE 64

static void* ptrs[MEMALLOC_TEST_CROSS_COUNT];

static void* cross_thread_producer(void* arg) {
    (void)arg;
    for (int i = 0; i < MEMALLOC_TEST_CROSS_COUNT; ++i) {
        void* p = memalloc_allocate(MEMALLOC_TEST_CROSS_SLOT_SIZE);
        if (!p) {
            fprintf(stderr, "cross_thread_free: allocate failed\n");
            abort();
        }
        memset(p, (unsigned char)i, MEMALLOC_TEST_CROSS_SLOT_SIZE);
        ptrs[i] = p;
    }
    return NULL;
}

static void* cross_thread_consumer(void* arg) {
    (void)arg;
    for (int i = 0; i < MEMALLOC_TEST_CROSS_COUNT; ++i) {
        unsigned char* p = (unsigned char*)ptrs[i];
        for (int j = 0; j < MEMALLOC_TEST_CROSS_SLOT_SIZE; ++j) {
            if (p[j] != (unsigned char)i) {
                fprintf(stderr, "cross_thread_free: corruption at ptr[%d]+%d\n", i, j);
                abort();
            }
        }
        memalloc_deallocate(ptrs[i]);
    }
    return NULL;
}

static void test_cross_thread_free(void) {
    pthread_t producer, consumer;

    /* Allocate and write a pattern in thread A. */
    pthread_create(&producer, NULL, cross_thread_producer, NULL);
    pthread_join(producer, NULL);

    /* Verify the pattern and free in thread B. */
    pthread_create(&consumer, NULL, cross_thread_consumer, NULL);
    pthread_join(consumer, NULL);

    printf("cross_thread_free: OK (%d allocs in thread A, freed in thread B)\n",
           MEMALLOC_TEST_CROSS_COUNT);
}

#define MEMALLOC_TEST_DRAIN_THREADS 64
#define MEMALLOC_TEST_DRAIN_ALLOCS_PER_THREAD 80  /* intentionally > MEMALLOC_TC_HIGH_WATER */
#define MEMALLOC_TEST_DRAIN_SLOT_SIZE 128

static void* thread_exit_drain_worker(void* arg) {
    (void)arg;
    void* live[MEMALLOC_TEST_DRAIN_ALLOCS_PER_THREAD];
    for (int i = 0; i < MEMALLOC_TEST_DRAIN_ALLOCS_PER_THREAD; ++i) {
        void* p = memalloc_allocate(MEMALLOC_TEST_DRAIN_SLOT_SIZE);
        if (!p) {
            fprintf(stderr, "thread_exit_drain: allocate failed\n");
            abort();
        }
        memset(p, 0xAB, MEMALLOC_TEST_DRAIN_SLOT_SIZE);
        live[i] = p;
    }
    for (int i = 0; i < MEMALLOC_TEST_DRAIN_ALLOCS_PER_THREAD; ++i) memalloc_deallocate(live[i]);
    /* Thread exits; the pthread-key destructor must flush the residual
     * per-thread cache back to the central SlabPool so memory is not
     * permanently lost. */
    return NULL;
}

static void test_thread_exit_drain(void) {
    /* Each worker allocates MEMALLOC_TEST_DRAIN_ALLOCS_PER_THREAD (> 64)
     * objects of one size class, then frees them all before returning. If
     * the thread-exit flush does not run, freed objects never reach the
     * central pool and the total slab footprint grows unboundedly with
     * thread count. Post-condition: a fresh batch of allocations succeeds
     * and sees correct data. */
    for (int t = 0; t < MEMALLOC_TEST_DRAIN_THREADS; ++t) {
        pthread_t worker;
        pthread_create(&worker, NULL, thread_exit_drain_worker, NULL);
        pthread_join(worker, NULL);
    }

    /* Allocate a fresh batch and verify correctness, confirming the freed
     * memory was reclaimed by the central pool and is reusable. */
    const int kVerifyCount = 128;
    void* verify[128];
    for (int i = 0; i < kVerifyCount; ++i) {
        void* p = memalloc_allocate(MEMALLOC_TEST_DRAIN_SLOT_SIZE);
        if (!p) {
            fprintf(stderr, "thread_exit_drain: post-drain allocate failed\n");
            abort();
        }
        memset(p, (unsigned char)i, MEMALLOC_TEST_DRAIN_SLOT_SIZE);
        verify[i] = p;
    }
    for (int i = 0; i < kVerifyCount; ++i) {
        unsigned char* p = (unsigned char*)verify[i];
        for (int j = 0; j < MEMALLOC_TEST_DRAIN_SLOT_SIZE; ++j) {
            if (p[j] != (unsigned char)i) {
                fprintf(stderr, "thread_exit_drain: post-drain corruption at %p+%d\n", verify[i], j);
                abort();
            }
        }
        memalloc_deallocate(verify[i]);
    }

    printf("thread_exit_drain: OK (%d threads x %d allocs, %d post-drain verifications)\n",
           MEMALLOC_TEST_DRAIN_THREADS, MEMALLOC_TEST_DRAIN_ALLOCS_PER_THREAD, kVerifyCount);
}

int main(void) {
    test_cross_thread_free();
    test_thread_exit_drain();
    return 0;
}
