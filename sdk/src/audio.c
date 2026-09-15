/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/audio.h>
#include <opengat/runtime.h>

long opengat_audio_open(void)
{
    return opengat_syscall0(OPENGAT_SYS_AUDIO_OPEN);
}

long opengat_audio_submit(
    opengat_handle_t output,
    const int16_t *samples,
    size_t byte_length
)
{
    const struct opengat_audio_submit_request request = {
        sizeof(request), OPENGAT_ABI_VERSION, output,
        (uint64_t)(uintptr_t)samples, (uint32_t)byte_length, 0U
    };

    if (samples == NULL || byte_length != OPENGAT_AUDIO_CHUNK_BYTES) {
        return -OPENGAT_EINVAL;
    }
    return opengat_syscall1(OPENGAT_SYS_AUDIO_SUBMIT,
        (uint64_t)(uintptr_t)&request);
}

long opengat_audio_set_volume(
    opengat_handle_t output,
    uint32_t left_q15,
    uint32_t right_q15
)
{
    const struct opengat_audio_volume_request request = {
        sizeof(request), OPENGAT_ABI_VERSION, output, left_q15, right_q15,
        0U, 0U
    };

    if (left_q15 > OPENGAT_AUDIO_VOLUME_MAX ||
        right_q15 > OPENGAT_AUDIO_VOLUME_MAX) {
        return -OPENGAT_EINVAL;
    }
    return opengat_syscall1(OPENGAT_SYS_AUDIO_VOLUME,
        (uint64_t)(uintptr_t)&request);
}

long opengat_audio_drain(opengat_handle_t output, uint64_t deadline_ns)
{
    return opengat_syscall2(OPENGAT_SYS_AUDIO_DRAIN, output, deadline_ns);
}

long opengat_audio_cancel(opengat_handle_t output)
{
    return opengat_syscall1(OPENGAT_SYS_CANCEL, output);
}

long opengat_audio_close(opengat_handle_t output)
{
    return opengat_handle_close(output);
}
