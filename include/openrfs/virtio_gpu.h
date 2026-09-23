/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_VIRTIO_GPU_H
#define OPENRFS_VIRTIO_GPU_H

#include <stdbool.h>
#include <stdint.h>
#include <openrfs/surface.h>

#define VIRTIO_GPU_OUTPUT_ABI_VERSION 1U

enum virtio_gpu_output_status {
    VIRTIO_GPU_OUTPUT_ABSENT = 0,
    VIRTIO_GPU_OUTPUT_ACTIVE,
    VIRTIO_GPU_OUTPUT_FAILED
};

struct virtio_gpu_output_state {
    uint32_t version;
    enum virtio_gpu_output_status status;
    uint32_t width;
    uint32_t height;
    uint64_t negotiated_features;
    uint64_t queued_commands;
    uint64_t completed_commands;
    uint64_t failed_commands;
    uint64_t frames_presented;
    uint64_t copied_bytes;
    uint64_t resource_pages;
    uint64_t cpu_raster_ns;
    uint64_t last_command_ns;
    uint64_t max_command_ns;
    uint64_t dropped_frames;
    uint32_t peak_queue_occupancy;
    uint32_t reset_reason;
    bool loader_framebuffer_selected;
    bool output_refused;
};

void virtio_gpu_output_start(uint32_t width, uint32_t height);
bool virtio_gpu_output_stop(void);
bool virtio_gpu_output_present(const struct surface *surface,
    struct surface_rect damage);
struct virtio_gpu_output_state virtio_gpu_output_get_state(void);
void virtio_gpu_output_note_raster(uint64_t elapsed_ns);

#endif
