/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_USER_AUDIO_H
#define RSD_USER_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include <rsd/abi.h>

long rsd_audio_open(void);
long rsd_audio_submit(rsd_handle_t output, const int16_t *samples,
    size_t byte_length);
long rsd_audio_set_volume(rsd_handle_t output, uint32_t left_q15,
    uint32_t right_q15);
long rsd_audio_drain(rsd_handle_t output, uint64_t deadline_ns);
long rsd_audio_cancel(rsd_handle_t output);
long rsd_audio_close(rsd_handle_t output);

#endif
