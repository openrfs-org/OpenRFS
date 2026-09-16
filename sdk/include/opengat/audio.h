/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_USER_AUDIO_H
#define OPENGAT_USER_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include <opengat/abi.h>

long opengat_audio_open(void);
long opengat_audio_submit(opengat_handle_t output, const int16_t *samples,
    size_t byte_length);
long opengat_audio_set_volume(opengat_handle_t output, uint32_t left_q15,
    uint32_t right_q15);
long opengat_audio_drain(opengat_handle_t output, uint64_t deadline_ns);
long opengat_audio_cancel(opengat_handle_t output);
long opengat_audio_close(opengat_handle_t output);

#endif
