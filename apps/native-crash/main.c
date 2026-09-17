/* SPDX-License-Identifier: GPL-3.0-only */
#include <pthread.h>
#include <openrfs/event.h>
#include <openrfs/runtime.h>
#include <openrfs/window.h>
#include <stdint.h>

static void *blocked_thread(void *argument)
{
    (void)argument;
    for (;;) {
        (void)openrfs_sleep_until(openrfs_monotonic_ns() +
            UINT64_C(1000000000));
    }
}

static _Noreturn void poison_state_and_fault(void)
{
    static const uint64_t pattern[2] = {
        UINT64_C(0xD15EA5E0D15EA5E0), UINT64_C(0x2EA15A1F2EA15A1F)
    };

    __asm__ volatile("movdqu %0, %%xmm15\n\tfldpi" : : "m" (pattern) :
        "xmm15", "memory");
    *(volatile uint64_t *)(uintptr_t)0U = UINT64_C(0xBADF00D);
    __builtin_unreachable();
}

int main(void)
{
    struct openrfs_memory_map_response mapping = {0U, 0U, 0U, 0U};
    struct openrfs_window_create_response window = {0U};
    pthread_t thread;
    long file;
    long directory;
    long timer;

    if (openrfs_path_mkdir(OPENRFS_VOLUME_DATA, "LIVE") != 0) return 10;
    file = openrfs_file_open(OPENRFS_VOLUME_DATA, "LIVE/OPEN.TXT",
        OPENRFS_OPEN_WRITE | OPENRFS_OPEN_CREATE | OPENRFS_OPEN_TRUNCATE);
    directory = openrfs_directory_open(OPENRFS_VOLUME_DATA, "LIVE");
    timer = openrfs_timer_create();
    if (file < 0 || directory < 0 || timer < 0 ||
        openrfs_file_write((openrfs_handle_t)file, "live", 4U) != 4 ||
        openrfs_timer_set((openrfs_handle_t)timer,
            openrfs_monotonic_ns() + UINT64_C(1000000000)) != 0 ||
        openrfs_memory_allocate(2U * OPENRFS_ABI_PAGE_SIZE,
            OPENRFS_MEMORY_READ | OPENRFS_MEMORY_WRITE, &mapping) != 0 ||
        openrfs_window_create("Crash containment", 160U, 96U, &window) != 0 ||
        pthread_create(&thread, NULL, blocked_thread, NULL) != 0) {
        return 11;
    }
    poison_state_and_fault();
}
