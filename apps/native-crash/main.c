/* SPDX-License-Identifier: GPL-3.0-only */
#include <pthread.h>
#include <trait/event.h>
#include <trait/runtime.h>
#include <trait/window.h>
#include <stdint.h>

static void *blocked_thread(void *argument)
{
    (void)argument;
    for (;;) {
        (void)trait_sleep_until(trait_monotonic_ns() +
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
    struct trait_memory_map_response mapping = {0U, 0U, 0U, 0U};
    struct trait_window_create_response window = {0U};
    pthread_t thread;
    long file;
    long directory;
    long timer;

    if (trait_path_mkdir(TRAIT_VOLUME_DATA, "LIVE") != 0) return 10;
    file = trait_file_open(TRAIT_VOLUME_DATA, "LIVE/OPEN.TXT",
        TRAIT_OPEN_WRITE | TRAIT_OPEN_CREATE | TRAIT_OPEN_TRUNCATE);
    directory = trait_directory_open(TRAIT_VOLUME_DATA, "LIVE");
    timer = trait_timer_create();
    if (file < 0 || directory < 0 || timer < 0 ||
        trait_file_write((trait_handle_t)file, "live", 4U) != 4 ||
        trait_timer_set((trait_handle_t)timer,
            trait_monotonic_ns() + UINT64_C(1000000000)) != 0 ||
        trait_memory_allocate(2U * TRAIT_ABI_PAGE_SIZE,
            TRAIT_MEMORY_READ | TRAIT_MEMORY_WRITE, &mapping) != 0 ||
        trait_window_create("Crash containment", 160U, 96U, &window) != 0 ||
        pthread_create(&thread, NULL, blocked_thread, NULL) != 0) {
        return 11;
    }
    poison_state_and_fault();
}
