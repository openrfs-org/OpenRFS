/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/audio.h>
#include <rsd/runtime.h>

long rsd_audio_open(void)
{
    return rsd_syscall0(RSD_SYS_AUDIO_OPEN);
}

long rsd_audio_submit(
    rsd_handle_t output,
    const int16_t *samples,
    size_t byte_length
)
{
    const struct rsd_audio_submit_request request = {
        sizeof(request), RSD_ABI_VERSION, output,
        (uint64_t)(uintptr_t)samples, (uint32_t)byte_length, 0U
    };

    if (samples == NULL || byte_length != RSD_AUDIO_CHUNK_BYTES) {
        return -RSD_EINVAL;
    }
    return rsd_syscall1(RSD_SYS_AUDIO_SUBMIT,
        (uint64_t)(uintptr_t)&request);
}

long rsd_audio_set_volume(
    rsd_handle_t output,
    uint32_t left_q15,
    uint32_t right_q15
)
{
    const struct rsd_audio_volume_request request = {
        sizeof(request), RSD_ABI_VERSION, output, left_q15, right_q15,
        0U, 0U
    };

    if (left_q15 > RSD_AUDIO_VOLUME_MAX ||
        right_q15 > RSD_AUDIO_VOLUME_MAX) {
        return -RSD_EINVAL;
    }
    return rsd_syscall1(RSD_SYS_AUDIO_VOLUME,
        (uint64_t)(uintptr_t)&request);
}

long rsd_audio_drain(rsd_handle_t output, uint64_t deadline_ns)
{
    return rsd_syscall2(RSD_SYS_AUDIO_DRAIN, output, deadline_ns);
}

long rsd_audio_cancel(rsd_handle_t output)
{
    return rsd_syscall1(RSD_SYS_CANCEL, output);
}

long rsd_audio_close(rsd_handle_t output)
{
    return rsd_handle_close(output);
}
