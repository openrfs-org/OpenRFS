/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/audio.h>
#include <openrfs/runtime.h>

long openrfs_audio_open(void)
{
    return openrfs_syscall0(OPENRFS_SYS_AUDIO_OPEN);
}

long openrfs_audio_submit(
    openrfs_handle_t output,
    const int16_t *samples,
    size_t byte_length
)
{
    const struct openrfs_audio_submit_request request = {
        sizeof(request), OPENRFS_ABI_VERSION, output,
        (uint64_t)(uintptr_t)samples, (uint32_t)byte_length, 0U
    };

    if (samples == NULL || byte_length != OPENRFS_AUDIO_CHUNK_BYTES) {
        return -OPENRFS_EINVAL;
    }
    return openrfs_syscall1(OPENRFS_SYS_AUDIO_SUBMIT,
        (uint64_t)(uintptr_t)&request);
}

long openrfs_audio_set_volume(
    openrfs_handle_t output,
    uint32_t left_q15,
    uint32_t right_q15
)
{
    const struct openrfs_audio_volume_request request = {
        sizeof(request), OPENRFS_ABI_VERSION, output, left_q15, right_q15,
        0U, 0U
    };

    if (left_q15 > OPENRFS_AUDIO_VOLUME_MAX ||
        right_q15 > OPENRFS_AUDIO_VOLUME_MAX) {
        return -OPENRFS_EINVAL;
    }
    return openrfs_syscall1(OPENRFS_SYS_AUDIO_VOLUME,
        (uint64_t)(uintptr_t)&request);
}

long openrfs_audio_drain(openrfs_handle_t output, uint64_t deadline_ns)
{
    return openrfs_syscall2(OPENRFS_SYS_AUDIO_DRAIN, output, deadline_ns);
}

long openrfs_audio_cancel(openrfs_handle_t output)
{
    return openrfs_syscall1(OPENRFS_SYS_CANCEL, output);
}

long openrfs_audio_close(openrfs_handle_t output)
{
    return openrfs_handle_close(output);
}
