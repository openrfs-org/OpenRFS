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
[write](https://pubs.opengroup.org/onlinepubs/9799919799/functions/write.html),
[kill](https://pubs.opengroup.org/onlinepubs/9799919799/functions/kill.html),
[setpgid](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setpgid.html),
[getpgid](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpgid.html),
[setsid](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setsid.html),
[getsid](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getsid.html),
[chdir](https://pubs.opengroup.org/onlinepubs/9799919799/functions/chdir.html),
[getcwd](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getcwd.html),
[umask](https://pubs.opengroup.org/onlinepubs/9799919799/functions/umask.html),
and [signals](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaction.html).

| Operation | Status | Current behavior and evidence |
| --- | --- | --- |
| `getpid`, `getppid` | Implemented, native | Kernel assigned positive IDs and parent IDs; the native QEMU C program checks both after fork. IDs are never reused during one boot. |
| `fork` | Limited, native | Both processes resume at the call site. The child receives zero and a private eager copy of writable mapped pages; immutable authenticated executable pages share reference-counted frames. The parent receives the child's ID. Only the calling thread survives in the child. Stacks of vanished threads are omitted from the child, and a later child thread uses an unoccupied stack range. The SDK quiesces its allocator, standard streams, thread records, exit handlers, and descriptor table around fork, then drops vanished thread records in the child. The native QEMU C program checks private memory, inherited file position, stale sibling handles, allocation and thread creation in children forked from main and worker threads, three active children, and slot exhaustion. User mutexes and separately allocated `FILE` locks held by vanished threads are not repaired. There is no copy on write for private pages. Four process slots and the parent's manifest memory limit apply. |
| `pthread_create`, `pthread_join` | Limited, native SDK | The kernel retains a bounded number of live or unjoined threads from the package manifest. An exited thread keeps its result while a thread handle or blocked joiner refers to it. After the last handle closes and joiners have completed, a later create releases its stack and reuses the slot with a new generation. The native QEMU C program checks repeated create/join beyond the manifest's four-thread limit and refuses stale handles. Detached threads and POSIX cancellation are unsupported. |
| `wait`, `waitpid` | Limited, native | Positive child ID, `-1` for any child, `0` for the caller's current group, and a PID below `-1` for that process group are supported, with `WNOHANG`, exit status, `ECHILD`, and an unreaped zombie unless the parent explicitly ignores `SIGCHLD`. A blocked waiter then receives `ECHILD` after its last eligible child is reaped automatically. Other wait options return `EINVAL`. There is no handler driven `EINTR` or stopped/continued child state. Child faults currently appear as signal 11 in wait status, without a handler. Host and QEMU C tests check group selection, reaping, and bad user status pointers. |
| `_Exit` | Implemented, native | Terminates all threads in the process without running native dynamic finalizers or SDK exit handlers. Wait reports the low eight bits of the exit value. |
| Orphans | Limited, native | A parent's exit releases its zombie children and detaches live children. There is no init reparenting or subreaper. |
| `open`, `read`, `write`, `close`, `lseek` | Limited, native SDK | Files, pipes, and console descriptors 0, 1, and 2 work. Closed numbers can be reused. A regular file description's cursor is shared after duplication and fork. Pipe seeking returns `ESPIPE`. The SDK has 32 descriptors. Plain relative Data paths resolve against the process working directory; `/` and `Data:` paths resolve from the app's Data namespace root. The native QEMU C program checks System and Data file inheritance. |
| `dup`, `dup2`, `dup3`, `fcntl` | Limited, native SDK | `F_DUPFD`, `F_DUPFD_CLOEXEC`, `F_DUPFD_CLOFORK`, `F_GETFD`, `F_SETFD`, and `F_GETFL` work. `F_SETFL` changes `O_APPEND` on regular files and `O_NONBLOCK` on pipes; other status flags are unsupported. These flags follow shared file and pipe descriptions across dup and fork. Descriptor flags are per number. `FD_CLOFORK` is applied in the SDK child after fork; native handles have no close on fork flag. `FD_CLOEXEC` closes SDK descriptors in the supported exec path, while raw native handles have no close on exec flag. Host and QEMU C tests cover duplication, redirection, and flags. |
| `execve` | Limited, native SDK | An authenticated System manifest can replace a native image while retaining its PID, parent, session, process group, umask, ignored signal dispositions, console input, and non-`FD_CLOEXEC` SDK descriptors. The kernel stages the new image and checks it before commit; precommit failures leave the old image running. A postcommit teardown error is logged and cannot restore the old image. The target must have the same app identifier, data namespace, resource directory, and capabilities; resource ceilings cannot grow. A second QEMU C program checks fork→exec of a distinct signed image under the same app identity, PID and parent continuity, argv/envp, an inherited file description, close on exec, wait, and teardown census. It also forks and execs separate producer and consumer processes, redirects their standard streams through a pipe, and waits for both. The original QEMU C program checks malformed pointers, oversized vectors, and failed exec rollback. Live typed native handles or windows return `EBUSY`. Path length is at most 127 bytes, each vector at most 32 strings, and argv plus envp strings at most 8192 bytes; bad pointers return `EFAULT` and excess vectors/bytes return `E2BIG`. PATH-searching exec-family wrappers, Data-volume executables, shebang interpreters, arbitrary PIE/interpreters, and cross-app exec are unsupported. |
| `pipe`, `pipe2` | Limited, native SDK | A kernel pipe has a 4096-byte buffer; at most 16 pipes can be live. `PIPE_BUF` is 4096. Writes of at most that size are all-or-nothing, and blocking readers and writers park until the requested transfer can proceed. `pipe2` accepts `O_NONBLOCK`, `O_CLOEXEC`, and `O_CLOFORK`; descriptor duplication, fork, close, EOF, and `EPIPE` work. `F_SETFL` changes nonblocking status across duplicated and inherited references to the same endpoint. `fdopen` wraps a descriptor for buffered stdio and `fclose` closes it; the native QEMU C program checks read, write, flush, EOF, `ESPIPE`, and nonblocking `EAGAIN` through this stream. A write after the final reader closes sends default `SIGPIPE` and terminates the writer; if the writer ignores `SIGPIPE`, the write returns `EPIPE`. Pipe metadata remains incomplete. The native QEMU C program also checks a forked producer/consumer, a full-buffer blocked writer, atomic nonblocking writes, `SIGPIPE`, `EPIPE`, and redirection. |
| `kill`, `raise`, signals | Limited, native | `kill(pid, 0)` checks a live self or direct child. `kill(0, sig)` targets the caller's process group; `kill(-pgid, sig)` targets a group in the caller's native session. `SIGINT`, `SIGTERM`, `SIGKILL`, and `SIGPIPE` terminate eligible processes with signaled wait status by default. `signal()` accepts only `SIG_DFL` and `SIG_IGN` for `SIGINT`, `SIGTERM`, `SIGPIPE`, and `SIGCHLD`; the settings follow fork. Explicitly ignoring `SIGCHLD` discards future child exit status without leaving a zombie. `SIGKILL` cannot be ignored. Other positive PIDs fail with `EPERM` or `ESRCH`; `kill(-1, sig)` returns `ENOSYS`. Other signals return `ENOSYS`, and user handlers return `ENOTSUP`. There are no masks, user handler frames, signal return, or SIGCHLD delivery. A child fault produces signal 11 in wait status without a handler. Host and native QEMU C tests cover these defaults, group delivery, ignored actions, and wait status. |
| `umask` | Limited, native | Each native process starts with mask `0022`. `umask` returns the old mask, retains only permission bits, and fork copies it independently. The kernel applies it when a native call creates a file or directory. Host and native QEMU C tests check the mask and inheritance; encrypted Data QEMU tests check a created file's mode on FAT32 and ext4. Encrypted Data stores its mode in authenticated namespace metadata, while raw FAT32 has no POSIX permission bits. Existing objects are unaffected. This does not establish multiuser permission enforcement. |
| `getpgrp`, `getpgid`, `setpgid`, `getsid`, `setsid` | Limited, native | Each launched native process starts a session and group identified by its PID. Fork inherits both, and exec retains both. A process can change its own group or that of a direct child before the child execs; joining a group requires a live member in the same session. `setsid` creates a new session and group for a caller that is not already a group leader, without a controlling terminal; `getsid` reads the caller's or an accessible process's session ID. Host and QEMU C tests check group signal delivery, group wait selection, and child session creation. There is no `tcgetpgrp`, `tcsetpgrp`, foreground terminal, Ctrl-C fanout, or background job control. |
| `chdir`, `getcwd` | Limited, native | The kernel stores a Data working directory per process. `fork` copies it; successful `execve` retains it. The SDK resolves plain relative Data paths against that directory, including `.` and `..` without leaving the app's Data namespace. `/` and `Data:` start at its root. `getcwd` returns a Data-rooted absolute path. Host tests cover path normalization and SDK calls; a native QEMU C program checks relative file access, fork inheritance, and exec preservation. The working directory is path based: renaming or deleting its directory does not retain an inode reference. `chdir` into System is unsupported, and SDK paths remain limited to 127 input bytes. |
| Process root, credentials | Unsupported as POSIX process attributes | The app's Data namespace acts as a fixed path root, and the kernel applies package capabilities and Data session checks. There is no mutable per-process root or POSIX credential inheritance. This is not a multiuser permission boundary. |
| Linux `fork`, `exec`, `wait`, pipes, signals | Unsupported | The measured Linux echo/uname/cat syscall path refuses calls outside each profile. Native calls and Linux calls are not interchangeable. |

The SDK's `execv` and `execl` forward the startup environment, while `execle`
accepts an explicit environment vector. The SDK has no mutable `environ` or
`setenv`, and none of these wrappers searches `PATH`. The distinct-image QEMU C
program checks both inherited and explicit environment variants.

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

An exploratory encrypted FAT32 run that created and removed another directory
before package upload failed: the later upload write returned `EIO`. Its raw
image passed structural and plaintext checks, while the backing directory had
reached the FAT32 backend's 64-live-entry limit. The encrypted layer reported
`EIO` and revoked the session. Backend host tests now return `FULL` for physical
directory creation, segment creation, and shadow-write capacity failures while
keeping the prior encrypted content readable and the session usable. The exact
lower-level cause of that QEMU upload failure has not been isolated, and
encrypted object cleanup does not yet prevent the 64-entry physical directory
limit. This failure is not counted as a passing directory-mode test.

The current QEMU process coverage runs in `NATIVET.APP` and in a separate
`EXECMAIN.APP` to `EXECALT.APP` case. Both use compiled C programs and the
production native syscall path. The latter runs a two-command pipeline through
fork, exec, and descriptor redirection. They do not cover a BusyBox shell
script, interactive shell pipelines, signal handlers,
FAT32/ext4 inherited Data logout cases, or a POSIX shell.
The kernel keeps only the caller when a multithreaded process forks. The SDK
quiesces its internal locks around fork, but application mutexes and separately
allocated stream locks held by vanished threads are not repaired in the child.
