/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/audio.h>
#include <trait/runtime.h>

long trait_audio_open(void)
{
    return trait_syscall0(TRAIT_SYS_AUDIO_OPEN);
}

long trait_audio_submit(
    trait_handle_t output,
    const int16_t *samples,
    size_t byte_length
)
{
    const struct trait_audio_submit_request request = {
        sizeof(request), TRAIT_ABI_VERSION, output,
        (uint64_t)(uintptr_t)samples, (uint32_t)byte_length, 0U
    };

    if (samples == NULL || byte_length != TRAIT_AUDIO_CHUNK_BYTES) {
        return -TRAIT_EINVAL;
    }
    return trait_syscall1(TRAIT_SYS_AUDIO_SUBMIT,
        (uint64_t)(uintptr_t)&request);
}

long trait_audio_set_volume(
    trait_handle_t output,
    uint32_t left_q15,
    uint32_t right_q15
)
{
    const struct trait_audio_volume_request request = {
        sizeof(request), TRAIT_ABI_VERSION, output, left_q15, right_q15,
        0U, 0U
    };

    if (left_q15 > TRAIT_AUDIO_VOLUME_MAX ||
        right_q15 > TRAIT_AUDIO_VOLUME_MAX) {
        return -TRAIT_EINVAL;
    }
    return trait_syscall1(TRAIT_SYS_AUDIO_VOLUME,
        (uint64_t)(uintptr_t)&request);
}

long trait_audio_drain(trait_handle_t output, uint64_t deadline_ns)
{
    return trait_syscall2(TRAIT_SYS_AUDIO_DRAIN, output, deadline_ns);
}

long trait_audio_cancel(trait_handle_t output)
{
    return trait_syscall1(TRAIT_SYS_CANCEL, output);
}

long trait_audio_close(trait_handle_t output)
{
    return trait_handle_close(output);
}
