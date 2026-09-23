/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The iPXE DMA API (include/ipxe/dma.h) over an OpenRFS DMA arena.
 *
 * A dma_device names the arena its device was granted when bus mastering was
 * enabled. dma_alloc() carves from that arena; dma_map() refuses any buffer
 * outside it, so a driver can never point its device at memory the kernel
 * did not hand over. Mappings are identity: the bus address of a buffer is
 * its CPU address.
 */
#ifndef OPENRFS_IPXE_DMA_H
#define OPENRFS_IPXE_DMA_H

#include <stddef.h>
#include <stdint.h>
#include <ipxe/io.h>
#include <ipxe/malloc.h>

struct dma_arena;

struct dma_mapping {
    physaddr_t offset;
    struct dma_device *dma;
    void *token;
};

struct dma_device {
    struct dma_operations *op;
    physaddr_t mask;
    unsigned int mapped;
    unsigned int allocated;
    /* OpenRFS: the arena this device may address. */
    struct dma_arena *openrfs_arena;
};

struct dma_operations {
    int unused;
};

#define DMA_TX 0x01
#define DMA_RX 0x02
#define DMA_BI (DMA_TX | DMA_RX)

int dma_map(struct dma_device *dma, struct dma_mapping *map, void *address,
    size_t length, int flags);
void dma_unmap(struct dma_mapping *map, size_t length);
void *dma_alloc(struct dma_device *dma, struct dma_mapping *map,
    size_t length, size_t align);
void dma_free(struct dma_mapping *map, void *address, size_t length);
void *dma_umalloc(struct dma_device *dma, struct dma_mapping *map,
    size_t length, size_t align);
void dma_ufree(struct dma_mapping *map, void *address, size_t length);
void dma_set_mask(struct dma_device *dma, physaddr_t mask);

static inline __attribute__((always_inline)) physaddr_t dma(
    struct dma_mapping *map, void *address)
{
    return virt_to_phys(address) + (map != NULL ? map->offset : 0U);
}

static inline __attribute__((always_inline)) int dma_mapped(
    struct dma_mapping *map)
{
    return map->dma != NULL;
}

static inline __attribute__((always_inline)) void dma_init(
    struct dma_device *dma, struct dma_operations *op)
{
    dma->op = op;
}

static inline __attribute__((always_inline)) void dma_set_mask_64bit(
    struct dma_device *dma)
{
    dma_set_mask(dma, ~((physaddr_t)0));
}

#endif
