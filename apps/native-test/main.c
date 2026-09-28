/* SPDX-License-Identifier: GPL-3.0-only */
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openrfs/event.h>
#include <openrfs/runtime.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static _Thread_local unsigned long tls_value = 17U;

int native_state_round_trip(uint64_t deadline_ns, uint64_t seed);
extern const uint8_t native_initial_fpu_state[512];

static int initial_state_is_clean(void)
{
    const uint8_t *const state = native_initial_fpu_state;
    uint32_t mxcsr;

    (void)memcpy(&mxcsr, state + 24U, sizeof(mxcsr));
    if (state[0] != 0x7FU || state[1] != 0x03U || state[4] != 0U ||
        mxcsr != UINT32_C(0x1F80)) {
        return 0;
    }
    for (size_t index = 160U; index < 416U; ++index) {
        if (state[index] != 0U) {
            return 0;
        }
    }
    return 1;
}

static void *thread_probe(void *argument)
{
    const unsigned long expected = (unsigned long)(uintptr_t)argument;

    tls_value = expected;
    for (unsigned int iteration = 0U; iteration < 32U; ++iteration) {
        if (native_state_round_trip(openrfs_monotonic_ns() + 1000000U,
                expected * 257U + iteration) != 0 || tls_value != expected) {
            return (void *)(uintptr_t)1U;
        }
    }
    return NULL;
}

static int syscall_performance_probe(void)
{
    const unsigned int iterations = 1024U;
    const uint64_t started = openrfs_monotonic_ns();

    for (unsigned int iteration = 0U; iteration < iterations; ++iteration) {
        if (openrfs_syscall0(OPENRFS_SYS_ABI_VERSION) != OPENRFS_ABI_VERSION) {
            return 14;
        }
    }
    const uint64_t elapsed = openrfs_monotonic_ns() - started;

    printf("OPENRFS PERF syscall iterations=%u total_ns=%llu average_ns=%llu\n",
        iterations, (unsigned long long)elapsed,
        (unsigned long long)(elapsed / iterations));
    return 0;
}

static int file_performance_probe(void)
{
    enum { BUFFER_BYTES = 4096, FILE_BYTES = 65536 };
    uint8_t buffer[BUFFER_BYTES];
    uint64_t write_started;
    uint64_t write_elapsed;
    uint64_t read_started;
    uint64_t read_elapsed;
    long file;

    for (size_t index = 0U; index < sizeof(buffer); ++index) {
        buffer[index] = (uint8_t)(index * 37U + 11U);
    }
    file = openrfs_file_open(OPENRFS_VOLUME_DATA, "TMP/PERF.BIN",
        OPENRFS_OPEN_WRITE | OPENRFS_OPEN_CREATE | OPENRFS_OPEN_TRUNCATE);
    if (file < 0) return 341;
    write_started = openrfs_monotonic_ns();
    for (size_t offset = 0U; offset < FILE_BYTES; offset += sizeof(buffer)) {
        if (openrfs_file_write((openrfs_handle_t)file, buffer,
                sizeof(buffer)) != (long)sizeof(buffer)) {
            return 342;
        }
    }
    write_elapsed = openrfs_monotonic_ns() - write_started;
    if (openrfs_handle_close((openrfs_handle_t)file) != 0) return 343;
    file = openrfs_file_open(OPENRFS_VOLUME_DATA, "TMP/PERF.BIN",
        OPENRFS_OPEN_READ);
    if (file < 0) return 344;
    read_started = openrfs_monotonic_ns();
    for (size_t offset = 0U; offset < FILE_BYTES; offset += sizeof(buffer)) {
        if (openrfs_file_read((openrfs_handle_t)file, buffer,
                sizeof(buffer)) != (long)sizeof(buffer) ||
            buffer[0] != UINT8_C(11) || buffer[sizeof(buffer) - 1U] !=
                (uint8_t)((sizeof(buffer) - 1U) * 37U + 11U)) {
            return 345;
        }
    }
    read_elapsed = openrfs_monotonic_ns() - read_started;
    if (openrfs_handle_close((openrfs_handle_t)file) != 0 ||
        openrfs_path_unlink(OPENRFS_VOLUME_DATA, "TMP/PERF.BIN") != 0) {
        return 346;
    }
    printf("OPENRFS PERF file sequential_bytes=%u write_ns=%llu read_ns=%llu\n",
        FILE_BYTES, (unsigned long long)write_elapsed,
        (unsigned long long)read_elapsed);
    return 0;
}

static int memory_and_pointer_probes(void)
{
    struct openrfs_memory_map_response split = {0U, 0U, 0U, 0U};
    struct openrfs_memory_map_response mappings[16];
    struct openrfs_memory_map_response ignored = {0U, 0U, 0U, 0U};
    const struct openrfs_memory_map_request bad_flags = {
        sizeof(bad_flags), OPENRFS_ABI_VERSION, OPENRFS_ABI_PAGE_SIZE, 0U,
        UINT32_C(0x80000000), 0U
    };
    size_t mapping_count = 0U;
    long exhausted = 0;

    if (openrfs_syscall2(OPENRFS_SYS_MEMORY_MAP,
            (uint64_t)(uintptr_t)&bad_flags,
            (uint64_t)(uintptr_t)&ignored) != -OPENRFS_EINVAL ||
        openrfs_random((void *)(uintptr_t)UINT64_C(0x12345000), 1U) !=
            -OPENRFS_EFAULT ||
        openrfs_memory_allocate(2U * OPENRFS_ABI_PAGE_SIZE,
            OPENRFS_MEMORY_READ | OPENRFS_MEMORY_WRITE, &split) != 0) {
        return 20;
    }
    {
        volatile uint8_t *edge = (volatile uint8_t *)(uintptr_t)
            (split.address + OPENRFS_ABI_PAGE_SIZE - 1U);

        *edge = UINT8_C(0xA5);
        if (openrfs_memory_release(split.address + OPENRFS_ABI_PAGE_SIZE,
                OPENRFS_ABI_PAGE_SIZE) != 0 ||
            openrfs_random((void *)(uintptr_t)(split.address +
                OPENRFS_ABI_PAGE_SIZE - 1U), 2U) != -OPENRFS_EFAULT ||
            *edge != UINT8_C(0xA5) ||
            openrfs_memory_release(split.address, OPENRFS_ABI_PAGE_SIZE) != 0) {
            return 21;
        }
    }
    while (mapping_count < sizeof(mappings) / sizeof(mappings[0])) {
        const long status = openrfs_memory_allocate(2U * 1024U * 1024U,
            OPENRFS_MEMORY_READ | OPENRFS_MEMORY_WRITE,
            &mappings[mapping_count]);

        if (status < 0) {
            exhausted = status;
            break;
        }
        ++mapping_count;
    }
    if (exhausted != -OPENRFS_ENOMEM || mapping_count == 0U) {
        return 22;
    }
    while (mapping_count != 0U) {
        --mapping_count;
        if (openrfs_memory_release(mappings[mapping_count].address,
                mappings[mapping_count].length) != 0) {
            return 23;
        }
    }
    return 0;
}

static int process_cwd_probe(void);
static int exec_probe(void);

static int file_and_handle_probes(void)
{
    static const char replacement[] = "replacement";
    struct openrfs_volume_space space = {0U, 0U, 0U, 0U, 0U, 0U};
    struct openrfs_directory_entry entry;
    char bytes[sizeof(replacement)];
    long file;
    long duplicate;
    long directory;
    int found = 0;

    if (openrfs_file_open(OPENRFS_VOLUME_DATA, "../ESCAPE.TXT",
            OPENRFS_OPEN_READ) != -OPENRFS_EINVAL ||
        openrfs_file_open(OPENRFS_VOLUME_SYSTEM, "../NATIVET.APP",
            OPENRFS_OPEN_READ) != -OPENRFS_EINVAL ||
        openrfs_syscall0(OPENRFS_SYS_STREAM_OPEN) != -OPENRFS_EACCES ||
        openrfs_syscall0(UINT64_C(0xFFFF)) != -OPENRFS_ENOSYS) {
        return 24;
    }
    if (openrfs_path_mkdir(OPENRFS_VOLUME_DATA, "TMP") != 0) {
        return 25;
    }
    {
        const int performance = file_performance_probe();

        if (performance != 0) return performance;
    }
    file = openrfs_file_open(OPENRFS_VOLUME_DATA, "TMP/A.TXT",
        OPENRFS_OPEN_READ | OPENRFS_OPEN_WRITE | OPENRFS_OPEN_CREATE |
            OPENRFS_OPEN_TRUNCATE);
    if (file < 0 || openrfs_timer_set((openrfs_handle_t)file,
            openrfs_monotonic_ns()) != -OPENRFS_EBADF) {
        return 26;
    }
    duplicate = openrfs_handle_duplicate((openrfs_handle_t)file);
    if (duplicate < 0 || openrfs_handle_close((openrfs_handle_t)file) != 0 ||
        openrfs_file_read((openrfs_handle_t)file, bytes, 1U) != -OPENRFS_ESTALE ||
        openrfs_handle_close((openrfs_handle_t)file) != -OPENRFS_ESTALE ||
        openrfs_file_write((openrfs_handle_t)duplicate, "abcdef", 6U) != 6 ||
        openrfs_handle_close((openrfs_handle_t)duplicate) != 0) {
        return 27;
    }
    puts("OPENRFS STORAGE typed duplicate stale-handle PASS");
    if (openrfs_path_truncate(OPENRFS_VOLUME_DATA, "TMP/A.TXT", 3U) != 0 ||
        openrfs_path_rename(OPENRFS_VOLUME_DATA, "TMP/A.TXT", "TMP/B.TXT") !=
            0) {
        return 28;
    }
    puts("OPENRFS STORAGE truncate rename PASS");
    file = openrfs_file_open(OPENRFS_VOLUME_DATA, "TMP/C.TXT",
        OPENRFS_OPEN_WRITE | OPENRFS_OPEN_CREATE | OPENRFS_OPEN_TRUNCATE);
    if (file < 0 || openrfs_file_write((openrfs_handle_t)file, replacement,
            sizeof(replacement) - 1U) != (long)(sizeof(replacement) - 1U) ||
        openrfs_handle_close((openrfs_handle_t)file) != 0 ||
        openrfs_path_replace(OPENRFS_VOLUME_DATA, "TMP/C.TXT", "TMP/B.TXT") !=
            0) {
        return 29;
    }
    puts("OPENRFS STORAGE replacement PASS");
    file = openrfs_file_open(OPENRFS_VOLUME_DATA, "TMP/B.TXT", OPENRFS_OPEN_READ);
    if (file < 0 || openrfs_file_read((openrfs_handle_t)file, bytes,
            sizeof(bytes)) != (long)(sizeof(replacement) - 1U) ||
        memcmp(bytes, replacement, sizeof(replacement) - 1U) != 0 ||
        openrfs_handle_close((openrfs_handle_t)file) != 0) {
        return 30;
    }
    directory = openrfs_directory_open(OPENRFS_VOLUME_DATA, "TMP");
    if (directory < 0) {
        return 31;
    }
    for (;;) {
        const long status = openrfs_directory_read((openrfs_handle_t)directory,
            &entry);

        if (status < 0) {
            return 32;
        }
        if (status == 0) {
            break;
        }
        if (entry.name_length == 5U &&
            memcmp(entry.name, "b.txt", 5U) == 0) {
            found = 1;
        }
    }
    if (!found) return 331;
    if (openrfs_handle_close((openrfs_handle_t)directory) != 0) return 332;
    puts("OPENRFS STORAGE directory enumeration PASS");
    if (openrfs_volume_space(OPENRFS_VOLUME_DATA, &space) != 0) return 333;
    if (space.total_bytes == 0U || space.free_bytes >= space.total_bytes) {
        return 334;
    }
    if (openrfs_volume_sync(OPENRFS_VOLUME_DATA) != 0) return 335;
    {
        const int cwd_result = process_cwd_probe();
        const int exec_result = cwd_result == 0 ? exec_probe() : 0;

        if (cwd_result != 0) return cwd_result;
        if (exec_result != 0) return exec_result;
    }
    if (openrfs_path_unlink(OPENRFS_VOLUME_DATA, "TMP/B.TXT") != 0) return 336;
    if (openrfs_path_unlink(OPENRFS_VOLUME_DATA, "TMP") != 0) return 337;
    if (openrfs_volume_sync(OPENRFS_VOLUME_DATA) != 0) return 338;
    puts("OPENRFS STORAGE sync cleanup PASS");
    return 0;
}

static int timer_probe(void)
{
    struct openrfs_wait_item item;
    const long timer = openrfs_timer_create();
    uint64_t now;

    if (timer < 0) {
        return 34;
    }
    now = openrfs_monotonic_ns();
    item = (struct openrfs_wait_item){(openrfs_handle_t)timer,
        OPENRFS_WAIT_SIGNALED, 0U};
    if (openrfs_timer_set((openrfs_handle_t)timer, now + UINT64_C(1000000)) != 0 ||
        openrfs_wait(&item, 1U, now + UINT64_C(20000000)) != 1 ||
        item.ready != OPENRFS_WAIT_SIGNALED) {
        return 35;
    }
    now = openrfs_monotonic_ns();
    item.ready = 0U;
    if (openrfs_timer_set((openrfs_handle_t)timer, now + UINT64_C(1000000000)) !=
            0 || openrfs_wait(&item, 1U, now) != -OPENRFS_ETIMEDOUT ||
        openrfs_cancel((openrfs_handle_t)timer) != 0 ||
        openrfs_handle_close((openrfs_handle_t)timer) != 0) {
        return 36;
    }
    return 0;
}

static int process_probe(void)
{
    volatile int private_value = 7;
    char byte = 0;
    int status = 0;
    const int parent_pid = getpid();
    const int descriptor = open("System:RESOURCE.TXT", O_RDONLY);

    if (parent_pid <= 0 || getppid() != 0 || descriptor < 3) return 41;
    if (fflush(NULL) != 0) return 42;
    const int child_pid = fork();

    if (child_pid < 0) return 43;
    if (child_pid == 0) {
        private_value = 19;
        if (getpid() == parent_pid || getppid() != parent_pid ||
            read(descriptor, &byte, 1U) != 1 || byte != 'O') {
            _Exit(44);
        }
        _Exit(23);
    }
    if (child_pid == parent_pid || waitpid(child_pid, &status, 0) != child_pid ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 23 ||
        private_value != 7 || read(descriptor, &byte, 1U) != 1 ||
        byte != 'p' || close(descriptor) != 0 ||
        waitpid(child_pid, NULL, WNOHANG) != -1 || errno != ECHILD) {
        return 45;
    }
    const int data_descriptor = open("FOUND.TXT", O_RDONLY);

    if (data_descriptor < 3) return 63;
    const int data_child = fork();

    if (data_child < 0) return 64;
    if (data_child == 0) {
        _Exit(read(data_descriptor, &byte, 1U) == 1 && byte == 'n' ? 0 : 65);
    }
    if (waitpid(data_child, &status, 0) != data_child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        read(data_descriptor, &byte, 1U) != 1 || byte != 'a' ||
        close(data_descriptor) != 0) {
        return 66;
    }
    puts("OPENRFS PROCESS fork wait private-memory shared-offset PASS");
    return 0;
}

static int descriptor_probe(void)
{
    static const char message[] = "native descriptor redirection\n";
    char received[sizeof(message)] = {0};
    char tail = 0;
    int status;
    const int saved_stdout = dup(STDOUT_FILENO);
    const int file = open("REDIR.TXT", O_CREAT | O_TRUNC | O_RDWR, 0600);

    if (saved_stdout < 3 || file < 3 ||
        fcntl(file, F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(file, F_GETFD) != FD_CLOEXEC ||
        dup2(file, STDOUT_FILENO) != STDOUT_FILENO ||
        fcntl(STDOUT_FILENO, F_GETFD) != 0 ||
        fputs(message, stdout) == EOF || fflush(stdout) != 0 ||
        dup2(saved_stdout, STDOUT_FILENO) != STDOUT_FILENO ||
        close(saved_stdout) != 0 ||
        lseek(file, 0, SEEK_SET) != 0 ||
        read(file, received, sizeof(message) - 1U) !=
            (ssize_t)(sizeof(message) - 1U) ||
        memcmp(received, message, sizeof(message) - 1U) != 0) {
        return 58;
    }
    const int duplicate = fcntl(file, F_DUPFD_CLOFORK, 3);

    if (duplicate < 3 || fcntl(duplicate, F_GETFD) != FD_CLOFORK) {
        return 59;
    }
    if (fcntl(duplicate, F_SETFL, O_APPEND) != 0 ||
        fcntl(file, F_GETFL) != (O_RDWR | O_APPEND) ||
        lseek(file, 0, SEEK_SET) != 0 || write(duplicate, "!", 1U) != 1 ||
        lseek(file, sizeof(message) - 1U, SEEK_SET) !=
            (off_t)(sizeof(message) - 1U) ||
        read(file, &tail, 1U) != 1 || tail != '!' ||
        fcntl(file, F_SETFL, O_RDWR) != 0 ||
        fcntl(duplicate, F_GETFL) != O_RDWR) return 83;
    const int child = fork();

    if (child < 0) return 60;
    if (child == 0) {
        if (fcntl(duplicate, F_GETFD) != -1 || errno != EBADF ||
            fcntl(file, F_GETFD) != FD_CLOEXEC ||
            fcntl(file, F_SETFL, O_APPEND) != 0) {
            _Exit(61);
        }
        _Exit(0);
    }
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 ||
        fcntl(file, F_GETFL) != (O_RDWR | O_APPEND) ||
        close(duplicate) != 0 ||
        close(file) != 0) {
        return 62;
    }
    puts("OPENRFS DESCRIPTOR dup redirection flags fork inheritance PASS");
    return 0;
}

static int pipe_probe(void)
{
    static const char full[PIPE_BUF] = {0};
    int ends[2];
    int status;
    char bytes[16] = {0};

    if (signal(SIGPIPE, SIG_IGN) != SIG_DFL ||
        pipe2(ends, O_NONBLOCK) != 0 ||
        fcntl(ends[0], F_GETFL) != (O_RDONLY | O_NONBLOCK) ||
        read(ends[0], bytes, 1U) != -1 || errno != EAGAIN ||
        write(ends[1], "ab", 2U) != 2 ||
        read(ends[0], bytes, 2U) != 2 ||
        bytes[0] != 'a' || bytes[1] != 'b' ||
        write(ends[1], full, sizeof(full)) != (ssize_t)sizeof(full) ||
        write(ends[1], "x", 1U) != -1 || errno != EAGAIN ||
        read(ends[0], bytes, 1U) != 1 ||
        write(ends[1], "xy", 2U) != -1 || errno != EAGAIN ||
        lseek(ends[0], 0, SEEK_SET) != -1 || errno != ESPIPE ||
        close(ends[0]) != 0 ||
        write(ends[1], "x", 1U) != -1 || errno != EPIPE ||
        close(ends[1]) != 0 ||
        signal(SIGPIPE, SIG_DFL) != SIG_IGN) return 67;

    if (pipe2(ends, O_NONBLOCK) != 0) return 78;
    const int copied_reader = dup(ends[0]);

    if (copied_reader < 0 ||
        fcntl(copied_reader, F_SETFL, O_RDONLY) != 0 ||
        fcntl(ends[0], F_GETFL) != O_RDONLY ||
        fcntl(ends[0], F_SETFL, O_NONBLOCK) != 0 ||
        fcntl(copied_reader, F_GETFL) != (O_RDONLY | O_NONBLOCK) ||
        close(copied_reader) != 0) return 79;
    const int flag_child = fork();

    if (flag_child < 0) return 80;
    if (flag_child == 0) {
        if (fcntl(ends[1], F_SETFL, O_WRONLY) != 0) _Exit(81);
        _Exit(0);
    }
    if (waitpid(flag_child, &status, 0) != flag_child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        fcntl(ends[1], F_GETFL) != O_WRONLY ||
        close(ends[0]) != 0 || close(ends[1]) != 0) return 82;

    if (pipe(ends) != 0) return 68;
    const int child = fork();

    if (child < 0) return 69;
    if (child == 0) {
        if (close(ends[0]) != 0 || write(ends[1], "producer", 8U) != 8 ||
            close(ends[1]) != 0) _Exit(70);
        _Exit(0);
    }
    if (close(ends[1]) != 0 || read(ends[0], bytes, sizeof(bytes)) != 8 ||
        memcmp(bytes, "producer", 8U) != 0 ||
        waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || read(ends[0], bytes, 1U) != 0 ||
        close(ends[0]) != 0) return 71;

    if (fflush(stdout) != 0 || pipe(ends) != 0) return 72;
    const int saved = dup(STDOUT_FILENO);

    if (saved < 0 || dup2(ends[1], STDOUT_FILENO) != STDOUT_FILENO ||
        write(STDOUT_FILENO, "redirect", 8U) != 8 ||
        dup2(saved, STDOUT_FILENO) != STDOUT_FILENO ||
        close(saved) != 0 || close(ends[1]) != 0 ||
        read(ends[0], bytes, sizeof(bytes)) != 8 ||
        memcmp(bytes, "redirect", 8U) != 0 ||
        close(ends[0]) != 0) return 73;

    int acknowledgement[2];

    if (pipe(ends) != 0 || pipe(acknowledgement) != 0 ||
        write(ends[1], full, sizeof(full) - 1U) !=
            (ssize_t)(sizeof(full) - 1U)) return 74;
    const int consumer = fork();

    if (consumer < 0) return 75;
    if (consumer == 0) {
        if (close(ends[1]) != 0 || close(acknowledgement[1]) != 0 ||
            read(ends[0], bytes, 1U) != 1 ||
            read(acknowledgement[0], bytes, 1U) != 1 ||
            close(ends[0]) != 0 || close(acknowledgement[0]) != 0) {
            _Exit(76);
        }
        _Exit(0);
    }
    if (close(ends[0]) != 0 || close(acknowledgement[0]) != 0 ||
        write(ends[1], "xy", 2U) != 2 ||
        write(acknowledgement[1], "a", 1U) != 1 ||
        close(ends[1]) != 0 || close(acknowledgement[1]) != 0 ||
        waitpid(consumer, &status, 0) != consumer || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) return 77;
    if (pipe(ends) != 0 || fdopen(ends[0], "w") != NULL || errno != EBADF)
        return 110;
    FILE *reader = fdopen(ends[0], "r");
    FILE *writer = fdopen(ends[1], "w");

    if (reader == NULL || writer == NULL ||
        fputs("stdio pipe\n", writer) != 0 || fflush(writer) != 0 ||
        fgets(bytes, sizeof(bytes), reader) == NULL ||
        strcmp(bytes, "stdio pipe\n") != 0 ||
        fseek(reader, 0L, SEEK_SET) != -1 || errno != ESPIPE ||
        fclose(writer) != 0 || fgetc(reader) != EOF || !feof(reader) ||
        fclose(reader) != 0 || fcntl(ends[0], F_GETFD) != -1 ||
        errno != EBADF || fcntl(ends[1], F_GETFD) != -1 ||
        errno != EBADF) return 111;
    if (pipe2(ends, O_NONBLOCK) != 0) return 112;
    reader = fdopen(ends[0], "r");
    writer = fdopen(ends[1], "w");
    if (reader == NULL || writer == NULL || fgetc(reader) != EOF ||
        errno != EAGAIN || !ferror(reader)) return 113;
    clearerr(reader);
    if (fputc('x', writer) != 'x' || fflush(writer) != 0 ||
        fgetc(reader) != 'x' || fclose(writer) != 0 ||
        fclose(reader) != 0) return 114;
    puts("OPENRFS PIPE fork blocking EOF EPIPE nonblock redirection fdopen PASS");
    return 0;
}

static int pipe_signal_probe(void)
{
    int status;

    if (signal(SIGPIPE, SIG_IGN) != SIG_DFL) return 98;
    const int child = fork();

    if (child < 0) return 99;
    if (child == 0) {
        int ends[2];

        if (signal(SIGPIPE, SIG_DFL) != SIG_IGN || pipe(ends) != 0 ||
            close(ends[0]) != 0) _Exit(100);
        (void)write(ends[1], "x", 1U);
        _Exit(101);
    }
    if (waitpid(child, &status, 0) != child || !WIFSIGNALED(status) ||
        WTERMSIG(status) != SIGPIPE ||
        signal(SIGPIPE, SIG_DFL) != SIG_IGN ||
        signal(SIGKILL, SIG_IGN) != SIG_ERR || errno != EINVAL ||
        signal(SIGCHLD, SIG_IGN) != SIG_DFL ||
        signal(SIGCHLD, SIG_DFL) != SIG_IGN ||
        signal(SIGINT, SIG_IGN) != SIG_DFL ||
        kill(getpid(), SIGINT) != 0 ||
        signal(SIGINT, SIG_DFL) != SIG_IGN) return 102;
    puts("OPENRFS SIGNAL SIGPIPE default ignore and fork PASS");
    return 0;
}

static int process_limits_probe(void)
{
    int children[3];
    int status;

    for (int index = 0; index < 3; ++index) {
        children[index] = fork();
        if (children[index] < 0) return 46;
        if (children[index] == 0) {
            volatile unsigned long progress = 0U;

            for (unsigned long step = 0U; step < 1000000U; ++step) {
                progress += step & 1U;
            }
            _Exit(progress == 500000U ? 30 + index : 49);
        }
    }
    if (fork() != -1 || errno != EAGAIN) return 47;
    for (int index = 0; index < 3; ++index) {
        if (waitpid(children[index], &status, 0) != children[index] ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 30 + index) {
            return 48;
        }
    }
    if (wait(NULL) != -1 || errno != ECHILD) return 50;
    puts("OPENRFS PROCESS slot exhaustion rollback and three children PASS");
    return 0;
}

static int process_fault_probe(void)
{
    int status;
    const int child = fork();

    if (child < 0) return 51;
    if (child == 0) {
        volatile unsigned char *guard =
            (volatile unsigned char *)(uintptr_t)UINT64_C(0x0000000600000000);
        *guard = 1U;
        _Exit(52);
    }
    if (waitpid(child, (int *)(uintptr_t)1U, 0) != -1 || errno != EFAULT ||
        waitpid(child, &status, 0) != child ||
        !WIFSIGNALED(status) || WTERMSIG(status) != 11) {
        return 53;
    }
    puts("OPENRFS PROCESS child fault status and wait pointer refusal PASS");
    return 0;
}

static int process_signal_probe(void)
{
    int status;
    int ends[2];

    if (kill(getpid(), 0) != 0 ||
        kill(getpid(), 65) != -1 || errno != EINVAL ||
        kill(getpid(), SIGCHLD) != -1 || errno != ENOSYS ||
        kill(-1, SIGTERM) != -1 || errno != ENOSYS ||
        pipe(ends) != 0) return 84;
    const int child = fork();

    if (child < 0) return 85;
    if (child == 0) {
        char byte;

        (void)close(ends[1]);
        (void)read(ends[0], &byte, 1U);
        _Exit(86);
    }
    if (close(ends[0]) != 0 || kill(child, 0) != 0 ||
        kill(child, SIGTERM) != 0 || close(ends[1]) != 0 ||
        waitpid(child, &status, 0) != child || !WIFSIGNALED(status) ||
        WTERMSIG(status) != SIGTERM ||
        kill(child, 0) != -1 || errno != ESRCH) return 87;
    const int killed = fork();

    if (killed < 0) return 88;
    if (killed == 0) {
        (void)kill(getpid(), SIGKILL);
        _Exit(89);
    }
    if (waitpid(killed, &status, 0) != killed ||
        !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) return 90;
    const int interrupted = fork();

    if (interrupted < 0) return 91;
    if (interrupted == 0) {
        (void)raise(SIGINT);
        _Exit(92);
    }
    if (waitpid(interrupted, &status, 0) != interrupted ||
        !WIFSIGNALED(status) || WTERMSIG(status) != SIGINT) return 93;
    puts("OPENRFS SIGNAL SIGINT SIGTERM SIGKILL default wait PASS");
    return 0;
}

static int process_sigchld_ignore_probe(void)
{
    int status;

    if (signal(SIGCHLD, SIG_IGN) != SIG_DFL) return 129;
    const int ignored = fork();

    if (ignored < 0) return 130;
    if (ignored == 0) _Exit(42);
    if (waitpid(ignored, &status, 0) != -1 || errno != ECHILD ||
        signal(SIGCHLD, SIG_DFL) != SIG_IGN) return 131;
    const int collected = fork();

    if (collected < 0) return 132;
    if (collected == 0) _Exit(43);
    if (waitpid(collected, &status, 0) != collected ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 43) return 133;
    puts("OPENRFS SIGNAL ignored SIGCHLD auto reap and wait ECHILD PASS");
    return 0;
}

static int process_group_probe(void)
{
    int ends[2];
    int status;
    int first;
    int second;
    int same_group;
    int reaped_first;
    int reaped_second;

    if (getpgrp() != getpid() || getpgid(0) != getpid() ||
        setpgid(0, 0) != -1 || errno != EPERM ||
        getpgid(-1) != -1 || errno != EINVAL ||
        pipe(ends) != 0) return 114;
    same_group = fork();
    if (same_group < 0) return 115;
    if (same_group == 0) _Exit(0);
    if (waitpid(0, &status, 0) != same_group ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0) return 115;
    first = fork();
    if (first < 0) return 115;
    if (first == 0) {
        char byte;

        (void)close(ends[1]);
        (void)read(ends[0], &byte, 1U);
        _Exit(116);
    }
    second = fork();
    if (second < 0) return 117;
    if (second == 0) {
        char byte;

        (void)close(ends[1]);
        (void)read(ends[0], &byte, 1U);
        _Exit(118);
    }
    if (close(ends[0]) != 0 ||
        setpgid(first, second) != -1 || errno != EPERM ||
        setpgid(first, first) != 0 ||
        setpgid(second, first) != 0 ||
        getpgid(first) != first || getpgid(second) != first ||
        waitpid(0, &status, WNOHANG) != -1 || errno != ECHILD ||
        kill(-first, 0) != 0 ||
        kill(-first, SIGTERM) != 0) return 119;
    if (close(ends[1]) != 0) return 120;
    reaped_first = waitpid(-first, &status, 0);
    if ((reaped_first != first && reaped_first != second) ||
        !WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM) return 121;
    reaped_second = waitpid(-first, &status, 0);
    if ((reaped_second != first && reaped_second != second) ||
        reaped_second == reaped_first || !WIFSIGNALED(status) ||
        WTERMSIG(status) != SIGTERM ||
        waitpid(-first, &status, WNOHANG) != -1 || errno != ECHILD ||
        kill(-first, 0) != -1 || errno != ESRCH) return 122;
    puts("OPENRFS PROCESS group signal and wait selectors PASS");
    return 0;
}

static int process_session_probe(void)
{
    int status;

    if (getsid(0) != getpid() || setsid() != -1 || errno != EPERM)
        return 134;
    const int child = fork();

    if (child < 0) return 135;
    if (child == 0) {
        if (getsid(0) != getppid() || setsid() != getpid() ||
            getsid(0) != getpid() || getpgrp() != getpid() ||
            setsid() != -1 || errno != EPERM) _Exit(136);
        _Exit(0);
    }
    if (waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        getsid(0) != getpid()) return 137;
    puts("OPENRFS PROCESS setsid session isolation and wait PASS");
    return 0;
}

static int process_umask_probe(void)
{
    int status;

    if (umask(0077U) != 0022U) return 94;
    const int child = fork();

    if (child < 0) return 95;
    if (child == 0) {
        _Exit(umask(0027U) == 0077U ? 0 : 96);
    }
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || umask(0022U) != 0077U) return 97;
    puts("OPENRFS PROCESS inherited umask PASS");
    return 0;
}

static int process_cwd_probe(void)
{
    char path[64];
    struct stat metadata;
    int status;
    int file;

    if (getcwd(path, 1U) != NULL || errno != ERANGE ||
        getcwd(path, sizeof(path)) != path || strcmp(path, "/") != 0 ||
        chdir("TMP") != 0 ||
        getcwd(path, sizeof(path)) != path ||
        strcmp(path, "/TMP") != 0) return 123;
    file = open("B.TXT", O_RDONLY);
    if (file < 0 || fstat(file, &metadata) != 0 ||
        metadata.st_size != 11U || close(file) != 0 ||
        stat("/TMP/B.TXT", &metadata) != 0 || metadata.st_size != 11U)
        return 124;
    if (chdir("B.TXT") != -1 || errno != ENOTDIR ||
        chdir("System:RESOURCE.TXT") != -1 || errno != ENOTSUP ||
        getcwd((char *)(uintptr_t)1U, sizeof(path)) != NULL ||
        errno != EFAULT ||
        getcwd(path, sizeof(path)) != path ||
        strcmp(path, "/TMP") != 0) return 124;
    const int child = fork();

    if (child < 0) return 125;
    if (child == 0) {
        if (getcwd(path, sizeof(path)) != path ||
            strcmp(path, "/TMP") != 0 || chdir("..") != 0 ||
            getcwd(path, sizeof(path)) != path || strcmp(path, "/") != 0)
            _Exit(126);
        _Exit(0);
    }
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 ||
        getcwd(path, sizeof(path)) != path ||
        strcmp(path, "/TMP") != 0 || chdir("..") != 0 ||
        getcwd(path, sizeof(path)) != path || strcmp(path, "/") != 0 ||
        chdir("../../..") != 0 ||
        getcwd(path, sizeof(path)) != path || strcmp(path, "/") != 0)
        return 127;
    puts("OPENRFS PROCESS Data cwd relative paths and fork inheritance PASS");
    return 0;
}

static void *sleeping_thread(void *unused)
{
    (void)unused;
    (void)usleep(100000U);
    return NULL;
}

static int multithread_fork_probe(void)
{
    pthread_t worker;
    int status;

    if (pthread_create(&worker, NULL, sleeping_thread, NULL) != 0) return 54;
    const int child = fork();

    if (child < 0) return 55;
    if (child == 0) {
        if (openrfs_syscall1(OPENRFS_SYS_THREAD_JOIN, worker) !=
                -OPENRFS_ESTALE) {
            _Exit(56);
        }
        _Exit(0);
    }
    if (waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        pthread_join(worker, NULL) != 0) {
        return 57;
    }
    puts("OPENRFS PROCESS fork retains only calling thread PASS");
    return 0;
}

static int exec_probe(void)
{
    int status;
    const int child = fork();

    if (child < 0) return 101;
    if (child == 0) {
        char kept_text[16];
        char closed_text[16];
        char pid_text[16];
        char *arguments[] = {"exec-child", kept_text, closed_text,
            pid_text, NULL};
        char *environment[] = {"OPENRFS_EXEC=validated", "", NULL};
        char *too_many[OPENRFS_EXEC_VECTOR_MAX + 2U];
        struct openrfs_exec_request bad_request = {
            sizeof(bad_request), OPENRFS_ABI_VERSION, 1U,
            (uint64_t)(uintptr_t)arguments,
            (uint64_t)(uintptr_t)environment, 0U,
            OPENRFS_EXEC_DESCRIPTOR_COUNT, 0U
        };
        const int kept = open("System:RESOURCE.TXT", O_RDONLY);
        const int closed = open("System:RESOURCE.TXT", O_RDONLY | O_CLOEXEC);

        if (chdir("/TMP") != 0 || setpgid(0, 0) != 0 ||
            getpgrp() != getpid() ||
            kept < 3 || closed < 3 ||
            snprintf(kept_text, sizeof(kept_text), "%d", kept) <= 0 ||
            snprintf(closed_text, sizeof(closed_text), "%d", closed) <= 0 ||
            snprintf(pid_text, sizeof(pid_text), "%d", getpid()) <= 0)
            _Exit(102);
        if (execve("MISSING.MAN", arguments, environment) != -1 ||
            errno != ENOENT || fcntl(kept, F_GETFD) != 0 ||
            fcntl(closed, F_GETFD) != FD_CLOEXEC)
            _Exit(103);
        for (size_t index = 0U; index <= OPENRFS_EXEC_VECTOR_MAX;
             ++index) too_many[index] = "x";
        too_many[OPENRFS_EXEC_VECTOR_MAX + 1U] = NULL;
        if (execve("NATIVET.MAN", too_many, environment) != -1 ||
            errno != E2BIG || fcntl(kept, F_GETFD) != 0)
            _Exit(104);
        if (openrfs_syscall1(OPENRFS_SYS_PROCESS_EXEC,
                (uint64_t)(uintptr_t)&bad_request) != -OPENRFS_EFAULT ||
            fcntl(kept, F_GETFD) != 0)
            _Exit(109);
        if (execve("NATIVET.MAN", arguments, environment) != -1)
            _Exit(105);
        printf("OPENRFS PROCESS exec error=%d\n", errno);
        _Exit(106);
    }
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        printf("OPENRFS PROCESS exec child status=%d\n", status);
        return 107;
    }
    puts("OPENRFS PROCESS fork exec same-pid argv env rollback cloexec PASS");
    puts("OPENRFS PROCESS cwd preserved across exec PASS");
    return 0;
}

int main(int argc, char **argv, char **environment)
{
    static const char expected_resource[] = "OpenRFS immutable resource\n";
    pthread_t first;
    pthread_t second;
    void *first_result = (void *)(uintptr_t)1U;
    void *second_result = (void *)(uintptr_t)1U;
    char *memory;
    FILE *file;
    char resource[sizeof(expected_resource)];
    long resource_handle;
    int probe;

    if (argc == 4 && argv != NULL && argv[0] != NULL &&
        strcmp(argv[0], "exec-child") == 0) {
        char first_byte = 0;
        char cwd[64];
        const int kept = atoi(argv[1]);
        const int closed = atoi(argv[2]);

        if (environment == NULL || environment[0] == NULL ||
            environment[1] == NULL || environment[2] != NULL ||
            strcmp(environment[0], "OPENRFS_EXEC=validated") != 0 ||
            strcmp(environment[1], "") != 0 ||
            getpid() != atoi(argv[3]) || getpgrp() != getpid() ||
            getcwd(cwd, sizeof(cwd)) != cwd ||
            strcmp(cwd, "/TMP") != 0 ||
            fcntl(kept, F_GETFD) != 0 ||
            read(kept, &first_byte, 1U) != 1 || first_byte != 'O' ||
            fcntl(closed, F_GETFD) != -1 || errno != EBADF ||
            close(kept) != 0) return 108;
        puts("OPENRFS PROCESS exec replacement image observed PASS");
        return 0;
    }

    if (openrfs_syscall0(OPENRFS_SYS_ABI_VERSION) != OPENRFS_ABI_VERSION ||
        argc != 3 || argv == NULL || environment == NULL ||
        strcmp(argv[0], "NATIVET.APP") != 0 ||
        strcmp(argv[1], "alpha") != 0 || strcmp(argv[2], "beta") != 0 ||
        argv[3] != NULL || strcmp(environment[0], "OPENRFS_ABI=1") != 0 ||
        strcmp(environment[1], "OPENRFS_APP_ID=NATIVET") != 0 ||
        strcmp(environment[2], "OPENRFS_DATA=NATIVET") != 0 ||
        environment[3] != NULL) {
        return 10;
    }
    puts("OPENRFS STARTUP argc argv environment auxiliary PASS");
    if (!initial_state_is_clean()) {
        return 11;
    }
    probe = syscall_performance_probe();
    if (probe != 0) return probe;
    memory = malloc(8192U);
    if (memory == NULL) return 12;
    (void)memset(memory, 0x5A, 8192U);
    if (memory[0] != 0x5A || memory[8191] != 0x5A) return 13;
    free(memory);
    probe = memory_and_pointer_probes();
    if (probe != 0) return probe;
    puts("OPENRFS MEMORY anonymous range exhaustion PASS");
    probe = file_and_handle_probes();
    if (probe != 0) return probe;
    puts("OPENRFS STORAGE handles directory persistence operations PASS");
    probe = timer_probe();
    if (probe != 0) return probe;
    puts("OPENRFS EVENT wait timeout cancellation PASS");
    resource_handle = openrfs_file_open(OPENRFS_VOLUME_SYSTEM, "RESOURCE.TXT",
        OPENRFS_OPEN_READ);
    if (resource_handle < 0 || openrfs_file_read((openrfs_handle_t)resource_handle,
            resource, sizeof(resource)) !=
            (long)(sizeof(expected_resource) - 1U) ||
        memcmp(resource, expected_resource, sizeof(expected_resource) - 1U) != 0 ||
        openrfs_file_read((openrfs_handle_t)resource_handle, resource, 1U) != 0 ||
        openrfs_handle_close((openrfs_handle_t)resource_handle) < 0) {
        return 37;
    }
    puts("OPENRFS RESOURCE immutable System read PASS");
    file = fopen("FOUND.TXT", "w+");
    if (file == NULL || fputs("native ABI v1\n", file) == EOF ||
        fflush(file) != 0 || fseek(file, 0L, SEEK_SET) != 0) return 38;
    {
        char line[32];
        if (fgets(line, sizeof(line), file) == NULL ||
            strcmp(line, "native ABI v1\n") != 0 || fclose(file) != 0) {
            return 39;
        }
    }
    puts("OPENRFS STDIO buffered update stream PASS");
    if (pthread_create(&first, NULL, thread_probe,
            (void *)(uintptr_t)101U) != 0) {
        return 40;
    }
    puts("OPENRFS THREAD first-created");
    if (pthread_create(&second, NULL, thread_probe,
            (void *)(uintptr_t)202U) != 0) {
        return 40;
    }
    puts("OPENRFS THREAD second-created");
    if (pthread_join(first, &first_result) != 0) {
        return 40;
    }
    puts("OPENRFS THREAD first-joined");
    if (pthread_join(second, &second_result) != 0 || first_result != NULL ||
        second_result != NULL || tls_value != 17U) {
        return 40;
    }
    puts("OPENRFS THREAD second-joined");
    probe = process_probe();
    if (probe != 0) return probe;
    probe = descriptor_probe();
    if (probe != 0) return probe;
    probe = pipe_probe();
    if (probe != 0) return probe;
    probe = pipe_signal_probe();
    if (probe != 0) return probe;
    probe = process_limits_probe();
    if (probe != 0) return probe;
    probe = process_fault_probe();
    if (probe != 0) return probe;
    probe = process_signal_probe();
    if (probe != 0) return probe;
    probe = process_sigchld_ignore_probe();
    if (probe != 0) return probe;
    probe = process_group_probe();
    if (probe != 0) return probe;
    probe = process_session_probe();
    if (probe != 0) return probe;
    probe = process_umask_probe();
    if (probe != 0) return probe;
    probe = multithread_fork_probe();
    if (probe != 0) return probe;
    printf("OPENRFS REFUSAL capability EACCES stale ESTALE pointer EFAULT "
        "traversal EINVAL exhaustion ENOMEM\n");
    printf("OPENRFS FILE create seek truncate rename replace sync unlink PASS\n");
    printf("OPENRFS STATE general FS x87 SSE PASS\n");
    printf("OPENRFS NATIVE PASS argc=%d app=%s\n", argc, argv[0]);
    return 0;
}
