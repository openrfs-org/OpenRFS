/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/audio.h>
#include <openrfs/event.h>
#include <openrfs/runtime.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OPERATION_NS UINT64_C(3000000000)

static int16_t first_pcm[OPENRFS_AUDIO_CHUNK_FRAMES * OPENRFS_AUDIO_CHANNELS];
static int16_t second_pcm[OPENRFS_AUDIO_CHUNK_FRAMES * OPENRFS_AUDIO_CHANNELS];
static int16_t canceled_pcm[OPENRFS_AUDIO_CHUNK_FRAMES * OPENRFS_AUDIO_CHANNELS];

static uint64_t deadline(void)
{
    return openrfs_monotonic_ns() + OPERATION_NS;
}

static void fill_pcm(void)
{
    for (size_t frame = 0U; frame < OPENRFS_AUDIO_CHUNK_FRAMES; ++frame) {
        const int16_t first = (frame / 32U) % 2U == 0U ?
            INT16_C(8192) : -INT16_C(8192);
        const int16_t second = (frame / 64U) % 2U == 0U ?
            INT16_C(4096) : -INT16_C(4096);
        const size_t sample = frame * OPENRFS_AUDIO_CHANNELS;

        first_pcm[sample] = first;
        first_pcm[sample + 1U] = first;
        second_pcm[sample] = second;
        second_pcm[sample + 1U] = second;
        canceled_pcm[sample] = INT16_C(30000);
        canceled_pcm[sample + 1U] = -INT16_C(30000);
    }
}

static int wait_writable(openrfs_handle_t first, openrfs_handle_t second)
{
    struct openrfs_wait_item items[2] = {
        {first, OPENRFS_WAIT_WRITABLE, 0U},
        {second, OPENRFS_WAIT_WRITABLE, 0U}
    };
    const long ready = openrfs_wait(items, 2U, deadline());

    return ready == 2 && items[0].ready == OPENRFS_WAIT_WRITABLE &&
        items[1].ready == OPENRFS_WAIT_WRITABLE ? 0 : -1;
}

static int run_refusal(void)
{
    if (openrfs_audio_open() != -OPENRFS_EACCES) {
        return 10;
    }
    puts("OPENRFS AUDIO REFUSAL PASS capability=EACCES");
    return 0;
}

static int run_proof(void)
{
    struct openrfs_wait_item canceled;
    long first_opened;
    long second_opened;
    long leaked;
    openrfs_handle_t first;
    openrfs_handle_t second;

    fill_pcm();
    first_opened = openrfs_audio_open();
    if (first_opened < 0) {
        return 20;
    }
    first = (openrfs_handle_t)first_opened;
    second_opened = openrfs_audio_open();
    if (second_opened < 0) {
        (void)openrfs_audio_close(first);
        return 21;
    }
    second = (openrfs_handle_t)second_opened;
    if (openrfs_audio_open() != -OPENRFS_EBUSY ||
        openrfs_audio_submit(first, first_pcm,
            OPENRFS_AUDIO_CHUNK_BYTES - OPENRFS_AUDIO_FRAME_BYTES) !=
                -OPENRFS_EINVAL ||
        wait_writable(first, second) != 0) {
        (void)openrfs_audio_close(second);
        (void)openrfs_audio_close(first);
        return 22;
    }
    puts("OPENRFS AUDIO PHASE open-limit-readiness PASS");

    if (openrfs_audio_set_volume(first, OPENRFS_AUDIO_VOLUME_UNITY,
            OPENRFS_AUDIO_VOLUME_UNITY / 2U) != 0 ||
        openrfs_audio_set_volume(second, OPENRFS_AUDIO_VOLUME_UNITY / 2U,
            OPENRFS_AUDIO_VOLUME_UNITY) != 0 ||
        openrfs_audio_submit(first, first_pcm, sizeof(first_pcm)) !=
            (long)sizeof(first_pcm) ||
        openrfs_audio_submit(second, second_pcm, sizeof(second_pcm)) !=
            (long)sizeof(second_pcm) ||
        openrfs_audio_drain(first, deadline()) != 0 ||
        openrfs_audio_drain(second, deadline()) != 0 ||
        wait_writable(first, second) != 0) {
        (void)openrfs_audio_close(second);
        (void)openrfs_audio_close(first);
        return 23;
    }
    puts("OPENRFS AUDIO PHASE two-stream-mix-drain PASS");

    if (openrfs_audio_submit(first, canceled_pcm, sizeof(canceled_pcm)) !=
            (long)sizeof(canceled_pcm) || openrfs_audio_cancel(first) != 0 ||
        openrfs_audio_drain(first, deadline()) != -OPENRFS_ECANCELED) {
        (void)openrfs_audio_close(second);
        (void)openrfs_audio_close(first);
        return 24;
    }
    canceled = (struct openrfs_wait_item){
        first, OPENRFS_WAIT_WRITABLE | OPENRFS_WAIT_CLOSED, 0U
    };
    if (openrfs_wait(&canceled, 1U, deadline()) != 1 ||
        canceled.ready != (OPENRFS_WAIT_WRITABLE | OPENRFS_WAIT_CLOSED)) {
        (void)openrfs_audio_close(second);
        (void)openrfs_audio_close(first);
        return 25;
    }
    puts("OPENRFS AUDIO PHASE cancel-terminal-readiness PASS");

    if (openrfs_audio_close(second) != 0 || openrfs_audio_close(first) != 0 ||
        openrfs_audio_close(first) != -OPENRFS_ESTALE) {
        return 26;
    }
    leaked = openrfs_audio_open();
    if (leaked < 0) {
        return 27;
    }
    puts("OPENRFS AUDIO PASS frames=1024 format=48000/S16LE/2 close=stale teardown=process");
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
