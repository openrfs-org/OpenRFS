/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  RSD audio backend addition for SDL 2.32.10. This file is distributed
  under the same zlib license as SDL.
*/

#include "../../SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_RSD

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"

#include <rsd/audio.h>
#include <rsd/event.h>
#include <rsd/runtime.h>

#define _THIS SDL_AudioDevice *_this

struct SDL_PrivateAudioData
{
    rsd_handle_t output;
    Uint8 buffer[RSD_AUDIO_CHUNK_BYTES];
    SDL_bool failed;
};

static uint64_t RSDAUDIO_Deadline(void)
{
    const uint64_t now = rsd_monotonic_ns();
    const uint64_t allowance = UINT64_C(1000000000);
    return now > UINT64_MAX - allowance ? UINT64_MAX : now + allowance;
}

static void RSDAUDIO_WaitDevice(_THIS)
{
    struct rsd_wait_item item;
    long result;

    if (_this->hidden->failed == SDL_TRUE) {
        return;
    }
    item.handle = _this->hidden->output;
    item.interests = RSD_WAIT_WRITABLE | RSD_WAIT_CLOSED;
    item.ready = 0U;
    result = rsd_wait(&item, 1U, RSDAUDIO_Deadline());
    if (result != 1 || item.ready != RSD_WAIT_WRITABLE) {
        _this->hidden->failed = SDL_TRUE;
        SDL_OpenedAudioDeviceDisconnected(_this);
    }
}

static void RSDAUDIO_PlayDevice(_THIS)
{
    long result;

    if (_this->hidden->failed == SDL_TRUE) {
        return;
    }
    result = rsd_audio_submit(_this->hidden->output,
        (const int16_t *)(const void *)_this->hidden->buffer,
        sizeof(_this->hidden->buffer));
    if (result >= 0) {
        result = rsd_audio_drain(_this->hidden->output,
            RSDAUDIO_Deadline());
    }
    if (result < 0) {
        _this->hidden->failed = SDL_TRUE;
        SDL_OpenedAudioDeviceDisconnected(_this);
    }
}

static Uint8 *RSDAUDIO_GetDeviceBuf(_THIS)
{
    return _this->hidden->buffer;
}

static void RSDAUDIO_CloseDevice(_THIS)
{
    if (_this->hidden != NULL) {
        (void)rsd_audio_cancel(_this->hidden->output);
        (void)rsd_audio_close(_this->hidden->output);
        SDL_free(_this->hidden);
        _this->hidden = NULL;
    }
}

static int RSDAUDIO_OpenDevice(_THIS, const char *devname)
{
    struct SDL_PrivateAudioData *hidden;
    long result;
    (void)devname;

    if (_this->iscapture) {
        return SDL_SetError("RSD SDL audio capture is unsupported");
    }
    hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*hidden));
    if (hidden == NULL) {
        return SDL_OutOfMemory();
    }
    result = rsd_audio_open();
    if (result < 0) {
        SDL_free(hidden);
        return SDL_SetError("RSD audio open failed: %ld", result);
    }
    hidden->output = (rsd_handle_t)result;
    _this->hidden = hidden;
    _this->spec.freq = (int)RSD_AUDIO_SAMPLE_RATE;
    _this->spec.format = AUDIO_S16SYS;
    _this->spec.channels = (Uint8)RSD_AUDIO_CHANNELS;
    _this->spec.samples = (Uint16)RSD_AUDIO_CHUNK_FRAMES;
    SDL_CalculateAudioSpec(&_this->spec);
    if (_this->spec.size != RSD_AUDIO_CHUNK_BYTES) {
        RSDAUDIO_CloseDevice(_this);
        return SDL_SetError("RSD SDL audio format calculation changed");
    }
    return 0;
}

static SDL_bool RSDAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = RSDAUDIO_OpenDevice;
    impl->WaitDevice = RSDAUDIO_WaitDevice;
    impl->PlayDevice = RSDAUDIO_PlayDevice;
    impl->GetDeviceBuf = RSDAUDIO_GetDeviceBuf;
    impl->CloseDevice = RSDAUDIO_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->HasCaptureSupport = SDL_FALSE;
    impl->SupportsNonPow2Samples = SDL_FALSE;
    return SDL_TRUE;
}

AudioBootStrap RSDAUDIO_bootstrap = {
    "rsd",
    "RSD native mixed PCM output",
    RSDAUDIO_Init,
    SDL_FALSE
};

#undef _THIS

#endif /* SDL_AUDIO_DRIVER_RSD */
