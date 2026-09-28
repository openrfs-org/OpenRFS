/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_UNISTD_H
#define OPENRFS_UNISTD_H

#include <stddef.h>
#include <stdint.h>

#ifndef _SSIZE_T_DEFINED
typedef long ssize_t;
#define _SSIZE_T_DEFINED
#endif
typedef int64_t off_t;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define R_OK 4
#define W_OK 2
#define F_OK 0

ssize_t read(int descriptor, void *buffer, size_t length);
ssize_t write(int descriptor, const void *buffer, size_t length);
off_t lseek(int descriptor, off_t offset, int origin);
int close(int descriptor);
int dup(int descriptor);
int dup2(int source, int destination);
int dup3(int source, int destination, int flags);
int access(const char *path, int mode);
int unlink(const char *path);
int symlink(const char *target, const char *path);
int link(const char *source, const char *destination);
ssize_t readlink(const char *path, char *output, size_t capacity);
int rmdir(const char *path);
int fsync(int descriptor);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int microseconds);
int getpid(void);
int getppid(void);
int getpgrp(void);
int getpgid(int pid);
int setpgid(int pid, int pgid);
int setsid(void);
int getsid(int pid);
int chdir(const char *path);
char *getcwd(char *buffer, size_t size);
int fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execl(const char *path, const char *arg0, ...);
int execle(const char *path, const char *arg0, ...);
int pipe(int descriptors[2]);
int pipe2(int descriptors[2], int flags);

#endif
