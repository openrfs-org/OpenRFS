/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_ABI_AUDIO_H
#define TRAIT_ABI_AUDIO_H

#include <trait/abi/base.h>

#define TRAIT_AUDIO_SAMPLE_RATE UINT32_C(48000)
#define TRAIT_AUDIO_CHANNELS UINT32_C(2)
#define TRAIT_AUDIO_BITS_PER_SAMPLE UINT32_C(16)
#define TRAIT_AUDIO_FRAME_BYTES UINT32_C(4)
#define TRAIT_AUDIO_CHUNK_FRAMES UINT32_C(1024)
#define TRAIT_AUDIO_CHUNK_BYTES \
    (TRAIT_AUDIO_CHUNK_FRAMES * TRAIT_AUDIO_FRAME_BYTES)
#define TRAIT_AUDIO_MAX_STREAMS UINT32_C(2)

/* Per-channel unsigned Q15 gain: zero is silent and 32768 is unity. */
#define TRAIT_AUDIO_VOLUME_SILENT UINT32_C(0)
#define TRAIT_AUDIO_VOLUME_UNITY UINT32_C(32768)
#define TRAIT_AUDIO_VOLUME_MAX TRAIT_AUDIO_VOLUME_UNITY

struct trait_audio_submit_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t handle;
    uint64_t buffer;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

struct trait_audio_volume_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t handle;
    uint32_t left_q15;
    uint32_t right_q15;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct trait_audio_submit_request) == 32U,
    "Trait OS audio-submit ABI changed");
_Static_assert(sizeof(struct trait_audio_volume_request) == 32U,
    "Trait OS audio-volume ABI changed");

#endif
