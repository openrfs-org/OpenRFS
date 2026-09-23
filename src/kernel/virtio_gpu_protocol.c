/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>
#include <stdint.h>

#include <openrfs/virtio_gpu_protocol.h>

static uint32_t read32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8U |
        (uint32_t)bytes[2] << 16U | (uint32_t)bytes[3] << 24U;
}

static uint64_t read64(const uint8_t *bytes)
{
    return (uint64_t)read32(bytes) | (uint64_t)read32(bytes + 4U) << 32U;
}

bool virtio_gpu_rect_valid(uint32_t x, uint32_t y, uint32_t width,
    uint32_t height, uint32_t outer_width, uint32_t outer_height)
{
    return width != 0U && height != 0U && x < outer_width &&
        y < outer_height && width <= outer_width - x &&
        height <= outer_height - y;
}

bool virtio_gpu_response_valid(const uint8_t *response, size_t capacity,
    uint32_t used_id, uint32_t used_length, uint32_t expected_type,
    uint32_t expected_length, uint64_t fence)
{
    if (response == NULL || expected_length < 24U ||
        expected_length > capacity || used_id != 0U ||
        used_length != expected_length || fence == 0U) {
        return false;
    }
    return read32(response) == expected_type &&
        read32(response + 4U) == 1U &&
        read64(response + 8U) == fence &&
        read32(response + 16U) == 0U && response[20U] == 0U &&
        response[21U] == 0U && response[22U] == 0U &&
        response[23U] == 0U;
}

bool virtio_gpu_ring_progress_valid(uint16_t device_index,
    uint16_t consumed_index, uint16_t queue_size)
{
    return queue_size != 0U &&
        (uint16_t)(device_index - consumed_index) <= queue_size;
}
