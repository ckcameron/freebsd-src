/*
 * Linux 6.18 reference test for futex_wake(454).
 * Build on Linux: cc -O2 -std=c11 -Wall -Wextra -pthread -o futex_wake_linux futex_wake_linux.c
 * Run the resulting Linux ELF on Linux and then under FreeBSD's Linuxulator.
 * Uses raw system calls; no Linux-specific userspace library required.
 * SPDX-License-Identifier: BSD-2-Clause
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_futex_wake
#define SYS_futex_wake 454
#endif
#ifndef FUTEX2_PRIVATE
#define FUTEX2_PRIVATE FUTEX_PRIVATE_FLAG
#endif
#ifndef FUTEX2_SIZE_U32
#define FUTEX2_SIZE_U32 2
#endif

static _Atomic uint32_t futex_word;
static _Atomic int ready;
static int waiter_result;

static long
wake_syscall(unsigned long mask, int nr, unsigned int flags)
{
    return syscall(SYS_futex_wake, &futex_word, mask, nr, flags);
}

static void
check(const char *label, int condition)
{
    printf("%s: %s\n", condition ? "PASS" : "FAIL", label);
    if (!condition)
        exit(1);
}

static void *
waiter(void *unused)
{
    struct timespec abs;
    (void)unused;
    clock_gettime(CLOCK_MONOTONIC, &abs);
    abs.tv_sec += 4;
    atomic_store_explicit(&ready, 1, memory_order_release);
    waiter_result = (int)syscall(SYS_futex, &futex_word,
        FUTEX_WAIT_BITSET_PRIVATE, 0, &abs, NULL, 1u);
    if (waiter_result < 0)
        waiter_result = -errno;
    return NULL;
}

int
main(void)
{
    pthread_t th;
    unsigned int valid = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE;
    struct timespec short_sleep = {.tv_sec = 0, .tv_nsec = 1000000};
    long r;

    errno = 0;
    r = wake_syscall(1, 1, valid);
    if (r < 0 && errno == ENOSYS) {
        fprintf(stderr, "SKIP: futex_wake(454) not available\n");
        return 77;
    }
    check("valid futex2 wake with no waiters returns zero", r == 0);

    errno = 0;
    r = wake_syscall(0, 1, valid);
    check("zero bitmask rejected", r == -1 && errno == EINVAL);

    errno = 0;
    r = wake_syscall(1, 1, valid | 0x80000000u);
    check("unknown flag rejected", r == -1 && errno == EINVAL);

    errno = 0;
    r = wake_syscall(1, 1, FUTEX2_PRIVATE);
    check("non-32-bit futex size rejected", r == -1 && errno == EINVAL);

#if __SIZEOF_LONG__ > 4
    errno = 0;
    r = wake_syscall(1UL << 32, 1, valid);
    check("mask wider than 32 bits rejected", r == -1 && errno == EINVAL);
#endif

    check("pthread_create", pthread_create(&th, NULL, waiter, NULL) == 0);
    while (atomic_load_explicit(&ready, memory_order_acquire) == 0)
        sched_yield();
    /* Allow entry to legacy WAIT_BITSET before observing strict wake results. */
    struct timespec settle = {.tv_sec = 0, .tv_nsec = 50000000};
    nanosleep(&settle, NULL);
    check("zero wake count does not wake a sleeping waiter",
        wake_syscall(1, 0, valid) == 0);
    check("nonmatching bitset does not wake a sleeping waiter",
        wake_syscall(2, 1, valid) == 0);
    /* Retry a matching wake only to cover an ordinary scheduling race. */
    for (int i = 0; i < 2000; i++) {
        r = wake_syscall(1, 1, valid);
        if (r != 0)
            break;
        nanosleep(&short_sleep, NULL);
    }
    check("wake one legacy FUTEX_WAIT_BITSET_PRIVATE waiter", r == 1);
    check("pthread_join", pthread_join(th, NULL) == 0);
    check("waiter returned success", waiter_result == 0);
    return 0;
}
