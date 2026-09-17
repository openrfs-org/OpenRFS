/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_USER_AUDIO_H
#define OPENRFS_USER_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include <openrfs/abi.h>

long openrfs_audio_open(void);
long openrfs_audio_submit(openrfs_handle_t output, const int16_t *samples,
    size_t byte_length);
long openrfs_audio_set_volume(openrfs_handle_t output, uint32_t left_q15,
    uint32_t right_q15);
long openrfs_audio_drain(openrfs_handle_t output, uint64_t deadline_ns);
long openrfs_audio_cancel(openrfs_handle_t output);
long openrfs_audio_close(openrfs_handle_t output);

#endif
