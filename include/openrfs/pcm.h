/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_PCM_H
#define OPENRFS_PCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * PCM playback devices driven by upstream drivers.
 *
 * OpenRFS's own HD Audio driver proves one fixed stream. An upstream sound
 * driver registers here instead and plays whatever format it accepts: the
 * caller opens it with a format, writes whole fragments of the size the
 * driver chose, and drains. Writes block until the driver has room, so a
 * caller that keeps writing keeps the device fed.
 */
#define PCM_MAX_DEVICES 4U
#define PCM_NAME_CAPACITY 8U
#define PCM_DRIVER_CAPACITY 24U
#define PCM_DESCRIPTION_CAPACITY 64U

enum pcm_status {
    PCM_STATUS_OK = 0,
    PCM_STATUS_NULL_ARGUMENT,
    PCM_STATUS_ABSENT,
    PCM_STATUS_TABLE_FULL,
    PCM_STATUS_UNSUPPORTED,
    PCM_STATUS_NOT_OPEN,
    PCM_STATUS_BUSY,
    PCM_STATUS_TIMEOUT,
    PCM_STATUS_DEVICE_ERROR,
    PCM_STATUS_COUNT
};

struct pcm_format {
    uint32_t rate;
    uint32_t channels;
    uint32_t bits;
    bool is_signed;
};

struct pcm_statistics {
    uint64_t fragments_written;
    uint64_t interrupts;          /* fragment completions serviced */
    uint64_t pauses;              /* times the device ran dry */
};

struct pcm_operations {
    /* Configure the stream; reports the fragment size writes must use. */
    enum pcm_status (*open)(void *context, const struct pcm_format *format,
        uint32_t *fragment_bytes);
    /* Queue one fragment, waiting for room at most timeout_ns. */
    enum pcm_status (*write)(void *context, const void *fragment,
        uint64_t timeout_ns);
    /* Wait until everything queued has played. */
    enum pcm_status (*drain)(void *context, uint64_t timeout_ns);
    enum pcm_status (*close)(void *context);
    void (*statistics)(void *context, struct pcm_statistics *statistics);
};

struct pcm_info {
    char name[PCM_NAME_CAPACITY];
    char driver[PCM_DRIVER_CAPACITY];
    char description[PCM_DESCRIPTION_CAPACITY];
    bool open;
    uint32_t fragment_bytes;
};

enum pcm_status pcm_register(
    const char *driver,
    const char *description,
    const struct pcm_operations *operations,
    void *context,
    char name[PCM_NAME_CAPACITY]
);
size_t pcm_count(void);
bool pcm_info(size_t index, struct pcm_info *info);
enum pcm_status pcm_find_driver(const char *driver, size_t *index);
enum pcm_status pcm_open(size_t index, const struct pcm_format *format,
    uint32_t *fragment_bytes);
enum pcm_status pcm_write(size_t index, const void *fragment,
    size_t length, uint64_t timeout_ns);
enum pcm_status pcm_drain(size_t index, uint64_t timeout_ns);
enum pcm_status pcm_close(size_t index);
bool pcm_statistics(size_t index, struct pcm_statistics *statistics);
const char *pcm_status_string(enum pcm_status status);

#endif
