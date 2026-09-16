/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  OpenGAT audio backend addition for SDL 2.32.10. This file is distributed
  under the same zlib license as SDL.
*/

#include "../../SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_OPENGAT

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"

#include <opengat/audio.h>
#include <opengat/event.h>
#include <opengat/runtime.h>

#define _THIS SDL_AudioDevice *_this

struct SDL_PrivateAudioData
{
    opengat_handle_t output;
    Uint8 buffer[OPENGAT_AUDIO_CHUNK_BYTES];
    SDL_bool failed;
};

static uint64_t OPENGATAUDIO_Deadline(void)
{
    const uint64_t now = opengat_monotonic_ns();
    const uint64_t allowance = UINT64_C(1000000000);
    return now > UINT64_MAX - allowance ? UINT64_MAX : now + allowance;
}

static void OPENGATAUDIO_WaitDevice(_THIS)
{
    struct opengat_wait_item item;
    long result;

    if (_this->hidden->failed == SDL_TRUE) {
        return;
    }
    item.handle = _this->hidden->output;
    item.interests = OPENGAT_WAIT_WRITABLE | OPENGAT_WAIT_CLOSED;
    item.ready = 0U;
    result = opengat_wait(&item, 1U, OPENGATAUDIO_Deadline());
    if (result != 1 || item.ready != OPENGAT_WAIT_WRITABLE) {
        _this->hidden->failed = SDL_TRUE;
        SDL_OpenedAudioDeviceDisconnected(_this);
    }
}

static void OPENGATAUDIO_PlayDevice(_THIS)
{
    long result;

    if (_this->hidden->failed == SDL_TRUE) {
        return;
    }
    result = opengat_audio_submit(_this->hidden->output,
        (const int16_t *)(const void *)_this->hidden->buffer,
        sizeof(_this->hidden->buffer));
    if (result >= 0) {
        result = opengat_audio_drain(_this->hidden->output,
            OPENGATAUDIO_Deadline());
    }
    if (result < 0) {
        _this->hidden->failed = SDL_TRUE;
        SDL_OpenedAudioDeviceDisconnected(_this);
    }
}

static Uint8 *OPENGATAUDIO_GetDeviceBuf(_THIS)
{
    return _this->hidden->buffer;
}

static void OPENGATAUDIO_CloseDevice(_THIS)
{
    if (_this->hidden != NULL) {
        (void)opengat_audio_cancel(_this->hidden->output);
        (void)opengat_audio_close(_this->hidden->output);
        SDL_free(_this->hidden);
        _this->hidden = NULL;
    }
}

static int OPENGATAUDIO_OpenDevice(_THIS, const char *devname)
{
    struct SDL_PrivateAudioData *hidden;
    long result;
    (void)devname;

    if (_this->iscapture) {
        return SDL_SetError("OpenGAT SDL audio capture is unsupported");
    }
    hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*hidden));
    if (hidden == NULL) {
        return SDL_OutOfMemory();
    }
    result = opengat_audio_open();
    if (result < 0) {
        SDL_free(hidden);
        return SDL_SetError("OpenGAT audio open failed: %ld", result);
    }
    hidden->output = (opengat_handle_t)result;
    _this->hidden = hidden;
    _this->spec.freq = (int)OPENGAT_AUDIO_SAMPLE_RATE;
    _this->spec.format = AUDIO_S16SYS;
    _this->spec.channels = (Uint8)OPENGAT_AUDIO_CHANNELS;
    _this->spec.samples = (Uint16)OPENGAT_AUDIO_CHUNK_FRAMES;
    SDL_CalculateAudioSpec(&_this->spec);
    if (_this->spec.size != OPENGAT_AUDIO_CHUNK_BYTES) {
        OPENGATAUDIO_CloseDevice(_this);
        return SDL_SetError("OpenGAT SDL audio format calculation changed");
    }
    return 0;
}

static SDL_bool OPENGATAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = OPENGATAUDIO_OpenDevice;
    impl->WaitDevice = OPENGATAUDIO_WaitDevice;
    impl->PlayDevice = OPENGATAUDIO_PlayDevice;
    impl->GetDeviceBuf = OPENGATAUDIO_GetDeviceBuf;
    impl->CloseDevice = OPENGATAUDIO_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->HasCaptureSupport = SDL_FALSE;
    impl->SupportsNonPow2Samples = SDL_FALSE;
    return SDL_TRUE;
}

AudioBootStrap OPENGATAUDIO_bootstrap = {
    "opengat",
    "OpenGAT native mixed PCM output",
    OPENGATAUDIO_Init,
    SDL_FALSE
};

#undef _THIS

#endif /* SDL_AUDIO_DRIVER_OPENGAT */
