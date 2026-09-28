/* SPDX-License-Identifier: GPL-3.0-only */
#include <pthread.h>
#include <rsd/event.h>
#include <rsd/runtime.h>
#include <rsd/window.h>
#include <stdint.h>

static void *blocked_thread(void *argument)
{
    (void)argument;
    for (;;) {
        (void)rsd_sleep_until(rsd_monotonic_ns() +
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
    struct rsd_memory_map_response mapping = {0U, 0U, 0U, 0U};
    struct rsd_window_create_response window = {0U};
    pthread_t thread;
    long file;
    long directory;
    long timer;

    if (rsd_path_mkdir(RSD_VOLUME_DATA, "LIVE") != 0) return 10;
    file = rsd_file_open(RSD_VOLUME_DATA, "LIVE/OPEN.TXT",
        RSD_OPEN_WRITE | RSD_OPEN_CREATE | RSD_OPEN_TRUNCATE);
    directory = rsd_directory_open(RSD_VOLUME_DATA, "LIVE");
    timer = rsd_timer_create();
    if (file < 0 || directory < 0 || timer < 0 ||
        rsd_file_write((rsd_handle_t)file, "live", 4U) != 4 ||
        rsd_timer_set((rsd_handle_t)timer,
            rsd_monotonic_ns() + UINT64_C(1000000000)) != 0 ||
        rsd_memory_allocate(2U * RSD_ABI_PAGE_SIZE,
            RSD_MEMORY_READ | RSD_MEMORY_WRITE, &mapping) != 0 ||
        rsd_window_create("Crash containment", 160U, 96U, &window) != 0 ||
        pthread_create(&thread, NULL, blocked_thread, NULL) != 0) {
        return 11;
    }
    poison_state_and_fault();
}
