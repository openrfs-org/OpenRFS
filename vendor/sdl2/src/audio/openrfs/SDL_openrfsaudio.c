/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  OpenRFS audio backend addition for SDL 2.32.10. This file is distributed
  under the same zlib license as SDL.
*/

#include "../../SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_OPENRFS

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"

#include <openrfs/audio.h>
#include <openrfs/event.h>
#include <openrfs/runtime.h>

#define _THIS SDL_AudioDevice *_this

struct SDL_PrivateAudioData
{
    openrfs_handle_t output;
    Uint8 buffer[OPENRFS_AUDIO_CHUNK_BYTES];
    SDL_bool failed;
};

static uint64_t OPENRFSAUDIO_Deadline(void)
{
    const uint64_t now = openrfs_monotonic_ns();
    const uint64_t allowance = UINT64_C(1000000000);
    return now > UINT64_MAX - allowance ? UINT64_MAX : now + allowance;
}

static void OPENRFSAUDIO_WaitDevice(_THIS)
{
    struct openrfs_wait_item item;
    long result;

    if (_this->hidden->failed == SDL_TRUE) {
        return;
    }
    item.handle = _this->hidden->output;
    item.interests = OPENRFS_WAIT_WRITABLE | OPENRFS_WAIT_CLOSED;
    item.ready = 0U;
    result = openrfs_wait(&item, 1U, OPENRFSAUDIO_Deadline());
    if (result != 1 || item.ready != OPENRFS_WAIT_WRITABLE) {
        _this->hidden->failed = SDL_TRUE;
        SDL_OpenedAudioDeviceDisconnected(_this);
    }
}

static void OPENRFSAUDIO_PlayDevice(_THIS)
{
    long result;

    if (_this->hidden->failed == SDL_TRUE) {
        return;
    }
    result = openrfs_audio_submit(_this->hidden->output,
        (const int16_t *)(const void *)_this->hidden->buffer,
        sizeof(_this->hidden->buffer));
    if (result >= 0) {
        result = openrfs_audio_drain(_this->hidden->output,
            OPENRFSAUDIO_Deadline());
    }
    if (result < 0) {
        _this->hidden->failed = SDL_TRUE;
        SDL_OpenedAudioDeviceDisconnected(_this);
    }
}

static Uint8 *OPENRFSAUDIO_GetDeviceBuf(_THIS)
{
    return _this->hidden->buffer;
}

static void OPENRFSAUDIO_CloseDevice(_THIS)
{
    if (_this->hidden != NULL) {
        (void)openrfs_audio_cancel(_this->hidden->output);
        (void)openrfs_audio_close(_this->hidden->output);
        SDL_free(_this->hidden);
        _this->hidden = NULL;
    }
}

static int OPENRFSAUDIO_OpenDevice(_THIS, const char *devname)
{
    struct SDL_PrivateAudioData *hidden;
    long result;
    (void)devname;

    if (_this->iscapture) {
        return SDL_SetError("OpenRFS SDL audio capture is unsupported");
    }
    hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*hidden));
    if (hidden == NULL) {
        return SDL_OutOfMemory();
    }
    result = openrfs_audio_open();
    if (result < 0) {
        SDL_free(hidden);
        return SDL_SetError("OpenRFS audio open failed: %ld", result);
    }
    hidden->output = (openrfs_handle_t)result;
    _this->hidden = hidden;
    _this->spec.freq = (int)OPENRFS_AUDIO_SAMPLE_RATE;
    _this->spec.format = AUDIO_S16SYS;
    _this->spec.channels = (Uint8)OPENRFS_AUDIO_CHANNELS;
    _this->spec.samples = (Uint16)OPENRFS_AUDIO_CHUNK_FRAMES;
    SDL_CalculateAudioSpec(&_this->spec);
    if (_this->spec.size != OPENRFS_AUDIO_CHUNK_BYTES) {
        OPENRFSAUDIO_CloseDevice(_this);
        return SDL_SetError("OpenRFS SDL audio format calculation changed");
    }
    return 0;
}

static SDL_bool OPENRFSAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = OPENRFSAUDIO_OpenDevice;
    impl->WaitDevice = OPENRFSAUDIO_WaitDevice;
    impl->PlayDevice = OPENRFSAUDIO_PlayDevice;
    impl->GetDeviceBuf = OPENRFSAUDIO_GetDeviceBuf;
    impl->CloseDevice = OPENRFSAUDIO_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->HasCaptureSupport = SDL_FALSE;
    impl->SupportsNonPow2Samples = SDL_FALSE;
    return SDL_TRUE;
}

AudioBootStrap OPENRFSAUDIO_bootstrap = {
    "openrfs",
    "OpenRFS native mixed PCM output",
    OPENRFSAUDIO_Init,
    SDL_FALSE
};

#undef _THIS

#endif /* SDL_AUDIO_DRIVER_OPENRFS */
