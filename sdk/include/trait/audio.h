/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_USER_AUDIO_H
#define TRAIT_USER_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include <trait/abi.h>

long trait_audio_open(void);
long trait_audio_submit(trait_handle_t output, const int16_t *samples,
    size_t byte_length);
long trait_audio_set_volume(trait_handle_t output, uint32_t left_q15,
    uint32_t right_q15);
long trait_audio_drain(trait_handle_t output, uint64_t deadline_ns);
long trait_audio_cancel(trait_handle_t output);
long trait_audio_close(trait_handle_t output);

#endif
