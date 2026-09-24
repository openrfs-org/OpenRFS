/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/account.h>
#include <openrfs/account_kdf.h>
#include <openrfs/console.h>
#include <openrfs/cpu.h>
#include <openrfs/heap.h>
#include <openrfs/memory.h>
#include <openrfs/paging.h>
#include <openrfs/thread.h>

#include "../../vendor/monocypher/src/monocypher.h"

/* One 64 MiB, non-executable supervisor mapping, with unmapped guard pages.
 * No process space may be built while it is live, so preemption is held for
 * this fixed-cost KDF call while ordinary device interrupts remain enabled.
 */
#define ACCOUNT_KDF_ARENA_BASE UINT64_C(0x0000000402000000)
#define ACCOUNT_KDF_ARENA_BYTES \
    ((uint64_t)ACCOUNT_KDF_V2_MEMORY_KIB * UINT64_C(1024))
#define ACCOUNT_KDF_ARENA_PAGES \
    (ACCOUNT_KDF_ARENA_BYTES / OPENRFS_PAGE_SIZE)
#define ACCOUNT_KDF_RESERVE_FRAMES 4096U
#define ACCOUNT_KDF_MAX_PHYSICAL_ADDRESS UINT64_C(0x3FFFFFFF)

static struct frame_contiguous_allocation arena_frames;
static bool arena_active;

_Static_assert(ACCOUNT_KDF_ARENA_BYTES % OPENRFS_PAGE_SIZE == 0U,
    "Argon2 work arena must be page aligned");
_Static_assert(ACCOUNT_KDF_ARENA_BASE % OPENRFS_PAGE_SIZE == 0U,
    "Argon2 work arena must start on a page");
_Static_assert(ACCOUNT_KDF_ARENA_BASE > HEAP_GUARD_ABOVE,
    "Argon2 work arena must not overlap the heap");
_Static_assert(ACCOUNT_KDF_ARENA_PAGES <= SIZE_MAX,
    "Argon2 page count must fit the frame allocator");

bool account_kdf_v2_parameters_supported(uint32_t algorithm,
    uint8_t version, uint32_t memory_kib, uint32_t passes,
    uint32_t lanes)
{
    return algorithm == ACCOUNT_KDF_V2_ALGORITHM &&
        version == ACCOUNT_KDF_V2_VERSION &&
        memory_kib == ACCOUNT_KDF_V2_MEMORY_KIB &&
        passes == ACCOUNT_KDF_V2_PASSES &&
        lanes == ACCOUNT_KDF_V2_LANES;
}

enum account_kdf_status account_kdf_v2_derive(
    const uint8_t salt[ACCOUNT_KDF_V2_SALT_BYTES],
    const uint8_t *password, size_t password_bytes,
    uint8_t output[ACCOUNT_KDF_V2_OUTPUT_BYTES]
)
{
    struct paging_translation guard;
    const struct frame_contiguous_request request = {
        .page_count = (size_t)ACCOUNT_KDF_ARENA_PAGES,
        .alignment = OPENRFS_PAGE_SIZE,
        .maximum_physical_address = ACCOUNT_KDF_MAX_PHYSICAL_ADDRESS
    };
    const crypto_argon2_config config = {
        .algorithm = CRYPTO_ARGON2_ID,
        .nb_blocks = ACCOUNT_KDF_V2_MEMORY_KIB,
        .nb_passes = ACCOUNT_KDF_V2_PASSES,
        .nb_lanes = ACCOUNT_KDF_V2_LANES
    };
    bool restore_preemption;
    enum account_kdf_status result = ACCOUNT_KDF_STATUS_OK;

    if (output == NULL) {
        return ACCOUNT_KDF_STATUS_BAD_ARGUMENT;
    }
    crypto_wipe(output, ACCOUNT_KDF_V2_OUTPUT_BYTES);
    if (salt == NULL || password == NULL ||
        password_bytes < ACCOUNT_PASSWORD_MIN_BYTES ||
        password_bytes > ACCOUNT_PASSWORD_MAX_BYTES) {
        return ACCOUNT_KDF_STATUS_BAD_ARGUMENT;
    }
    if (!account_kdf_v2_parameters_supported(config.algorithm,
            ACCOUNT_KDF_V2_VERSION, config.nb_blocks, config.nb_passes,
            config.nb_lanes)) {
        return ACCOUNT_KDF_STATUS_UNSUPPORTED_PARAMETERS;
    }
    if (!cpu_interrupts_enabled()) {
        return ACCOUNT_KDF_STATUS_INTERRUPTS_DISABLED;
    }

    cpu_interrupt_disable();
    if (arena_active) {
        cpu_interrupt_enable();
        return ACCOUNT_KDF_STATUS_BUSY;
    }
    const struct paging_state paging = paging_get_state();
    if (!paging.active ||
        (cpu_read_cr3() & ~(PAGING_PAGE_SIZE - 1U)) !=
            paging.root_physical_address) {
        cpu_interrupt_enable();
        return ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE;
    }
    arena_active = true;
    restore_preemption = thread_preemption_enabled();
    if (restore_preemption &&
        thread_disable_preemption() != THREAD_STATUS_OK) {
        result = ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE;
        goto release_owner;
    }
    if (frame_allocator_get_stats().free_frames <
            ACCOUNT_KDF_ARENA_PAGES + ACCOUNT_KDF_RESERVE_FRAMES ||
        paging_translate(ACCOUNT_KDF_ARENA_BASE - OPENRFS_PAGE_SIZE,
            &guard) != PAGING_STATUS_NOT_MAPPED ||
        paging_translate(ACCOUNT_KDF_ARENA_BASE + ACCOUNT_KDF_ARENA_BYTES,
            &guard) != PAGING_STATUS_NOT_MAPPED ||
        frame_allocate_contiguous(&request, &arena_frames) !=
            FRAME_STATUS_OK) {
        result = ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE;
        goto restore_scheduler;
    }
    if (paging_map(ACCOUNT_KDF_ARENA_BASE, arena_frames.physical_base,
            ACCOUNT_KDF_ARENA_BYTES, PAGING_WRITE) != PAGING_STATUS_OK) {
        result = ACCOUNT_KDF_STATUS_MAPPING_FAILURE;
        /* paging_map promises rollback on refusal. Check that promise before
         * releasing these frames for reuse by a different owner. */
        for (uint64_t offset = 0U; offset < ACCOUNT_KDF_ARENA_BYTES;
                offset += OPENRFS_PAGE_SIZE) {
            if (paging_translate(ACCOUNT_KDF_ARENA_BASE + offset,
                    &guard) != PAGING_STATUS_NOT_MAPPED) {
                console_panic("account KDF arena map rollback failed");
            }
        }
        if (frame_release_contiguous(&arena_frames) != FRAME_STATUS_OK) {
            result = ACCOUNT_KDF_STATUS_CLEANUP_FAILURE;
            goto restore_scheduler;
        }
        goto restore_scheduler;
    }
    cpu_interrupt_enable();

    const crypto_argon2_inputs inputs = {
        .pass = password,
        .salt = salt,
        .pass_size = (uint32_t)password_bytes,
        .salt_size = ACCOUNT_KDF_V2_SALT_BYTES
    };
    crypto_argon2(output, ACCOUNT_KDF_V2_OUTPUT_BYTES,
        (void *)(uintptr_t)ACCOUNT_KDF_ARENA_BASE, config, inputs,
        crypto_argon2_no_extras);
    crypto_wipe((void *)(uintptr_t)ACCOUNT_KDF_ARENA_BASE,
        (size_t)ACCOUNT_KDF_ARENA_BYTES);

    cpu_interrupt_disable();
    if (paging_unmap(ACCOUNT_KDF_ARENA_BASE,
            ACCOUNT_KDF_ARENA_BYTES) != PAGING_STATUS_OK) {
        /* A reachable transient supervisor mapping could be copied into a
         * later process space. Do not resume scheduling after this failure. */
        crypto_wipe(output, ACCOUNT_KDF_V2_OUTPUT_BYTES);
        console_panic("account KDF arena unmap failed");
    }
    if (frame_release_contiguous(&arena_frames) != FRAME_STATUS_OK) {
        result = ACCOUNT_KDF_STATUS_CLEANUP_FAILURE;
    }
restore_scheduler:
    if (restore_preemption &&
        thread_enable_preemption() != THREAD_STATUS_OK) {
        result = ACCOUNT_KDF_STATUS_CLEANUP_FAILURE;
    }
release_owner:
    if (result != ACCOUNT_KDF_STATUS_CLEANUP_FAILURE) {
        arena_active = false;
    }
    cpu_interrupt_enable();
    if (result != ACCOUNT_KDF_STATUS_OK) {
        crypto_wipe(output, ACCOUNT_KDF_V2_OUTPUT_BYTES);
    }
    return result;
}
