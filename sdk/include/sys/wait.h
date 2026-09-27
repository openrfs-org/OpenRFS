/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_SYS_WAIT_H
#define OPENRFS_SYS_WAIT_H

typedef int pid_t;

#define WNOHANG 1
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status) ((status) & 0x7f)

pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait(int *status);

#endif
