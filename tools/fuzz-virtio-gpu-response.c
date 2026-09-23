/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>
#include <stdint.h>

#include <openrfs/virtio_gpu_protocol.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint32_t id;
    uint32_t used;
    uint32_t expected;
    uint64_t fence;
    if (size < 20U) {
        return 0;
    }
    id = (uint32_t)data[0];
    used = (uint32_t)data[1] | (uint32_t)data[2] << 8U;
    expected = (uint32_t)data[3] | (uint32_t)data[4] << 8U;
    fence = (uint64_t)data[5] | (uint64_t)data[6] << 8U;
    (void)virtio_gpu_response_valid(data + 7U, size - 7U, id, used,
        expected, used, fence);
    (void)virtio_gpu_ring_progress_valid((uint16_t)used,
        (uint16_t)expected, (uint16_t)(fence & 31U));
    (void)virtio_gpu_rect_valid(id, used, expected, (uint32_t)fence,
        UINT32_MAX, (uint32_t)size);
    return 0;
}
