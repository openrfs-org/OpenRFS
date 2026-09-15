/* SPDX-License-Identifier: GPL-3.0-only */
#include <pthread.h>
#include <opengat/event.h>
#include <opengat/runtime.h>
#include <opengat/window.h>
#include <stdint.h>

static void *blocked_thread(void *argument)
{
    (void)argument;
    for (;;) {
        (void)opengat_sleep_until(opengat_monotonic_ns() +
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
    struct opengat_memory_map_response mapping = {0U, 0U, 0U, 0U};
    struct opengat_window_create_response window = {0U};
    pthread_t thread;
    long file;
    long directory;
    long timer;

    if (opengat_path_mkdir(OPENGAT_VOLUME_DATA, "LIVE") != 0) return 10;
    file = opengat_file_open(OPENGAT_VOLUME_DATA, "LIVE/OPEN.TXT",
        OPENGAT_OPEN_WRITE | OPENGAT_OPEN_CREATE | OPENGAT_OPEN_TRUNCATE);
    directory = opengat_directory_open(OPENGAT_VOLUME_DATA, "LIVE");
    timer = opengat_timer_create();
    if (file < 0 || directory < 0 || timer < 0 ||
        opengat_file_write((opengat_handle_t)file, "live", 4U) != 4 ||
        opengat_timer_set((opengat_handle_t)timer,
            opengat_monotonic_ns() + UINT64_C(1000000000)) != 0 ||
        opengat_memory_allocate(2U * OPENGAT_ABI_PAGE_SIZE,
            OPENGAT_MEMORY_READ | OPENGAT_MEMORY_WRITE, &mapping) != 0 ||
        opengat_window_create("Crash containment", 160U, 96U, &window) != 0 ||
        pthread_create(&thread, NULL, blocked_thread, NULL) != 0) {
        return 11;
    }
    poison_state_and_fault();
}
