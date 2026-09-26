/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include <openrfs/account_kdf.h>
#include <openrfs/cpu.h>
#include <openrfs/console.h>
#include <openrfs/memory.h>
#include <openrfs/paging.h>
#include <openrfs/thread.h>

#include "../vendor/monocypher/src/monocypher.h"

#define TEST_ARENA_BASE UINT64_C(0x0000000402000000)
#define TEST_ARENA_BYTES (64U * 1024U * 1024U)

static bool interrupts_enabled = true;
static bool preemption_enabled = true;
static bool frame_owned;
static bool mapped;
static bool deny_frame;
static bool deny_mapping;
static bool partial_mapping;
static bool deny_unmap;
static bool block_guard;
static bool wrong_address_space;
static size_t free_frames = 29412U;
static jmp_buf panic_jump;
static const char *panic_reason;

_Noreturn void console_panic(const char *message)
{
    assert(panic_reason != NULL && message != NULL &&
        strcmp(message, panic_reason) == 0);
    longjmp(panic_jump, 1);
}

bool cpu_interrupts_enabled(void) { return interrupts_enabled; }
uint64_t cpu_read_cr3(void)
{
    return wrong_address_space ? UINT64_C(0x2000) : UINT64_C(0x1000);
}
void cpu_interrupt_disable(void) { interrupts_enabled = false; }
void cpu_interrupt_enable(void) { interrupts_enabled = true; }
struct paging_state paging_get_state(void)
{
    return (struct paging_state){
        .root_physical_address = UINT64_C(0x1000), .active = true
    };
}
bool thread_preemption_enabled(void) { return preemption_enabled; }
enum thread_status thread_disable_preemption(void)
{
    assert(preemption_enabled && !interrupts_enabled);
    preemption_enabled = false;
    return THREAD_STATUS_OK;
}
enum thread_status thread_enable_preemption(void)
{
    assert(!preemption_enabled && !interrupts_enabled);
    preemption_enabled = true;
    return THREAD_STATUS_OK;
}
struct frame_allocator_stats frame_allocator_get_stats(void)
{
    return (struct frame_allocator_stats){ .free_frames = free_frames };
}
enum frame_status frame_allocate_contiguous(
    const struct frame_contiguous_request *request,
    struct frame_contiguous_allocation *allocation)
{
    assert(!interrupts_enabled && !frame_owned &&
        request->page_count == 16384U &&
        request->alignment == OPENRFS_PAGE_SIZE &&
        request->maximum_physical_address == UINT64_C(0x3FFFFFFF));
    if (deny_frame) return FRAME_STATUS_OUT_OF_MEMORY;
    *allocation = (struct frame_contiguous_allocation){
        .physical_base = UINT64_C(0x04000000),
        .page_count = request->page_count,
        .identifier = 1U,
        .active = true
    };
    frame_owned = true;
    return FRAME_STATUS_OK;
}
enum frame_status frame_release_contiguous(
    struct frame_contiguous_allocation *allocation)
{
    assert(!interrupts_enabled && frame_owned && allocation->active);
    frame_owned = false;
    allocation->active = false;
    return FRAME_STATUS_OK;
}
enum paging_status paging_translate(uint64_t address,
    struct paging_translation *translation)
{
    assert(!interrupts_enabled && translation != NULL &&
        (address == TEST_ARENA_BASE - OPENRFS_PAGE_SIZE ||
         address == TEST_ARENA_BASE + TEST_ARENA_BYTES ||
         (address >= TEST_ARENA_BASE &&
          address < TEST_ARENA_BASE + TEST_ARENA_BYTES)));
    if (address >= TEST_ARENA_BASE &&
            address < TEST_ARENA_BASE + TEST_ARENA_BYTES) {
        return mapped ? PAGING_STATUS_OK : PAGING_STATUS_NOT_MAPPED;
    }
    return block_guard ? PAGING_STATUS_OK : PAGING_STATUS_NOT_MAPPED;
}
enum paging_status paging_map(uint64_t address, uint64_t physical,
    uint64_t length, uint32_t permissions)
{
    void *result;

    assert(!interrupts_enabled && frame_owned && !mapped &&
        address == TEST_ARENA_BASE && physical == UINT64_C(0x04000000) &&
        length == TEST_ARENA_BYTES && permissions == PAGING_WRITE);
    if (deny_mapping) return PAGING_STATUS_OUT_OF_FRAMES;
    if (partial_mapping) {
        mapped = true;
        return PAGING_STATUS_OUT_OF_FRAMES;
    }
    #ifdef _WIN32
    result = VirtualAlloc((void *)(uintptr_t)address, (SIZE_T)length,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    #else
    result = mmap((void *)(uintptr_t)address, length,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    #endif
    assert(result == (void *)(uintptr_t)address);
    mapped = true;
    return PAGING_STATUS_OK;
}
enum paging_status paging_unmap(uint64_t address, uint64_t length)
{
    assert(!interrupts_enabled && frame_owned && mapped &&
        address == TEST_ARENA_BASE && length == TEST_ARENA_BYTES);
    if (deny_unmap) return PAGING_STATUS_VALIDATION_FAILURE;
    #ifdef _WIN32
    assert(VirtualFree((void *)(uintptr_t)address, 0U, MEM_RELEASE) != 0);
    #else
    assert(munmap((void *)(uintptr_t)address, length) == 0);
    #endif
    mapped = false;
    return PAGING_STATUS_OK;
}

static void published_vector(void)
{
    uint8_t password[32];
    uint8_t salt[16];
    uint8_t secret[8];
    uint8_t ad[12];
    uint8_t work[32U * 1024U];
    uint8_t output[32];
    static const uint8_t expected[32] = {
        0x0d, 0x64, 0x0d, 0xf5, 0x8d, 0x78, 0x76, 0x6c,
        0x08, 0xc0, 0x37, 0xa3, 0x4a, 0x8b, 0x53, 0xc9,
        0xd0, 0x1e, 0xf0, 0x45, 0x2d, 0x75, 0xb6, 0x5e,
        0xb5, 0x25, 0x20, 0xe9, 0x6b, 0x01, 0xe6, 0x59
    };
    const crypto_argon2_config config = { CRYPTO_ARGON2_ID, 32U, 3U, 4U };
    const crypto_argon2_inputs inputs = {
        password, salt, sizeof(password), sizeof(salt)
    };
    const crypto_argon2_extras extras = {
        secret, ad, sizeof(secret), sizeof(ad)
    };

    memset(password, 1, sizeof(password));
    memset(salt, 2, sizeof(salt));
    memset(secret, 3, sizeof(secret));
    memset(ad, 4, sizeof(ad));
    crypto_argon2(output, sizeof(output), work, config, inputs, extras);
    assert(memcmp(output, expected, sizeof(output)) == 0);
    crypto_wipe(work, sizeof(work));
}

int main(int argc, char **argv)
{
    static const uint8_t password[] = "correct horse battery staple";
    static const uint8_t salt[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    static const uint8_t expected[32] = {
        0x85, 0x3b, 0x27, 0x2a, 0x44, 0xdb, 0x14, 0x21,
        0xc0, 0x29, 0x62, 0x66, 0x9a, 0x55, 0xeb, 0x09,
        0x94, 0xf3, 0xca, 0xb3, 0x85, 0xed, 0x1c, 0x4c,
        0x79, 0x25, 0x3e, 0xee, 0x19, 0xba, 0xb4, 0x9e
    };
    uint8_t output[32];

    if (argc == 2 && strcmp(argv[1], "--partial-map") == 0) {
        partial_mapping = true;
        panic_reason = "account KDF arena map rollback failed";
        if (setjmp(panic_jump) == 0) {
            (void)account_kdf_v2_derive(salt, password,
                sizeof(password) - 1U, output);
            assert(false && "partial arena mapping returned to scheduler");
        }
        assert(frame_owned && mapped && !interrupts_enabled &&
            !preemption_enabled);
        puts("account v2 partial-map rollback fail-stop: PASS");
        return 0;
    }
    assert(argc == 1);

    published_vector();
    assert(account_kdf_v2_parameters_supported(2U, 0x13U,
        65536U, 3U, 4U));
    assert(!account_kdf_v2_parameters_supported(1U, 0x13U,
        65536U, 3U, 4U));
    assert(!account_kdf_v2_parameters_supported(2U, 0x10U,
        65536U, 3U, 4U));
    assert(!account_kdf_v2_parameters_supported(2U, 0x13U,
        65535U, 3U, 4U));
    assert(!account_kdf_v2_parameters_supported(2U, 0x13U,
        65536U, 4U, 4U));
    assert(!account_kdf_v2_parameters_supported(2U, 0x13U,
        65536U, 3U, 1U));

    assert(account_kdf_v2_derive(salt, password, sizeof(password) - 1U,
        output) == ACCOUNT_KDF_STATUS_OK);
    assert(memcmp(output, expected, sizeof(output)) == 0);
    assert(!mapped && !frame_owned && interrupts_enabled &&
        preemption_enabled);

    free_frames = 16384U + 4095U;
    memset(output, 0xa5, sizeof(output));
    assert(account_kdf_v2_derive(salt, password, sizeof(password) - 1U,
        output) == ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE);
    assert(output[0] == 0U && !frame_owned && !mapped);
    free_frames = 29412U;

    block_guard = true;
    assert(account_kdf_v2_derive(salt, password, sizeof(password) - 1U,
        output) == ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE);
    block_guard = false;
    deny_frame = true;
    assert(account_kdf_v2_derive(salt, password, sizeof(password) - 1U,
        output) == ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE);
    deny_frame = false;
    deny_mapping = true;
    assert(account_kdf_v2_derive(salt, password, sizeof(password) - 1U,
        output) == ACCOUNT_KDF_STATUS_MAPPING_FAILURE);
    deny_mapping = false;
    assert(!frame_owned && !mapped && preemption_enabled);

    interrupts_enabled = false;
    assert(account_kdf_v2_derive(salt, password, sizeof(password) - 1U,
        output) == ACCOUNT_KDF_STATUS_INTERRUPTS_DISABLED);
    interrupts_enabled = true;

    wrong_address_space = true;
    assert(account_kdf_v2_derive(salt, password, sizeof(password) - 1U,
        output) == ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE);
    wrong_address_space = false;

    deny_unmap = true;
    panic_reason = "account KDF arena unmap failed";
    memset(output, 0xa5, sizeof(output));
    if (setjmp(panic_jump) == 0) {
        (void)account_kdf_v2_derive(salt, password,
            sizeof(password) - 1U, output);
        assert(false && "failed arena unmap returned to scheduler");
    }
    panic_reason = NULL;
    assert(output[0] == 0U && frame_owned && mapped &&
        !interrupts_enabled && !preemption_enabled);
    interrupts_enabled = true;
    assert(account_kdf_v2_derive(salt, password, sizeof(password) - 1U,
        output) == ACCOUNT_KDF_STATUS_BUSY);
    puts("account v2 Argon2id RFC vector, independent profile and resource refusals: PASS");
    return 0;
}
