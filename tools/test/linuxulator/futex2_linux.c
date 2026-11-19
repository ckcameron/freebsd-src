/*
 * Linux 6.18 futex2 syscall ABI regression tests.
 * SPDX-License-Identifier: BSD-2-Clause
 * Build: cc -O2 -std=c11 -Wall -Wextra -Werror -pthread futex2_linux.c -o futex2_linux
 * Run as a Linux ELF on Linux 6.18 and in the FreeBSD Linuxulator.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_futex_waitv
#define SYS_futex_waitv 449
#endif
#ifndef SYS_futex_wait
#define SYS_futex_wait 455
#endif
#ifndef SYS_futex_requeue
#define SYS_futex_requeue 456
#endif
#define F2_FLAGS (FUTEX2_SIZE_U32 | FUTEX2_PRIVATE)

static _Atomic uint32_t words[2];
static _Atomic int ready;
static int waiter_return;
static int waiter_errno;

enum waiter_kind { WAIT_SINGLE, WAIT_VECTOR, WAIT_REQUEUE };
static enum waiter_kind kind;

static void
check(const char *name, int ok)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        exit(EXIT_FAILURE);
}

static struct timespec
deadline(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 4;
    return ts;
}

static void *
waiter(void *unused)
{
    struct timespec ts = deadline();
    struct futex_waitv vector[2] = {
        {.val=0, .uaddr=(uintptr_t)&words[0], .flags=F2_FLAGS},
        {.val=0, .uaddr=(uintptr_t)&words[1], .flags=F2_FLAGS},
    };
    (void)unused;
    atomic_store_explicit(&ready, 1, memory_order_release);
    switch (kind) {
    case WAIT_SINGLE:
        waiter_return = (int)syscall(SYS_futex_wait, &words[0],
            0UL, 1UL, F2_FLAGS, &ts, CLOCK_MONOTONIC);
        break;
    case WAIT_VECTOR:
        waiter_return = (int)syscall(SYS_futex_waitv, vector,
            2U, 0U, &ts, CLOCK_MONOTONIC);
        break;
    case WAIT_REQUEUE:
        waiter_return = (int)syscall(SYS_futex, &words[0],
            FUTEX_WAIT_BITSET_PRIVATE, 0U, &ts, NULL, 1U);
        break;
    }
    waiter_errno = errno;
    return NULL;
}

static pthread_t
start(enum waiter_kind k)
{
    pthread_t th;
    struct timespec settle = {.tv_sec=0, .tv_nsec=50000000};
    kind = k;
    atomic_store(&ready, 0);
    atomic_store(&words[0], 0);
    atomic_store(&words[1], 0);
    waiter_return = -12345;
    waiter_errno = 0;
    check("pthread_create", pthread_create(&th, NULL, waiter, NULL) == 0);
    while (atomic_load_explicit(&ready, memory_order_acquire) == 0)
        sched_yield();
    nanosleep(&settle, NULL);
    return th;
}

int
main(void)
{
    struct timespec expired = {.tv_sec=0, .tv_nsec=1};
    struct futex_waitv vector[2] = {
        {.val=0, .uaddr=(uintptr_t)&words[0], .flags=F2_FLAGS},
        {.val=0, .uaddr=(uintptr_t)&words[1], .flags=F2_FLAGS},
    };
    pthread_t th;
    long ret;

    errno = 0;
    ret = syscall(SYS_futex_wait, &words[0], 1UL, 1UL,
        F2_FLAGS, NULL, CLOCK_MONOTONIC);
    if (ret == -1 && errno == ENOSYS) {
        fprintf(stderr, "SKIP: futex_wait unavailable\n");
        return 77;
    }
    check("futex_wait mismatched value returns EAGAIN",
        ret == -1 && errno == EAGAIN);

    errno = 0;
    ret = syscall(SYS_futex_wait, &words[0], 0UL, 0UL,
        F2_FLAGS, NULL, CLOCK_MONOTONIC);
    check("futex_wait zero wake mask returns EINVAL",
        ret == -1 && errno == EINVAL);

    errno = 0;
    ret = syscall(SYS_futex_wait, &words[0], 0UL, 1UL,
        F2_FLAGS, &expired, CLOCK_MONOTONIC);
    check("futex_wait absolute timeout returns ETIMEDOUT",
        ret == -1 && errno == ETIMEDOUT);

    errno = 0;
    ret = syscall(SYS_futex_waitv, vector, 0U, 0U,
        NULL, CLOCK_MONOTONIC);
    check("futex_waitv rejects zero element count",
        ret == -1 && errno == EINVAL);

    errno = 0;
    ret = syscall(SYS_futex_waitv, vector, 2U, 1U,
        NULL, CLOCK_MONOTONIC);
    check("futex_waitv rejects global flags",
        ret == -1 && errno == EINVAL);

    errno = 0;
    vector[1].val = 1;
    ret = syscall(SYS_futex_waitv, vector, 2U, 0U,
        NULL, CLOCK_MONOTONIC);
    check("futex_waitv rejects mismatched member",
        ret == -1 && errno == EAGAIN);
    vector[1].val = 0;

    th = start(WAIT_SINGLE);
    check("futex2 wake can wake futex2 wait",
        syscall(SYS_futex_wake, &words[0], 1UL, 1, F2_FLAGS) == 1);
    check("join single waiter", pthread_join(th, NULL) == 0);
    check("single futex wait returns zero", waiter_return == 0);

    th = start(WAIT_VECTOR);
    check("legacy wake can wake a multi-key waitv member",
        syscall(SYS_futex, &words[1], FUTEX_WAKE_BITSET_PRIVATE,
            1, NULL, NULL, 1) == 1);
    check("join multi waiter", pthread_join(th, NULL) == 0);
    check("waitv returns index 1", waiter_return == 1);

    th = start(WAIT_REQUEUE);
    ret = syscall(SYS_futex_requeue, vector, 0U, 0, 1);
    check("futex_requeue moves exactly one waiter", ret == 1);
    check("futex2 wake reaches requeued waiter",
        syscall(SYS_futex_wake, &words[1], 1UL, 1, F2_FLAGS) == 1);
    check("join requeued waiter", pthread_join(th, NULL) == 0);
    check("requeued waiter returns success", waiter_return == 0);

    errno = 0;
    vector[0].val = 1;
    ret = syscall(SYS_futex_requeue, vector, 0U, 0, 1);
    check("requeue mismatched source value returns EAGAIN",
        ret == -1 && errno == EAGAIN);
    return EXIT_SUCCESS;
}
