/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  Trait OS audio backend addition for SDL 2.32.10. This file is distributed
  under the same zlib license as SDL.
*/

#include "../../SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_TRAIT

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"

#include <trait/audio.h>
#include <trait/event.h>
#include <trait/runtime.h>

#define _THIS SDL_AudioDevice *_this

struct SDL_PrivateAudioData
{
    trait_handle_t output;
    Uint8 buffer[TRAIT_AUDIO_CHUNK_BYTES];
    SDL_bool failed;
};

static uint64_t TRAITAUDIO_Deadline(void)
{
    const uint64_t now = trait_monotonic_ns();
    const uint64_t allowance = UINT64_C(1000000000);
    return now > UINT64_MAX - allowance ? UINT64_MAX : now + allowance;
}

static void TRAITAUDIO_WaitDevice(_THIS)
{
    struct trait_wait_item item;
    long result;

    if (_this->hidden->failed == SDL_TRUE) {
        return;
    }
    item.handle = _this->hidden->output;
    item.interests = TRAIT_WAIT_WRITABLE | TRAIT_WAIT_CLOSED;
    item.ready = 0U;
    result = trait_wait(&item, 1U, TRAITAUDIO_Deadline());
    if (result != 1 || item.ready != TRAIT_WAIT_WRITABLE) {
        _this->hidden->failed = SDL_TRUE;
        SDL_OpenedAudioDeviceDisconnected(_this);
    }
}

static void TRAITAUDIO_PlayDevice(_THIS)
{
    long result;

    if (_this->hidden->failed == SDL_TRUE) {
        return;
    }
    result = trait_audio_submit(_this->hidden->output,
        (const int16_t *)(const void *)_this->hidden->buffer,
        sizeof(_this->hidden->buffer));
    if (result >= 0) {
        result = trait_audio_drain(_this->hidden->output,
            TRAITAUDIO_Deadline());
    }
    if (result < 0) {
        _this->hidden->failed = SDL_TRUE;
        SDL_OpenedAudioDeviceDisconnected(_this);
    }
}

static Uint8 *TRAITAUDIO_GetDeviceBuf(_THIS)
{
    return _this->hidden->buffer;
}

static void TRAITAUDIO_CloseDevice(_THIS)
{
    if (_this->hidden != NULL) {
        (void)trait_audio_cancel(_this->hidden->output);
        (void)trait_audio_close(_this->hidden->output);
        SDL_free(_this->hidden);
        _this->hidden = NULL;
    }
}

static int TRAITAUDIO_OpenDevice(_THIS, const char *devname)
{
    struct SDL_PrivateAudioData *hidden;
    long result;
    (void)devname;

    if (_this->iscapture) {
        return SDL_SetError("Trait OS SDL audio capture is unsupported");
    }
    hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*hidden));
    if (hidden == NULL) {
        return SDL_OutOfMemory();
    }
    result = trait_audio_open();
    if (result < 0) {
        SDL_free(hidden);
        return SDL_SetError("Trait OS audio open failed: %ld", result);
    }
    hidden->output = (trait_handle_t)result;
    _this->hidden = hidden;
    _this->spec.freq = (int)TRAIT_AUDIO_SAMPLE_RATE;
    _this->spec.format = AUDIO_S16SYS;
    _this->spec.channels = (Uint8)TRAIT_AUDIO_CHANNELS;
    _this->spec.samples = (Uint16)TRAIT_AUDIO_CHUNK_FRAMES;
    SDL_CalculateAudioSpec(&_this->spec);
    if (_this->spec.size != TRAIT_AUDIO_CHUNK_BYTES) {
        TRAITAUDIO_CloseDevice(_this);
        return SDL_SetError("Trait OS SDL audio format calculation changed");
    }
    return 0;
}

static SDL_bool TRAITAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = TRAITAUDIO_OpenDevice;
    impl->WaitDevice = TRAITAUDIO_WaitDevice;
    impl->PlayDevice = TRAITAUDIO_PlayDevice;
    impl->GetDeviceBuf = TRAITAUDIO_GetDeviceBuf;
    impl->CloseDevice = TRAITAUDIO_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->HasCaptureSupport = SDL_FALSE;
    impl->SupportsNonPow2Samples = SDL_FALSE;
    return SDL_TRUE;
}

AudioBootStrap TRAITAUDIO_bootstrap = {
    "trait",
    "Trait OS native mixed PCM output",
    TRAITAUDIO_Init,
    SDL_FALSE
};

#undef _THIS

#endif /* SDL_AUDIO_DRIVER_TRAIT */
