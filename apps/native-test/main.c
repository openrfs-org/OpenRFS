/* SPDX-License-Identifier: GPL-3.0-only */
#include <pthread.h>
#include <opengat/event.h>
#include <opengat/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        if (native_state_round_trip(opengat_monotonic_ns() + 1000000U,
                expected * 257U + iteration) != 0 || tls_value != expected) {
            return (void *)(uintptr_t)1U;
        }
    }
    return NULL;
}

static int syscall_performance_probe(void)
{
    const unsigned int iterations = 1024U;
    const uint64_t started = opengat_monotonic_ns();

    for (unsigned int iteration = 0U; iteration < iterations; ++iteration) {
        if (opengat_syscall0(OPENGAT_SYS_ABI_VERSION) != OPENGAT_ABI_VERSION) {
            return 14;
        }
    }
    const uint64_t elapsed = opengat_monotonic_ns() - started;

    printf("OPENGAT PERF syscall iterations=%u total_ns=%llu average_ns=%llu\n",
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
    file = opengat_file_open(OPENGAT_VOLUME_DATA, "TMP/PERF.BIN",
        OPENGAT_OPEN_WRITE | OPENGAT_OPEN_CREATE | OPENGAT_OPEN_TRUNCATE);
    if (file < 0) return 341;
    write_started = opengat_monotonic_ns();
    for (size_t offset = 0U; offset < FILE_BYTES; offset += sizeof(buffer)) {
        if (opengat_file_write((opengat_handle_t)file, buffer,
                sizeof(buffer)) != (long)sizeof(buffer)) {
            return 342;
        }
    }
    write_elapsed = opengat_monotonic_ns() - write_started;
    if (opengat_handle_close((opengat_handle_t)file) != 0) return 343;
    file = opengat_file_open(OPENGAT_VOLUME_DATA, "TMP/PERF.BIN",
        OPENGAT_OPEN_READ);
    if (file < 0) return 344;
    read_started = opengat_monotonic_ns();
    for (size_t offset = 0U; offset < FILE_BYTES; offset += sizeof(buffer)) {
        if (opengat_file_read((opengat_handle_t)file, buffer,
                sizeof(buffer)) != (long)sizeof(buffer) ||
            buffer[0] != UINT8_C(11) || buffer[sizeof(buffer) - 1U] !=
                (uint8_t)((sizeof(buffer) - 1U) * 37U + 11U)) {
            return 345;
        }
    }
    read_elapsed = opengat_monotonic_ns() - read_started;
    if (opengat_handle_close((opengat_handle_t)file) != 0 ||
        opengat_path_unlink(OPENGAT_VOLUME_DATA, "TMP/PERF.BIN") != 0) {
        return 346;
    }
    printf("OPENGAT PERF file sequential_bytes=%u write_ns=%llu read_ns=%llu\n",
        FILE_BYTES, (unsigned long long)write_elapsed,
        (unsigned long long)read_elapsed);
    return 0;
}

static int memory_and_pointer_probes(void)
{
    struct opengat_memory_map_response split = {0U, 0U, 0U, 0U};
    struct opengat_memory_map_response mappings[16];
    struct opengat_memory_map_response ignored = {0U, 0U, 0U, 0U};
    const struct opengat_memory_map_request bad_flags = {
        sizeof(bad_flags), OPENGAT_ABI_VERSION, OPENGAT_ABI_PAGE_SIZE, 0U,
        UINT32_C(0x80000000), 0U
    };
    size_t mapping_count = 0U;
    long exhausted = 0;

    if (opengat_syscall2(OPENGAT_SYS_MEMORY_MAP,
            (uint64_t)(uintptr_t)&bad_flags,
            (uint64_t)(uintptr_t)&ignored) != -OPENGAT_EINVAL ||
        opengat_random((void *)(uintptr_t)UINT64_C(0x12345000), 1U) !=
            -OPENGAT_EFAULT ||
        opengat_memory_allocate(2U * OPENGAT_ABI_PAGE_SIZE,
            OPENGAT_MEMORY_READ | OPENGAT_MEMORY_WRITE, &split) != 0) {
        return 20;
    }
    {
        volatile uint8_t *edge = (volatile uint8_t *)(uintptr_t)
            (split.address + OPENGAT_ABI_PAGE_SIZE - 1U);

        *edge = UINT8_C(0xA5);
        if (opengat_memory_release(split.address + OPENGAT_ABI_PAGE_SIZE,
                OPENGAT_ABI_PAGE_SIZE) != 0 ||
            opengat_random((void *)(uintptr_t)(split.address +
                OPENGAT_ABI_PAGE_SIZE - 1U), 2U) != -OPENGAT_EFAULT ||
            *edge != UINT8_C(0xA5) ||
            opengat_memory_release(split.address, OPENGAT_ABI_PAGE_SIZE) != 0) {
            return 21;
        }
    }
    while (mapping_count < sizeof(mappings) / sizeof(mappings[0])) {
        const long status = opengat_memory_allocate(2U * 1024U * 1024U,
            OPENGAT_MEMORY_READ | OPENGAT_MEMORY_WRITE,
            &mappings[mapping_count]);

        if (status < 0) {
            exhausted = status;
            break;
        }
        ++mapping_count;
    }
    if (exhausted != -OPENGAT_ENOMEM || mapping_count == 0U) {
        return 22;
    }
    while (mapping_count != 0U) {
        --mapping_count;
        if (opengat_memory_release(mappings[mapping_count].address,
                mappings[mapping_count].length) != 0) {
            return 23;
        }
    }
    return 0;
}

static int file_and_handle_probes(void)
{
    static const char replacement[] = "replacement";
    struct opengat_volume_space space = {0U, 0U, 0U, 0U, 0U, 0U};
    struct opengat_directory_entry entry;
    char bytes[sizeof(replacement)];
    long file;
    long duplicate;
    long directory;
    int found = 0;

    if (opengat_file_open(OPENGAT_VOLUME_DATA, "../ESCAPE.TXT",
            OPENGAT_OPEN_READ) != -OPENGAT_EINVAL ||
        opengat_file_open(OPENGAT_VOLUME_SYSTEM, "../NATIVET.APP",
            OPENGAT_OPEN_READ) != -OPENGAT_EINVAL ||
        opengat_syscall0(OPENGAT_SYS_STREAM_OPEN) != -OPENGAT_EACCES ||
        opengat_syscall0(UINT64_C(0xFFFF)) != -OPENGAT_ENOSYS) {
        return 24;
    }
    if (opengat_path_mkdir(OPENGAT_VOLUME_DATA, "TMP") != 0) {
        return 25;
    }
    {
        const int performance = file_performance_probe();

        if (performance != 0) return performance;
    }
    file = opengat_file_open(OPENGAT_VOLUME_DATA, "TMP/A.TXT",
        OPENGAT_OPEN_READ | OPENGAT_OPEN_WRITE | OPENGAT_OPEN_CREATE |
            OPENGAT_OPEN_TRUNCATE);
    if (file < 0 || opengat_timer_set((opengat_handle_t)file,
            opengat_monotonic_ns()) != -OPENGAT_EBADF) {
        return 26;
    }
    duplicate = opengat_handle_duplicate((opengat_handle_t)file);
    if (duplicate < 0 || opengat_handle_close((opengat_handle_t)file) != 0 ||
        opengat_file_read((opengat_handle_t)file, bytes, 1U) != -OPENGAT_ESTALE ||
        opengat_handle_close((opengat_handle_t)file) != -OPENGAT_ESTALE ||
        opengat_file_write((opengat_handle_t)duplicate, "abcdef", 6U) != 6 ||
        opengat_handle_close((opengat_handle_t)duplicate) != 0) {
        return 27;
    }
    puts("OPENGAT STORAGE typed duplicate stale-handle PASS");
    if (opengat_path_truncate(OPENGAT_VOLUME_DATA, "TMP/A.TXT", 3U) != 0 ||
        opengat_path_rename(OPENGAT_VOLUME_DATA, "TMP/A.TXT", "TMP/B.TXT") !=
            0) {
        return 28;
    }
    puts("OPENGAT STORAGE truncate rename PASS");
    file = opengat_file_open(OPENGAT_VOLUME_DATA, "TMP/C.TXT",
        OPENGAT_OPEN_WRITE | OPENGAT_OPEN_CREATE | OPENGAT_OPEN_TRUNCATE);
    if (file < 0 || opengat_file_write((opengat_handle_t)file, replacement,
            sizeof(replacement) - 1U) != (long)(sizeof(replacement) - 1U) ||
        opengat_handle_close((opengat_handle_t)file) != 0 ||
        opengat_path_replace(OPENGAT_VOLUME_DATA, "TMP/C.TXT", "TMP/B.TXT") !=
            0) {
        return 29;
    }
    puts("OPENGAT STORAGE replacement PASS");
    file = opengat_file_open(OPENGAT_VOLUME_DATA, "TMP/B.TXT", OPENGAT_OPEN_READ);
    if (file < 0 || opengat_file_read((opengat_handle_t)file, bytes,
            sizeof(bytes)) != (long)(sizeof(replacement) - 1U) ||
        memcmp(bytes, replacement, sizeof(replacement) - 1U) != 0 ||
        opengat_handle_close((opengat_handle_t)file) != 0) {
        return 30;
    }
    directory = opengat_directory_open(OPENGAT_VOLUME_DATA, "TMP");
    if (directory < 0) {
        return 31;
    }
    for (;;) {
        const long status = opengat_directory_read((opengat_handle_t)directory,
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
    if (opengat_handle_close((opengat_handle_t)directory) != 0) return 332;
    puts("OPENGAT STORAGE directory enumeration PASS");
    if (opengat_volume_space(OPENGAT_VOLUME_DATA, &space) != 0) return 333;
    if (space.total_bytes == 0U || space.free_bytes >= space.total_bytes) {
        return 334;
    }
    if (opengat_volume_sync(OPENGAT_VOLUME_DATA) != 0) return 335;
    if (opengat_path_unlink(OPENGAT_VOLUME_DATA, "TMP/B.TXT") != 0) return 336;
    if (opengat_path_unlink(OPENGAT_VOLUME_DATA, "TMP") != 0) return 337;
    if (opengat_volume_sync(OPENGAT_VOLUME_DATA) != 0) return 338;
    puts("OPENGAT STORAGE sync cleanup PASS");
    return 0;
}

static int timer_probe(void)
{
    struct opengat_wait_item item;
    const long timer = opengat_timer_create();
    uint64_t now;

    if (timer < 0) {
        return 34;
    }
    now = opengat_monotonic_ns();
    item = (struct opengat_wait_item){(opengat_handle_t)timer,
        OPENGAT_WAIT_SIGNALED, 0U};
    if (opengat_timer_set((opengat_handle_t)timer, now + UINT64_C(1000000)) != 0 ||
        opengat_wait(&item, 1U, now + UINT64_C(20000000)) != 1 ||
        item.ready != OPENGAT_WAIT_SIGNALED) {
        return 35;
    }
    now = opengat_monotonic_ns();
    item.ready = 0U;
    if (opengat_timer_set((opengat_handle_t)timer, now + UINT64_C(1000000000)) !=
            0 || opengat_wait(&item, 1U, now) != -OPENGAT_ETIMEDOUT ||
        opengat_cancel((opengat_handle_t)timer) != 0 ||
        opengat_handle_close((opengat_handle_t)timer) != 0) {
        return 36;
    }
    return 0;
}

int main(int argc, char **argv, char **environment)
{
    static const char expected_resource[] = "OpenGAT immutable resource\n";
    pthread_t first;
    pthread_t second;
    void *first_result = (void *)(uintptr_t)1U;
    void *second_result = (void *)(uintptr_t)1U;
    char *memory;
    FILE *file;
    char resource[sizeof(expected_resource)];
    long resource_handle;
    int probe;

    if (opengat_syscall0(OPENGAT_SYS_ABI_VERSION) != OPENGAT_ABI_VERSION ||
        argc != 3 || argv == NULL || environment == NULL ||
        strcmp(argv[0], "NATIVET.APP") != 0 ||
        strcmp(argv[1], "alpha") != 0 || strcmp(argv[2], "beta") != 0 ||
        argv[3] != NULL || strcmp(environment[0], "OPENGAT_ABI=1") != 0 ||
        strcmp(environment[1], "OPENGAT_APP_ID=NATIVET") != 0 ||
        strcmp(environment[2], "OPENGAT_DATA=NATIVET") != 0 ||
        environment[3] != NULL) {
        return 10;
    }
    puts("OPENGAT STARTUP argc argv environment auxiliary PASS");
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
    puts("OPENGAT MEMORY anonymous range exhaustion PASS");
    probe = file_and_handle_probes();
    if (probe != 0) return probe;
    puts("OPENGAT STORAGE handles directory persistence operations PASS");
    probe = timer_probe();
    if (probe != 0) return probe;
    puts("OPENGAT EVENT wait timeout cancellation PASS");
    resource_handle = opengat_file_open(OPENGAT_VOLUME_SYSTEM, "RESOURCE.TXT",
        OPENGAT_OPEN_READ);
    if (resource_handle < 0 || opengat_file_read((opengat_handle_t)resource_handle,
            resource, sizeof(resource)) !=
            (long)(sizeof(expected_resource) - 1U) ||
        memcmp(resource, expected_resource, sizeof(expected_resource) - 1U) != 0 ||
        opengat_file_read((opengat_handle_t)resource_handle, resource, 1U) != 0 ||
        opengat_handle_close((opengat_handle_t)resource_handle) < 0) {
        return 37;
    }
    puts("OPENGAT RESOURCE immutable System read PASS");
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
    puts("OPENGAT STDIO buffered update stream PASS");
    if (pthread_create(&first, NULL, thread_probe,
            (void *)(uintptr_t)101U) != 0) {
        return 40;
    }
    puts("OPENGAT THREAD first-created");
    if (pthread_create(&second, NULL, thread_probe,
            (void *)(uintptr_t)202U) != 0) {
        return 40;
    }
    puts("OPENGAT THREAD second-created");
    if (pthread_join(first, &first_result) != 0) {
        return 40;
    }
    puts("OPENGAT THREAD first-joined");
    if (pthread_join(second, &second_result) != 0 || first_result != NULL ||
        second_result != NULL || tls_value != 17U) {
        return 40;
    }
    puts("OPENGAT THREAD second-joined");
    printf("OPENGAT REFUSAL capability EACCES stale ESTALE pointer EFAULT "
        "traversal EINVAL exhaustion ENOMEM\n");
    printf("OPENGAT FILE create seek truncate rename replace sync unlink PASS\n");
    printf("OPENGAT STATE general FS x87 SSE PASS\n");
    printf("OPENGAT NATIVE PASS argc=%d app=%s\n", argc, argv[0]);
    return 0;
}
