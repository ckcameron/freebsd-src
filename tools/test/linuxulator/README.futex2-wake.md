# FreeBSD Linuxulator — Linux 6.18 futex2 ABI, first four calls

Development branch: `linuxulator/futex2-wake-454`.

The changes register and provide candidate implementations for:

| Linux syscall | Number | Candidate behavior |
| --- | ---: | --- |
| `futex_waitv` | 449 | Wait on 1–128 futexes, return awakened member index |
| `futex_wake` | 454 | Wake by bitmask with futex2 strict count semantics |
| `futex_wait` | 455 | 32-bit word comparison plus absolute-time masked wait |
| `futex_requeue` | 456 | Compare source value; wake/requeue bounded number of waiters |

These are **not** claims of full Linux 6.18 kernel ABI parity.

## Implementation

The FreeBSD `umtx` subsystem provides futex keys and wait queues.
The Linuxulator dispatcher and 64-bit timespec translation live in
`sys/compat/linux/linux_futex.{c,h}`; a small opt-in multikey waiter
extension lives in `sys/{sys/umtxvar.h,kern/kern_umtx.c}`.

`futex_waitv` allocates separate keyed registrations (up to 128), associates
them with one group wake state, and serializes the wake-before-sleep handoff
under a group mutex. Each registration is removed under its own chain mutex
before its key is released or the group state destroyed. Wakes through the
legacy futex ABI and futex2 can both notify this group.

Requeue now acquires key-chain locks in a consistent order and enforces the
exact wake and requeue quotas, including zero-count requests.

**Known semantic limitation:** Requeueing an active **vector** waiter currently
notifies/wakes that vector member instead of transferring its registration
to the destination key. This avoids unsafe reassignment of shared-mapping key
references, but is not fully Linux-compatible behavior. Plain futex waiters
are requeued normally. This limitation should be addressed before claiming
complete `futex_waitv`/`futex_requeue` interoperability.

Only 32-bit futex words are implemented. Futex2 8-, 16-, 64-bit, NUMA, and
memory-policy options are rejected. Timeouts use the 64-bit Linux
`__kernel_timespec` layout, even for Linux32, with overflow rejected where
the native kernel time type cannot represent the timestamp.

## Syscall regeneration (mandatory before merge)

Edit `syscalls.master` (the authoritative definitions); generated Linux
syscall headers, tables, and DTrace descriptors were updated for review.
**Regenerate on a native FreeBSD CURRENT build host, and check consistency.**

```sh
cd /usr/src
make -C sys/amd64/linux sysent
make -C sys/amd64/linux32 sysent
make -C sys/arm64/linux sysent
make -C sys/i386/linux sysent
git diff --check
git diff -- sys/amd64/linux sys/amd64/linux32 sys/arm64/linux sys/i386/linux
```

The Linux shadow-stack syscall (453) is reserved as an unimplemented slot
for ABIs other than amd64.

## Linux ELF conformance smoke tests

Compile on Linux:

```sh
cc -O2 -std=c11 -Wall -Wextra -Werror -pthread \
  tools/test/linuxulator/futex_wake_linux.c -o futex_wake_linux
cc -O2 -std=c11 -Wall -Wextra -Werror -pthread \
  tools/test/linuxulator/futex2_linux.c -o futex2_linux
./futex_wake_linux
./futex2_linux
```

Run the **same Linux ELF binaries** inside a disposable FreeBSD CURRENT
Linuxulator VM. Exit code 77 means unavailable syscall.

The regression suite checks invalid arguments, mismatched values, timeouts,
bitmask wakeups, legacy interoperability, vector return indices, bounded
requeue, and target wakeups. Further race/stress tests and external review
are necessary.

## Verification status

- Linux 6.18.57: raw reference syscall checks passed for all three new calls
  and interoperability with the existing `futex_wake` operation.
- FreeBSD kernel/module build: **not executed**.
- FreeBSD runtime validation: **not executed**.
- Official FreeBSD syscall table regeneration: **not executed**.
- Linux C smoke-test source compilation: **not yet verified**.

Keep this PR **draft**. Do not change `compat.linux.osrelease` or merge until
the FreeBSD build, generator consistency, and Linuxulator runtime behavior
have been verified.

References: Linux v6.18 `kernel/futex/syscalls.c`,
`kernel/futex/waitwake.c`, `kernel/futex/requeue.c`, and
`include/uapi/linux/futex.h`.
