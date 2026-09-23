/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_VIRTIO_GPU_PROTOCOL_H
#define OPENRFS_VIRTIO_GPU_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool virtio_gpu_rect_valid(uint32_t x, uint32_t y, uint32_t width,
    uint32_t height, uint32_t outer_width, uint32_t outer_height);
bool virtio_gpu_response_valid(const uint8_t *response, size_t capacity,
    uint32_t used_id, uint32_t used_length, uint32_t expected_type,
    uint32_t expected_length, uint64_t fence);
bool virtio_gpu_ring_progress_valid(uint16_t device_index,
    uint16_t consumed_index, uint16_t queue_size);

#endif
