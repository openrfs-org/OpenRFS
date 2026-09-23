/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openrfs/virtio_gpu_protocol.h>

static void store32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void store64(uint8_t *bytes, uint64_t value)
{
    store32(bytes, (uint32_t)value);
    store32(bytes + 4U, (uint32_t)(value >> 32U));
}

int main(void)
{
    uint8_t response[408] = { 0 };
    store32(response, 0x1101U);
    store32(response + 4U, 1U);
    store64(response + 8U, 42U);
    assert(virtio_gpu_response_valid(response, sizeof(response), 0U,
        sizeof(response), 0x1101U, sizeof(response), 42U));
    assert(!virtio_gpu_response_valid(response, sizeof(response), 1U,
        sizeof(response), 0x1101U, sizeof(response), 42U));
    assert(!virtio_gpu_response_valid(response, sizeof(response), 0U,
        24U, 0x1101U, sizeof(response), 42U));
    assert(!virtio_gpu_response_valid(response, 23U, 0U,
        24U, 0x1101U, 24U, 42U));
    assert(!virtio_gpu_response_valid(response, sizeof(response), 0U,
        sizeof(response), 0x1101U, sizeof(response), 43U));
    response[21] = 1U;
    assert(!virtio_gpu_response_valid(response, sizeof(response), 0U,
        sizeof(response), 0x1101U, sizeof(response), 42U));
    response[21] = 0U;
    store32(response, 0x1203U);
    assert(!virtio_gpu_response_valid(response, sizeof(response), 0U,
        sizeof(response), 0x1101U, sizeof(response), 42U));
    assert(virtio_gpu_rect_valid(0U, 0U, 1024U, 768U, 1024U, 768U));
    assert(!virtio_gpu_rect_valid(UINT32_MAX, 0U, 2U, 1U, 1024U, 768U));
    assert(!virtio_gpu_rect_valid(1023U, 0U, 2U, 1U, 1024U, 768U));
    assert(!virtio_gpu_rect_valid(0U, 0U, 0U, 1U, 1024U, 768U));
    assert(virtio_gpu_ring_progress_valid(0U, UINT16_MAX, 1U));
    assert(!virtio_gpu_ring_progress_valid(2U, 0U, 1U));
    assert(!virtio_gpu_ring_progress_valid(9U, 0U, 8U));
    puts("VirtIO GPU response and rectangle controls passed");
    return 0;
}
