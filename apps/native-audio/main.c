/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/audio.h>
#include <opengat/event.h>
#include <opengat/runtime.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OPERATION_NS UINT64_C(3000000000)

static int16_t first_pcm[OPENGAT_AUDIO_CHUNK_FRAMES * OPENGAT_AUDIO_CHANNELS];
static int16_t second_pcm[OPENGAT_AUDIO_CHUNK_FRAMES * OPENGAT_AUDIO_CHANNELS];
static int16_t canceled_pcm[OPENGAT_AUDIO_CHUNK_FRAMES * OPENGAT_AUDIO_CHANNELS];

static uint64_t deadline(void)
{
    return opengat_monotonic_ns() + OPERATION_NS;
}

static void fill_pcm(void)
{
    for (size_t frame = 0U; frame < OPENGAT_AUDIO_CHUNK_FRAMES; ++frame) {
        const int16_t first = (frame / 32U) % 2U == 0U ?
            INT16_C(8192) : -INT16_C(8192);
        const int16_t second = (frame / 64U) % 2U == 0U ?
            INT16_C(4096) : -INT16_C(4096);
        const size_t sample = frame * OPENGAT_AUDIO_CHANNELS;

        first_pcm[sample] = first;
        first_pcm[sample + 1U] = first;
        second_pcm[sample] = second;
        second_pcm[sample + 1U] = second;
        canceled_pcm[sample] = INT16_C(30000);
        canceled_pcm[sample + 1U] = -INT16_C(30000);
    }
}

static int wait_writable(opengat_handle_t first, opengat_handle_t second)
{
    struct opengat_wait_item items[2] = {
        {first, OPENGAT_WAIT_WRITABLE, 0U},
        {second, OPENGAT_WAIT_WRITABLE, 0U}
    };
    const long ready = opengat_wait(items, 2U, deadline());

    return ready == 2 && items[0].ready == OPENGAT_WAIT_WRITABLE &&
        items[1].ready == OPENGAT_WAIT_WRITABLE ? 0 : -1;
}

static int run_refusal(void)
{
    if (opengat_audio_open() != -OPENGAT_EACCES) {
        return 10;
    }
    puts("OPENGAT AUDIO REFUSAL PASS capability=EACCES");
    return 0;
}

static int run_proof(void)
{
    struct opengat_wait_item canceled;
    long first_opened;
    long second_opened;
    long leaked;
    opengat_handle_t first;
    opengat_handle_t second;

    fill_pcm();
    first_opened = opengat_audio_open();
    if (first_opened < 0) {
        return 20;
    }
    first = (opengat_handle_t)first_opened;
    second_opened = opengat_audio_open();
    if (second_opened < 0) {
        (void)opengat_audio_close(first);
        return 21;
    }
    second = (opengat_handle_t)second_opened;
    if (opengat_audio_open() != -OPENGAT_EBUSY ||
        opengat_audio_submit(first, first_pcm,
            OPENGAT_AUDIO_CHUNK_BYTES - OPENGAT_AUDIO_FRAME_BYTES) !=
                -OPENGAT_EINVAL ||
        wait_writable(first, second) != 0) {
        (void)opengat_audio_close(second);
        (void)opengat_audio_close(first);
        return 22;
    }
    puts("OPENGAT AUDIO PHASE open-limit-readiness PASS");

    if (opengat_audio_set_volume(first, OPENGAT_AUDIO_VOLUME_UNITY,
            OPENGAT_AUDIO_VOLUME_UNITY / 2U) != 0 ||
        opengat_audio_set_volume(second, OPENGAT_AUDIO_VOLUME_UNITY / 2U,
            OPENGAT_AUDIO_VOLUME_UNITY) != 0 ||
        opengat_audio_submit(first, first_pcm, sizeof(first_pcm)) !=
            (long)sizeof(first_pcm) ||
        opengat_audio_submit(second, second_pcm, sizeof(second_pcm)) !=
            (long)sizeof(second_pcm) ||
        opengat_audio_drain(first, deadline()) != 0 ||
        opengat_audio_drain(second, deadline()) != 0 ||
        wait_writable(first, second) != 0) {
        (void)opengat_audio_close(second);
        (void)opengat_audio_close(first);
        return 23;
    }
    puts("OPENGAT AUDIO PHASE two-stream-mix-drain PASS");

    if (opengat_audio_submit(first, canceled_pcm, sizeof(canceled_pcm)) !=
            (long)sizeof(canceled_pcm) || opengat_audio_cancel(first) != 0 ||
        opengat_audio_drain(first, deadline()) != -OPENGAT_ECANCELED) {
        (void)opengat_audio_close(second);
        (void)opengat_audio_close(first);
        return 24;
    }
    canceled = (struct opengat_wait_item){
        first, OPENGAT_WAIT_WRITABLE | OPENGAT_WAIT_CLOSED, 0U
    };
    if (opengat_wait(&canceled, 1U, deadline()) != 1 ||
        canceled.ready != (OPENGAT_WAIT_WRITABLE | OPENGAT_WAIT_CLOSED)) {
        (void)opengat_audio_close(second);
        (void)opengat_audio_close(first);
        return 25;
    }
    puts("OPENGAT AUDIO PHASE cancel-terminal-readiness PASS");

    if (opengat_audio_close(second) != 0 || opengat_audio_close(first) != 0 ||
        opengat_audio_close(first) != -OPENGAT_ESTALE) {
        return 26;
    }
    leaked = opengat_audio_open();
    if (leaked < 0) {
        return 27;
    }
    puts("OPENGAT AUDIO PASS frames=1024 format=48000/S16LE/2 close=stale teardown=process");
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
