/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DMA_ARENA_H
#define OPENRFS_DMA_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/dma.h>

/*
 * A sub-allocator over one contiguous DMA allocation.
 *
 * Upstream drivers allocate device-visible memory the way their home
 * environment lets them: iPXE calls malloc_phys() for every descriptor ring
 * and packet buffer, SeaBIOS calls memalign_high() for every queue. OpenRFS
 * instead hands a device exactly the DMA allocations it names when bus
 * mastering is enabled. An arena reconciles the two: it is one allocation,
 * created and transferred to device ownership before any device that uses it
 * may master the bus, and every upstream allocation is carved out of it.
 *
 * The block table is fixed and sorted by offset, so every operation is a
 * bounded walk and adjacent free blocks are always coalesced. Memory is
 * identity mapped, so the CPU address of a block is also its bus address.
 */
#define DMA_ARENA_MAX_BLOCKS 1024U
#define DMA_ARENA_MINIMUM_ALIGNMENT UINT64_C(16)

enum dma_arena_status {
    DMA_ARENA_STATUS_OK = 0,
    DMA_ARENA_STATUS_NULL_ARGUMENT,
    DMA_ARENA_STATUS_ALREADY_ACTIVE,
    DMA_ARENA_STATUS_NOT_ACTIVE,
    DMA_ARENA_STATUS_BAD_SIZE,
    DMA_ARENA_STATUS_BAD_ALIGNMENT,
    DMA_ARENA_STATUS_DMA_FAILURE,
    DMA_ARENA_STATUS_EXHAUSTED,
    DMA_ARENA_STATUS_OUT_OF_BLOCKS,
    DMA_ARENA_STATUS_BAD_POINTER,
    DMA_ARENA_STATUS_DOUBLE_FREE,
    DMA_ARENA_STATUS_BUSY,
    DMA_ARENA_STATUS_CORRUPT,
    DMA_ARENA_STATUS_COUNT
};

struct dma_arena_block {
    uint64_t offset;
    uint64_t size;
    bool used;
};

struct dma_arena_state {
    uint64_t physical_base;
    uint64_t size;
    uint64_t allocated_bytes;
    uint64_t peak_bytes;
    size_t live_allocations;
    size_t block_count;
    uint64_t failed_allocations;
    bool active;
};

struct dma_arena {
    struct dma_allocation allocation;
    uint8_t *base;
    uint64_t size;
    uint64_t allocated_bytes;
    uint64_t peak_bytes;
    uint64_t failed_allocations;
    size_t live_allocations;
    size_t block_count;
    struct dma_arena_block blocks[DMA_ARENA_MAX_BLOCKS];
    bool active;
};

/*
 * Allocate the arena's backing pages below maximum_physical_address (plus
 * one), zero them, and transfer the allocation to device ownership so a bus
 * master request may name it.
 */
enum dma_arena_status dma_arena_create(
    struct dma_arena *arena,
    size_t page_count,
    uint64_t maximum_physical_address
);
/* Release the backing allocation; refused while any block is live. */
enum dma_arena_status dma_arena_destroy(struct dma_arena *arena);
/*
 * Carve a zeroed block of size bytes whose address is a multiple of
 * alignment, a power of two. NULL on exhaustion; never a partial block.
 */
void *dma_arena_allocate(
    struct dma_arena *arena,
    uint64_t size,
    uint64_t alignment
);
enum dma_arena_status dma_arena_free(struct dma_arena *arena, void *pointer);
/* The rounded size of the live block starting at pointer, or zero. */
uint64_t dma_arena_allocation_size(
    const struct dma_arena *arena,
    const void *pointer
);
bool dma_arena_contains(
    const struct dma_arena *arena,
    const void *pointer,
    uint64_t length
);
uint64_t dma_arena_bus_address(const struct dma_arena *arena,
    const void *pointer);
struct dma_arena_state dma_arena_get_state(const struct dma_arena *arena);
enum dma_arena_status dma_arena_verify(const struct dma_arena *arena);
/* Pure controls over a synthetic arena; needs no DMA foundation. */
bool dma_arena_self_test(size_t *completed_tests);
const char *dma_arena_status_string(enum dma_arena_status status);

#endif
