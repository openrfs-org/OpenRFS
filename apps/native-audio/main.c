/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/audio.h>
#include <rsd/event.h>
#include <rsd/runtime.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OPERATION_NS UINT64_C(3000000000)

static int16_t first_pcm[RSD_AUDIO_CHUNK_FRAMES * RSD_AUDIO_CHANNELS];
static int16_t second_pcm[RSD_AUDIO_CHUNK_FRAMES * RSD_AUDIO_CHANNELS];
static int16_t canceled_pcm[RSD_AUDIO_CHUNK_FRAMES * RSD_AUDIO_CHANNELS];

static uint64_t deadline(void)
{
    return rsd_monotonic_ns() + OPERATION_NS;
}

static void fill_pcm(void)
{
    for (size_t frame = 0U; frame < RSD_AUDIO_CHUNK_FRAMES; ++frame) {
        const int16_t first = (frame / 32U) % 2U == 0U ?
            INT16_C(8192) : -INT16_C(8192);
        const int16_t second = (frame / 64U) % 2U == 0U ?
            INT16_C(4096) : -INT16_C(4096);
        const size_t sample = frame * RSD_AUDIO_CHANNELS;

        first_pcm[sample] = first;
        first_pcm[sample + 1U] = first;
        second_pcm[sample] = second;
        second_pcm[sample + 1U] = second;
        canceled_pcm[sample] = INT16_C(30000);
        canceled_pcm[sample + 1U] = -INT16_C(30000);
    }
}

static int wait_writable(rsd_handle_t first, rsd_handle_t second)
{
    struct rsd_wait_item items[2] = {
        {first, RSD_WAIT_WRITABLE, 0U},
        {second, RSD_WAIT_WRITABLE, 0U}
    };
    const long ready = rsd_wait(items, 2U, deadline());

    return ready == 2 && items[0].ready == RSD_WAIT_WRITABLE &&
        items[1].ready == RSD_WAIT_WRITABLE ? 0 : -1;
}

static int run_refusal(void)
{
    if (rsd_audio_open() != -RSD_EACCES) {
        return 10;
    }
    puts("RSD AUDIO REFUSAL PASS capability=EACCES");
    return 0;
}

static int run_proof(void)
{
    struct rsd_wait_item canceled;
    long first_opened;
    long second_opened;
    long leaked;
    rsd_handle_t first;
    rsd_handle_t second;

    fill_pcm();
    first_opened = rsd_audio_open();
    if (first_opened < 0) {
        return 20;
    }
    first = (rsd_handle_t)first_opened;
    second_opened = rsd_audio_open();
    if (second_opened < 0) {
        (void)rsd_audio_close(first);
        return 21;
    }
    second = (rsd_handle_t)second_opened;
    if (rsd_audio_open() != -RSD_EBUSY ||
        rsd_audio_submit(first, first_pcm,
            RSD_AUDIO_CHUNK_BYTES - RSD_AUDIO_FRAME_BYTES) !=
                -RSD_EINVAL ||
        wait_writable(first, second) != 0) {
        (void)rsd_audio_close(second);
        (void)rsd_audio_close(first);
        return 22;
    }
    puts("RSD AUDIO PHASE open-limit-readiness PASS");

    if (rsd_audio_set_volume(first, RSD_AUDIO_VOLUME_UNITY,
            RSD_AUDIO_VOLUME_UNITY / 2U) != 0 ||
        rsd_audio_set_volume(second, RSD_AUDIO_VOLUME_UNITY / 2U,
            RSD_AUDIO_VOLUME_UNITY) != 0 ||
        rsd_audio_submit(first, first_pcm, sizeof(first_pcm)) !=
            (long)sizeof(first_pcm) ||
        rsd_audio_submit(second, second_pcm, sizeof(second_pcm)) !=
            (long)sizeof(second_pcm) ||
        rsd_audio_drain(first, deadline()) != 0 ||
        rsd_audio_drain(second, deadline()) != 0 ||
        wait_writable(first, second) != 0) {
        (void)rsd_audio_close(second);
        (void)rsd_audio_close(first);
        return 23;
    }
    puts("RSD AUDIO PHASE two-stream-mix-drain PASS");

    if (rsd_audio_submit(first, canceled_pcm, sizeof(canceled_pcm)) !=
            (long)sizeof(canceled_pcm) || rsd_audio_cancel(first) != 0 ||
        rsd_audio_drain(first, deadline()) != -RSD_ECANCELED) {
        (void)rsd_audio_close(second);
        (void)rsd_audio_close(first);
        return 24;
    }
    canceled = (struct rsd_wait_item){
        first, RSD_WAIT_WRITABLE | RSD_WAIT_CLOSED, 0U
    };
    if (rsd_wait(&canceled, 1U, deadline()) != 1 ||
        canceled.ready != (RSD_WAIT_WRITABLE | RSD_WAIT_CLOSED)) {
        (void)rsd_audio_close(second);
        (void)rsd_audio_close(first);
        return 25;
    }
    puts("RSD AUDIO PHASE cancel-terminal-readiness PASS");

    if (rsd_audio_close(second) != 0 || rsd_audio_close(first) != 0 ||
        rsd_audio_close(first) != -RSD_ESTALE) {
        return 26;
    }
    leaked = rsd_audio_open();
    if (leaked < 0) {
        return 27;
    }
    puts("RSD AUDIO PASS frames=1024 format=48000/S16LE/2 close=stale teardown=process");
    /* The last typed handle intentionally exercises process-exit cleanup. */
    return 0;
}

int main(int argc, char **argv, char **environment)
{
    (void)environment;
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 2 && strcmp(argv[1], "refusal") == 0) {
        return run_refusal();
    }
    if (argc == 2 && strcmp(argv[1], "proof") == 0) {
        return run_proof();
    }
    return 2;
}
