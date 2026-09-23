/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * First-fit sub-allocation over one contiguous DMA allocation. See
 * include/openrfs/dma_arena.h for why upstream drivers need it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/dma.h>
#include <openrfs/dma_arena.h>
#include <openrfs/memory.h>

static void zero_bytes(void *pointer, uint64_t length)
{
    uint8_t *bytes = pointer;

    for (uint64_t index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static bool power_of_two(uint64_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool round_up(uint64_t value, uint64_t alignment, uint64_t *result)
{
    const uint64_t mask = alignment - 1U;

    if (value > UINT64_MAX - mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

/* Reset the block table to one free block spanning [0, size). */
static void view_reset(struct dma_arena *arena)
{
    arena->blocks[0].offset = 0U;
    arena->blocks[0].size = arena->size;
    arena->blocks[0].used = false;
    arena->block_count = 1U;
    arena->allocated_bytes = 0U;
    arena->peak_bytes = 0U;
    arena->failed_allocations = 0U;
    arena->live_allocations = 0U;
}

static bool insert_block(struct dma_arena *arena, size_t index,
    uint64_t offset, uint64_t size, bool used)
{
    if (arena->block_count >= DMA_ARENA_MAX_BLOCKS ||
        index > arena->block_count) {
        return false;
    }
    for (size_t move = arena->block_count; move > index; --move) {
        arena->blocks[move] = arena->blocks[move - 1U];
    }
    arena->blocks[index].offset = offset;
    arena->blocks[index].size = size;
    arena->blocks[index].used = used;
    ++arena->block_count;
    return true;
}

static void remove_block(struct dma_arena *arena, size_t index)
{
    for (size_t move = index; move + 1U < arena->block_count; ++move) {
        arena->blocks[move] = arena->blocks[move + 1U];
    }
    --arena->block_count;
}

void *dma_arena_allocate(
    struct dma_arena *arena,
    uint64_t size,
    uint64_t alignment
)
{
    uint64_t rounded;

    if (arena == NULL || !arena->active || size == 0U ||
        size > arena->size) {
        return NULL;
    }
    if (alignment < DMA_ARENA_MINIMUM_ALIGNMENT) {
        alignment = DMA_ARENA_MINIMUM_ALIGNMENT;
    }
    if (!power_of_two(alignment) ||
        !round_up(size, DMA_ARENA_MINIMUM_ALIGNMENT, &rounded)) {
        ++arena->failed_allocations;
        return NULL;
    }

    for (size_t index = 0U; index < arena->block_count; ++index) {
        struct dma_arena_block *block = &arena->blocks[index];
        const uint64_t start = (uint64_t)(uintptr_t)arena->base +
            block->offset;
        uint64_t aligned;
        uint64_t pad;
        uint64_t tail;
        size_t needed;

        if (block->used || !round_up(start, alignment, &aligned)) {
            continue;
        }
        pad = aligned - start;
        if (pad > block->size || block->size - pad < rounded) {
            continue;
        }
        tail = block->size - pad - rounded;
        needed = (pad != 0U ? 1U : 0U) + (tail != 0U ? 1U : 0U);
        if (arena->block_count + needed > DMA_ARENA_MAX_BLOCKS) {
            ++arena->failed_allocations;
            return NULL;
        }

        const uint64_t used_offset = block->offset + pad;
        const uint64_t tail_offset = used_offset + rounded;

        /* Shrink the found block to the leading pad, or reuse it. */
        if (pad != 0U) {
            block->size = pad;
            if (!insert_block(arena, index + 1U, used_offset, rounded,
                    true)) {
                return NULL;
            }
            ++index;
        } else {
            block->size = rounded;
            block->used = true;
        }
        if (tail != 0U &&
            !insert_block(arena, index + 1U, tail_offset, tail, false)) {
            return NULL;
        }

        arena->allocated_bytes += rounded;
        if (arena->allocated_bytes > arena->peak_bytes) {
            arena->peak_bytes = arena->allocated_bytes;
        }
        ++arena->live_allocations;
        zero_bytes(arena->base + used_offset, rounded);
        return arena->base + used_offset;
    }
    ++arena->failed_allocations;
    return NULL;
}

enum dma_arena_status dma_arena_free(struct dma_arena *arena, void *pointer)
{
    uint64_t offset;

    if (arena == NULL || pointer == NULL) {
        return DMA_ARENA_STATUS_NULL_ARGUMENT;
    }
    if (!arena->active) {
        return DMA_ARENA_STATUS_NOT_ACTIVE;
    }
    if ((uint8_t *)pointer < arena->base ||
        (uint8_t *)pointer >= arena->base + arena->size) {
        return DMA_ARENA_STATUS_BAD_POINTER;
    }
    offset = (uint64_t)((uint8_t *)pointer - arena->base);

    for (size_t index = 0U; index < arena->block_count; ++index) {
        struct dma_arena_block *block = &arena->blocks[index];

        if (block->offset != offset) {
            if (block->offset > offset) {
                break;
            }
            continue;
        }
        if (!block->used) {
            return DMA_ARENA_STATUS_DOUBLE_FREE;
        }
        block->used = false;
        arena->allocated_bytes -= block->size;
        --arena->live_allocations;

        /* Coalesce with the following block, then the preceding one. */
        if (index + 1U < arena->block_count &&
            !arena->blocks[index + 1U].used) {
            block->size += arena->blocks[index + 1U].size;
            remove_block(arena, index + 1U);
        }
        if (index > 0U && !arena->blocks[index - 1U].used) {
            arena->blocks[index - 1U].size += block->size;
            remove_block(arena, index);
        }
        return DMA_ARENA_STATUS_OK;
    }
    return DMA_ARENA_STATUS_BAD_POINTER;
}

uint64_t dma_arena_allocation_size(
    const struct dma_arena *arena,
    const void *pointer
)
{
    uint64_t offset;

    if (arena == NULL || !arena->active || pointer == NULL ||
        (const uint8_t *)pointer < arena->base ||
        (const uint8_t *)pointer >= arena->base + arena->size) {
        return 0U;
    }
    offset = (uint64_t)((const uint8_t *)pointer - arena->base);
    for (size_t index = 0U; index < arena->block_count; ++index) {
        if (arena->blocks[index].offset == offset) {
            return arena->blocks[index].used ? arena->blocks[index].size : 0U;
        }
        if (arena->blocks[index].offset > offset) {
            break;
        }
    }
    return 0U;
}

bool dma_arena_contains(
    const struct dma_arena *arena,
    const void *pointer,
    uint64_t length
)
{
    const uint8_t *bytes = pointer;

    if (arena == NULL || !arena->active || pointer == NULL ||
        bytes < arena->base || bytes >= arena->base + arena->size) {
        return false;
    }
    return length <= (uint64_t)(arena->base + arena->size - bytes);
}

uint64_t dma_arena_bus_address(const struct dma_arena *arena,
    const void *pointer)
{
    (void)arena;
    /* The arena is identity mapped: the CPU address is the bus address. */
    return (uint64_t)(uintptr_t)pointer;
}

enum dma_arena_status dma_arena_verify(const struct dma_arena *arena)
{
    uint64_t expected_offset = 0U;
    uint64_t used_bytes = 0U;
    size_t used_blocks = 0U;

    if (arena == NULL) {
        return DMA_ARENA_STATUS_NULL_ARGUMENT;
    }
    if (!arena->active) {
        return DMA_ARENA_STATUS_NOT_ACTIVE;
    }
    if (arena->block_count == 0U ||
        arena->block_count > DMA_ARENA_MAX_BLOCKS) {
        return DMA_ARENA_STATUS_CORRUPT;
    }
    for (size_t index = 0U; index < arena->block_count; ++index) {
        const struct dma_arena_block *block = &arena->blocks[index];

        if (block->offset != expected_offset || block->size == 0U ||
            block->size > arena->size - block->offset) {
            return DMA_ARENA_STATUS_CORRUPT;
        }
        if (index > 0U && !block->used && !arena->blocks[index - 1U].used) {
            return DMA_ARENA_STATUS_CORRUPT;
        }
        if (block->used) {
            used_bytes += block->size;
            ++used_blocks;
        }
        expected_offset += block->size;
    }
    if (expected_offset != arena->size ||
        used_bytes != arena->allocated_bytes ||
        used_blocks != arena->live_allocations) {
        return DMA_ARENA_STATUS_CORRUPT;
    }
    return DMA_ARENA_STATUS_OK;
}

enum dma_arena_status dma_arena_create(
    struct dma_arena *arena,
    size_t page_count,
    uint64_t maximum_physical_address
)
{
    struct dma_request request;

    if (arena == NULL) {
        return DMA_ARENA_STATUS_NULL_ARGUMENT;
    }
    if (arena->active) {
        return DMA_ARENA_STATUS_ALREADY_ACTIVE;
    }
    if (page_count == 0U ||
        page_count > UINT64_MAX / OPENRFS_PAGE_SIZE) {
        return DMA_ARENA_STATUS_BAD_SIZE;
    }
    request.page_count = page_count;
    request.alignment = OPENRFS_PAGE_SIZE;
    request.maximum_physical_address = maximum_physical_address;
    if (dma_allocate(&request, &arena->allocation) != DMA_STATUS_OK) {
        return DMA_ARENA_STATUS_DMA_FAILURE;
    }
    arena->base = arena->allocation.cpu_address;
    arena->size = arena->allocation.byte_length;
    zero_bytes(arena->base, arena->size);
    if (dma_mark_initialized(&arena->allocation) != DMA_STATUS_OK ||
        dma_transfer_to_device(&arena->allocation) != DMA_STATUS_OK) {
        (void)dma_release(&arena->allocation);
        arena->base = NULL;
        arena->size = 0U;
        return DMA_ARENA_STATUS_DMA_FAILURE;
    }
    view_reset(arena);
    arena->active = true;
    return DMA_ARENA_STATUS_OK;
}

enum dma_arena_status dma_arena_destroy(struct dma_arena *arena)
{
    if (arena == NULL) {
        return DMA_ARENA_STATUS_NULL_ARGUMENT;
    }
    if (!arena->active) {
        return DMA_ARENA_STATUS_NOT_ACTIVE;
    }
    if (arena->live_allocations != 0U) {
        return DMA_ARENA_STATUS_BUSY;
    }
    if (dma_transfer_to_cpu(&arena->allocation) != DMA_STATUS_OK ||
        dma_release(&arena->allocation) != DMA_STATUS_OK) {
        return DMA_ARENA_STATUS_DMA_FAILURE;
    }
    arena->active = false;
    arena->base = NULL;
    arena->size = 0U;
    arena->block_count = 0U;
    return DMA_ARENA_STATUS_OK;
}

struct dma_arena_state dma_arena_get_state(const struct dma_arena *arena)
{
    struct dma_arena_state state;

    zero_bytes(&state, sizeof(state));
    if (arena == NULL || !arena->active) {
        return state;
    }
    state.physical_base = (uint64_t)(uintptr_t)arena->base;
    state.size = arena->size;
    state.allocated_bytes = arena->allocated_bytes;
    state.peak_bytes = arena->peak_bytes;
    state.live_allocations = arena->live_allocations;
    state.block_count = arena->block_count;
    state.failed_allocations = arena->failed_allocations;
    state.active = true;
    return state;
}

/*
 * The controls run over a static buffer posing as an arena, so they prove the
 * allocator without taking frames from the DMA foundation. Each one names a
 * way the block table could go wrong for a real driver.
 */
#define SELF_TEST_BYTES 8192U

static uint8_t self_test_buffer[SELF_TEST_BYTES]
    __attribute__((aligned(4096)));
static struct dma_arena self_test_arena;

bool dma_arena_self_test(size_t *completed_tests)
{
    struct dma_arena *arena = &self_test_arena;
    size_t completed = 0U;
    void *first;
    void *second;
    void *aligned;
    void *whole;

    if (completed_tests != NULL) {
        *completed_tests = 0U;
    }
    arena->base = self_test_buffer;
    arena->size = SELF_TEST_BYTES;
    view_reset(arena);
    arena->active = true;

    /* 1: small allocations are distinct, zeroed and inside the arena. */
    first = dma_arena_allocate(arena, 100U, 16U);
    second = dma_arena_allocate(arena, 100U, 16U);
    if (first == NULL || second == NULL || first == second ||
        !dma_arena_contains(arena, first, 100U) ||
        ((uint8_t *)first)[99] != 0U) {
        return false;
    }
    ++completed;

    /* 2: an alignment request is honoured even from an unaligned start. */
    aligned = dma_arena_allocate(arena, 64U, 1024U);
    if (aligned == NULL || ((uintptr_t)aligned & 1023U) != 0U ||
        dma_arena_verify(arena) != DMA_ARENA_STATUS_OK) {
        return false;
    }
    ++completed;

    /* 3: a non-power-of-two alignment and an oversize request refuse. */
    if (dma_arena_allocate(arena, 16U, 48U) != NULL ||
        dma_arena_allocate(arena, SELF_TEST_BYTES + 1U, 16U) != NULL) {
        return false;
    }
    ++completed;

    /* 4: double free and foreign pointers are named, not absorbed. */
    if (dma_arena_free(arena, first) != DMA_ARENA_STATUS_OK ||
        dma_arena_free(arena, first) != DMA_ARENA_STATUS_DOUBLE_FREE ||
        dma_arena_free(arena, self_test_buffer + SELF_TEST_BYTES) !=
            DMA_ARENA_STATUS_BAD_POINTER ||
        dma_arena_free(arena, (uint8_t *)second + 16U) !=
            DMA_ARENA_STATUS_BAD_POINTER) {
        return false;
    }
    ++completed;

    /* 5: freeing everything coalesces back into one block. */
    if (dma_arena_free(arena, second) != DMA_ARENA_STATUS_OK ||
        dma_arena_free(arena, aligned) != DMA_ARENA_STATUS_OK ||
        arena->block_count != 1U || arena->allocated_bytes != 0U ||
        dma_arena_verify(arena) != DMA_ARENA_STATUS_OK) {
        return false;
    }
    ++completed;

    /* 6: the whole arena can be taken at once, then nothing more. */
    whole = dma_arena_allocate(arena, SELF_TEST_BYTES, 4096U);
    if (whole != self_test_buffer ||
        dma_arena_allocate(arena, 16U, 16U) != NULL ||
        dma_arena_free(arena, whole) != DMA_ARENA_STATUS_OK ||
        arena->peak_bytes != SELF_TEST_BYTES) {
        return false;
    }
    ++completed;

    arena->active = false;
    arena->block_count = 0U;
    if (completed_tests != NULL) {
        *completed_tests = completed;
    }
    return true;
}

const char *dma_arena_status_string(enum dma_arena_status status)
{
    static const char *const messages[DMA_ARENA_STATUS_COUNT] = {
        "ok", "null DMA arena argument", "DMA arena is already active",
        "DMA arena is not active", "DMA arena size is invalid",
        "DMA arena alignment is not a power of two",
        "DMA arena backing allocation failed", "DMA arena is exhausted",
        "DMA arena block table is full",
        "pointer is not a DMA arena allocation",
        "DMA arena block was freed twice",
        "DMA arena still has live allocations",
        "DMA arena block table is inconsistent"
    };

    _Static_assert(sizeof(messages) / sizeof(messages[0]) ==
        DMA_ARENA_STATUS_COUNT, "DMA arena messages are out of sync");
    if (status < DMA_ARENA_STATUS_OK || status >= DMA_ARENA_STATUS_COUNT) {
        return "unknown DMA arena status";
    }
    return messages[status];
}
