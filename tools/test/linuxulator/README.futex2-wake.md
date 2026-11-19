# Linux futex2 wake(454): FreeBSD Linuxulator development note

Target: Linux 6.18 userspace ABI, FreeBSD CURRENT `main`.

## Scope

Adds Linux futex2 `futex_wake` syscall number 454 to amd64, arm64, i386, and
amd64 Linux32 syscall tables. The handler delegates 32-bit private and shared
wake operations to Linuxulator's existing futex wait queues.

The syscall type definitions live in `sys/compat/linux/linux_futex.h`. The new
entry point and existing internal helper live in
`sys/compat/linux/linux_futex.c`.

The implementation explicitly rejects unsupported futex sizes and NUMA /
memory-policy extensions with `EINVAL`; it is not full Linux futex2 support.
This patch does not change `compat.linux.osrelease`.

## Generated interfaces

`syscalls.master` is authoritative. The accompanying generated
`linux{,32}_{proto,syscall,sysent,syscalls,systrace_args}` files have been
updated for review. **Before merge, rerun FreeBSD's actual generator** and
confirm the output is identical to the committed files:

```sh
cd /usr/src
make -C sys/amd64/linux sysent
make -C sys/amd64/linux32 sysent
make -C sys/arm64/linux sysent
make -C sys/i386/linux sysent
git diff --check
git diff -- sys/{amd64,arm64,i386}
```

The 453 slot is left unimplemented on non-amd64 ABIs. The Linux kernel
6.18 syscall number definitions are in
`arch/x86/entry/syscalls/syscall_64.tbl` and
`include/uapi/asm-generic/unistd.h`.

## Reference smoke test

Build **as a Linux ELF binary** (not as a native FreeBSD program):

```sh
cc -O2 -std=c11 -Wall -Wextra -Werror -pthread \
  -o futex_wake_linux tools/test/linuxulator/futex_wake_linux.c
./futex_wake_linux
```

Copy the same Linux ELF to a **disposable FreeBSD CURRENT VM** and run it
under Linuxulator. Exit status 77 means syscall 454 is absent.
Tests check flag and wake-mask validation, zero-wake behavior, and
cross-compatibility with legacy futex bitset waits. They are not a proof of
race-freedom or complete ABI parity.

## Validation status

- Linux 6.18.57: compiled with GCC and passed the 11 smoke-test assertions.
- FreeBSD kernel/module build: **not performed**.
- FreeBSD Linuxulator runtime test: **not performed**.
- Official FreeBSD makesyscalls regeneration comparison: **not performed**.

Keep this as a draft until all three outstanding checks pass. No
kernel-version claim or compatibility sysctl change should accompany this
patch.
