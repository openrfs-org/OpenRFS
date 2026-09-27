<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Process and POSIX compatibility status

This table describes the native SDK and production native scheduler. The Linux
compatibility path is a separate measured echo, uname, and cat profile; it does
not run a general Linux process or BusyBox shell. OpenRFS is not POSIX compliant.
The behavior here is compared with POSIX Issue 8 for
[fork](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fork.html),
[exec](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exec.html),
[wait](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wait.html),
[descriptors](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dup.html),
[pipes](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pipe.html),
[kill](https://pubs.opengroup.org/onlinepubs/9799919799/functions/kill.html),
and [signals](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaction.html).

| Operation | Status | Current behavior and evidence |
| --- | --- | --- |
| `getpid`, `getppid` | Implemented, native | Kernel assigned positive IDs and parent IDs; the native QEMU C program checks both after fork. IDs are never reused during one boot. |
| `fork` | Limited, native | Both processes resume at the call site. The child receives zero and a private eager copy of mapped pages; the parent receives the child's ID. Only the calling thread survives in the child. The native QEMU C program checks private memory, inherited file position, a stale sibling thread handle, three active children, and slot exhaustion. There is no copy on write. Four process slots and the parent's manifest memory limit apply. |
| `wait`, `waitpid` | Limited, native | Positive child ID and `-1` are supported, with `WNOHANG`, exit status, `ECHILD`, and an unreaped zombie. Other PID selectors and wait options return `EINVAL`. Child faults currently appear as signal 11 in wait status, without a signal implementation. Host and QEMU tests check reaping and bad user status pointers. |
| `_Exit` | Implemented, native | Terminates all threads in the process without running native dynamic finalizers or SDK exit handlers. Wait reports the low eight bits of the exit value. |
| Orphans | Limited, native | A parent's exit releases its zombie children and detaches live children. There is no init reparenting or subreaper. |
| `open`, `read`, `write`, `close`, `lseek` | Limited, native SDK | Files, pipes, and console descriptors 0, 1, and 2 work. Closed numbers can be reused. A regular file description's cursor is shared after duplication and fork. Pipe seeking returns `ESPIPE`. The SDK has 32 descriptors; paths are resolved by its existing app namespace parser, not by a process current directory. The native QEMU C program checks System and Data file inheritance. |
| `dup`, `dup2`, `dup3`, `fcntl` | Limited, native SDK | `F_DUPFD`, `F_DUPFD_CLOEXEC`, `F_DUPFD_CLOFORK`, `F_GETFD`, `F_SETFD`, and `F_GETFL` work. `F_SETFL` changes `O_APPEND` on regular files and `O_NONBLOCK` on pipes; other status flags are unsupported. These flags follow shared file and pipe descriptions across dup and fork. Descriptor flags are per number. `FD_CLOFORK` is applied in the SDK child after fork; native handles have no close on fork flag. `FD_CLOEXEC` is recorded but exec is unavailable. Host and QEMU C tests cover duplication, redirection, and flags. |
| `execve`, exec family | Unsupported | The SDK `execve` wrapper returns `ENOSYS`. No replacement image transaction, argument and environment transfer, or interpreter execution exists. Native loading admits authenticated static ET_EXEC and a bounded authenticated dynamic profile when launched by the existing package path; it is not an exec interface. |
| `pipe`, `pipe2` | Limited, native SDK | A kernel pipe has a 4096-byte buffer; at most 16 pipes can be live. `PIPE_BUF` is 4096. Writes of at most that size are all-or-nothing, and blocking readers and writers park until the requested transfer can proceed. `pipe2` accepts `O_NONBLOCK`, `O_CLOEXEC`, and `O_CLOFORK`; descriptor duplication, fork, close, EOF, and `EPIPE` work. `F_SETFL` changes nonblocking status across duplicated and inherited references to the same endpoint. A broken pipe returns `EPIPE` without `SIGPIPE` delivery. Pipe metadata and `fdopen` blocking behavior are incomplete. The native QEMU C program checks a forked producer/consumer, a full-buffer blocked writer, atomic nonblocking writes, EOF, `EPIPE`, and redirection. |
| `kill`, `raise`, signals | Limited, native | `kill(pid, 0)` checks a live self or direct child. `SIGINT`, `SIGTERM`, and `SIGKILL` terminate self or a direct child with signaled wait status; only default actions exist. Other positive PIDs fail with `EPERM` or `ESRCH`; process-group PID forms return `ENOSYS`. Other signals return `ENOSYS`, and `signal()` returns `ENOTSUP`. There is no signal disposition, mask, user handler frame, signal return, SIGCHLD delivery, or SIGPIPE delivery. A child fault produces signal 11 in wait status without a handler. Host and native QEMU C tests cover the limited default actions and wait status. |
| Process groups and terminal control | Unsupported | No `setpgid`, `tcsetpgrp`, foreground pipeline, Ctrl-C fanout, or job control exists. |
| Process cwd, root, umask, credentials | Unsupported as POSIX process attributes | The SDK uses its existing application rooted path parser and the kernel applies package capabilities and Data session checks. There is no per process cwd, root, umask, or POSIX credential inheritance. This is not a multiuser permission boundary. |
| Linux `fork`, `exec`, `wait`, pipes, signals | Unsupported | The measured Linux echo/uname/cat syscall path refuses calls outside each profile. Native calls and Linux calls are not interchangeable. |

Fork inherits native file and pipe handles, the underlying VFS open file
description, and live pipe endpoint references.
Windows, sockets, package controller handles, timers, and other typed native
handles are explicitly dropped in the child. The existing package manifest and
capabilities are copied without expansion. Data file operations still check the
current login session; this change does not establish a new encrypted Data
authorization or multiuser security model.

The encrypted Data QEMU test runs a compiled C child with an inherited Data
file handle after login on FAT32 and ext4. The child reads one byte, and the
parent reads the remaining bytes through the shared file position. Both runs
also inspect the raw Data image for plaintext markers. A VFS host test checks
that an extra retained file reference cannot read or write after the session
epoch changes, including after login resumes. A child left alive across logout,
password rotation, or account deletion has not been tested in QEMU.

The current QEMU process coverage runs in `NATIVET.APP`, a real compiled C
program using the production native syscall path. It does not cover fork plus
exec, separate child executable programs, a BusyBox shell script, multi-command pipelines,
signal handlers, FAT32/ext4 inherited Data logout cases, or a POSIX shell.
The kernel keeps only the caller when a multithreaded process forks, but SDK
locks held by vanished threads are not generally repaired in the child.
