/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead_backend.h>
#include <openrfs/random.h>

#include "../../vendor/monocypher/src/monocypher.h"

struct held_segment {
    uint8_t revision_id[DATA_AEAD_ID_BYTES];
    char path[DATA_AEAD_PATH_MAX + 1U];
    openrfsfs_handle handle;
    bool used;
};

struct backend_context {
    const struct vfs_backend_ops *backend;
    enum openrfsfs_volume volume;
    const uint8_t *key;
    char slot_paths[3][DATA_AEAD_PATH_MAX + 1U];
    struct held_segment segments[DATA_AEAD_SEGMENTS_MAX];
};

static void zero_bytes(void *memory, size_t count)
{
    volatile uint8_t *to = memory;
    for (size_t at = 0U; at < count; ++at) to[at] = 0U;
}

static bool same_id(const uint8_t *left, const uint8_t *right)
{
    return crypto_verify16(left, right) == 0;
}

static bool required_ops(const struct vfs_backend_ops *backend)
{
    return backend != NULL && backend->sync != NULL &&
        backend->open != NULL && backend->close != NULL &&
        backend->pread != NULL && backend->write != NULL &&
        backend->stat_path != NULL && backend->mkdir != NULL &&
        backend->rename != NULL && backend->unlink != NULL &&
        (backend->open_options != NULL || backend->create != NULL);
}

static bool ensure_parent_directories(struct backend_context *context,
    const char *path)
{
    char parent[DATA_AEAD_PATH_MAX + 1U] = {0};
    size_t length = 0U;
    while (length <= DATA_AEAD_PATH_MAX && path[length] != '\0') ++length;
    if (length == 0U || length > DATA_AEAD_PATH_MAX) return false;
    for (size_t at = 0U; at < length; ++at) {
        parent[at] = path[at];
        if (path[at] != '/') continue;
        parent[at] = '\0';
        struct openrfsfs_stat stat;
        enum openrfsfs_status status = context->backend->lstat_path != NULL ?
            context->backend->lstat_path(context->volume, parent, &stat) :
            context->backend->stat_path(context->volume, parent, &stat);
        if (status == OPENRFSFS_STATUS_NOT_FOUND) {
            status = context->backend->mkdir(context->volume, parent);
            if (status != OPENRFSFS_STATUS_OK ||
                    context->backend->sync(context->volume) !=
                        OPENRFSFS_STATUS_OK) return false;
            status = context->backend->lstat_path != NULL ?
                context->backend->lstat_path(context->volume, parent, &stat) :
                context->backend->stat_path(context->volume, parent, &stat);
        }
        if (status != OPENRFSFS_STATUS_OK || !stat.directory)
            return false;
        parent[at] = '/';
    }
    return true;
}

static bool read_record_file(struct backend_context *context,
    const char *path, uint8_t record[DATA_AEAD_MANIFEST_BYTES],
    bool *present)
{
    *present = false;
    zero_bytes(record, DATA_AEAD_MANIFEST_BYTES);
    struct openrfsfs_stat stat;
    const enum openrfsfs_status found = context->backend->lstat_path != NULL ?
        context->backend->lstat_path(context->volume, path, &stat) :
        context->backend->stat_path(context->volume, path, &stat);
    if (found == OPENRFSFS_STATUS_NOT_FOUND) return true;
    if (found != OPENRFSFS_STATUS_OK || stat.directory) return false;
    *present = true;
    if (stat.size != DATA_AEAD_MANIFEST_BYTES) return true;
    openrfsfs_handle handle = 0U;
    if (context->backend->open(context->volume, path,
            OPENRFSFS_ACCESS_READ, &handle) != OPENRFSFS_STATUS_OK)
        return false;
    size_t got = 0U;
    const enum openrfsfs_status read_status = context->backend->pread(
        handle, record, DATA_AEAD_MANIFEST_BYTES, 0U, &got);
    const enum openrfsfs_status close_status = context->backend->close(handle);
    if (read_status != OPENRFSFS_STATUS_OK ||
            got != DATA_AEAD_MANIFEST_BYTES ||
            close_status != OPENRFSFS_STATUS_OK) {
        zero_bytes(record, DATA_AEAD_MANIFEST_BYTES);
        return false;
    }
    return true;
}

static bool slot_read(void *opaque, unsigned slot,
    uint8_t record[DATA_AEAD_MANIFEST_BYTES], bool *present)
{
    struct backend_context *context = opaque;
    return slot < 2U && read_record_file(context,
        context->slot_paths[slot], record, present);
}

static bool slot_remove(void *opaque, unsigned slot)
{
    struct backend_context *context = opaque;
    if (slot > 1U) return false;
    const enum openrfsfs_status status = context->backend->unlink(
        context->volume, context->slot_paths[slot]);
    return status == OPENRFSFS_STATUS_OK ||
        status == OPENRFSFS_STATUS_NOT_FOUND;
}

static bool slot_sync(void *opaque, unsigned slot)
{
    struct backend_context *context = opaque;
    return slot < 2U && context->backend->sync(context->volume) ==
        OPENRFSFS_STATUS_OK;
}

static bool temp_write(void *opaque,
    const uint8_t record[DATA_AEAD_MANIFEST_BYTES])
{
    struct backend_context *context = opaque;
    const char *path = context->slot_paths[2];
    const enum openrfsfs_status removed = context->backend->unlink(
        context->volume, path);
    if (removed != OPENRFSFS_STATUS_OK &&
            removed != OPENRFSFS_STATUS_NOT_FOUND) return false;
    if (removed == OPENRFSFS_STATUS_OK &&
            context->backend->sync(context->volume) !=
                OPENRFSFS_STATUS_OK) return false;
    openrfsfs_handle handle = 0U;
    enum openrfsfs_status status;
    if (context->backend->open_options != NULL) {
        struct openrfsfs_stat stat;
        status = context->backend->open_options(context->volume, path,
            OPENRFSFS_ACCESS_WRITE,
            OPENRFSFS_OPEN_CREATE | OPENRFSFS_OPEN_EXCLUSIVE,
            0600U, &handle, &stat);
    } else {
        status = context->backend->create(context->volume, path, 0600U);
        if (status == OPENRFSFS_STATUS_OK)
            status = context->backend->open(context->volume, path,
                OPENRFSFS_ACCESS_WRITE, &handle);
    }
    if (status != OPENRFSFS_STATUS_OK) return false;
    size_t done = 0U;
    while (done < DATA_AEAD_MANIFEST_BYTES) {
        size_t written = 0U;
        status = context->backend->write(handle, record + done,
            DATA_AEAD_MANIFEST_BYTES - done, &written);
        if (status != OPENRFSFS_STATUS_OK || written == 0U ||
                written > DATA_AEAD_MANIFEST_BYTES - done) break;
        done += written;
    }
    const enum openrfsfs_status closed = context->backend->close(handle);
    return status == OPENRFSFS_STATUS_OK &&
        done == DATA_AEAD_MANIFEST_BYTES &&
        closed == OPENRFSFS_STATUS_OK;
}

static bool temp_sync(void *opaque)
{
    struct backend_context *context = opaque;
    return context->backend->sync(context->volume) == OPENRFSFS_STATUS_OK;
}

static bool temp_read(void *opaque,
    uint8_t record[DATA_AEAD_MANIFEST_BYTES])
{
    struct backend_context *context = opaque;
    bool present = false;
    return read_record_file(context, context->slot_paths[2], record,
        &present) && present;
}

static bool temp_publish(void *opaque, unsigned slot)
{
    struct backend_context *context = opaque;
    return slot < 2U && context->backend->rename(context->volume,
        context->slot_paths[2], context->slot_paths[slot]) ==
        OPENRFSFS_STATUS_OK;
}

static struct held_segment *find_segment(struct backend_context *context,
    const uint8_t revision_id[DATA_AEAD_ID_BYTES])
{
    for (unsigned at = 0U; at < DATA_AEAD_SEGMENTS_MAX; ++at)
        if (context->segments[at].used &&
                same_id(context->segments[at].revision_id, revision_id))
            return &context->segments[at];
    return NULL;
}

static bool segment_sync(void *opaque,
    const uint8_t revision_id[DATA_AEAD_ID_BYTES])
{
    struct backend_context *context = opaque;
    if (find_segment(context, revision_id) != NULL)
        return context->backend->sync(context->volume) ==
            OPENRFSFS_STATUS_OK;
    struct held_segment *entry = NULL;
    for (unsigned at = 0U; at < DATA_AEAD_SEGMENTS_MAX; ++at)
        if (!context->segments[at].used) {
            entry = &context->segments[at];
            break;
        }
    if (entry == NULL ||
            data_aead_segment_storage_path(context->key, revision_id,
                entry->path) != DATA_AEAD_OK ||
            context->backend->open(context->volume, entry->path,
                OPENRFSFS_ACCESS_READ, &entry->handle) !=
                OPENRFSFS_STATUS_OK)
        return false;
    for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at)
        entry->revision_id[at] = revision_id[at];
    entry->used = true;
    return context->backend->sync(context->volume) == OPENRFSFS_STATUS_OK;
}

static bool segment_size(void *opaque,
    const uint8_t revision_id[DATA_AEAD_ID_BYTES], uint64_t *size)
{
    struct backend_context *context = opaque;
    struct held_segment *entry = find_segment(context, revision_id);
    if (entry == NULL || size == NULL) return false;
    struct openrfsfs_stat stat;
    const enum openrfsfs_status status = context->backend->fstat != NULL ?
        context->backend->fstat(entry->handle, &stat) :
        context->backend->stat_path(context->volume, entry->path, &stat);
    if (status != OPENRFSFS_STATUS_OK || stat.directory) return false;
    *size = stat.size;
    return true;
}

static bool segment_read(void *opaque,
    const uint8_t revision_id[DATA_AEAD_ID_BYTES], uint64_t offset,
    uint8_t *to, size_t bytes)
{
    struct backend_context *context = opaque;
    struct held_segment *entry = find_segment(context, revision_id);
    if (entry == NULL) return false;
    size_t got = 0U;
    return context->backend->pread(entry->handle, to, bytes, offset,
        &got) == OPENRFSFS_STATUS_OK && got == bytes;
}

static bool random_nonce(void *opaque, uint8_t *to, size_t bytes)
{
    (void)opaque;
    return random_bytes(to, bytes) == RANDOM_STATUS_OK;
}

enum data_aead_status data_aead_backend_publish_manifest(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_manifest *candidate,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *published, unsigned *slot)
{
    if (candidate == published) return DATA_AEAD_ARGUMENT;
    if (published != NULL) zero_bytes(published, sizeof(*published));
    if (slot != NULL) *slot = 0U;
    if (!required_ops(backend) || volume != OPENRFSFS_VOLUME_DATA ||
            key == NULL || candidate == NULL || workspace == NULL ||
            published == NULL || slot == NULL)
        return DATA_AEAD_ARGUMENT;
    struct backend_context context = {0};
    context.backend = backend;
    context.volume = volume;
    context.key = key;
    for (unsigned at = 0U; at < 3U; ++at)
        if (data_aead_manifest_storage_path(key, canonical_path, at,
                context.slot_paths[at]) != DATA_AEAD_OK) {
            zero_bytes(&context, sizeof(context));
            return DATA_AEAD_ARGUMENT;
        }
    if (!ensure_parent_directories(&context, context.slot_paths[2])) {
        zero_bytes(&context, sizeof(context));
        return DATA_AEAD_IO;
    }
    const struct data_aead_manifest_publish_io io = {
        .context = &context,
        .slot_read = slot_read,
        .slot_remove = slot_remove,
        .slot_sync = slot_sync,
        .temp_write = temp_write,
        .temp_sync = temp_sync,
        .temp_read = temp_read,
        .temp_publish = temp_publish,
        .segment_sync = segment_sync,
        .segment_size = segment_size,
        .segment_read = segment_read,
        .random = random_nonce,
    };
    enum data_aead_status result = data_aead_manifest_publish(key,
        canonical_path, candidate, &io, workspace, workspace_bytes,
        published, slot);
    for (unsigned at = 0U; at < DATA_AEAD_SEGMENTS_MAX; ++at)
        if (context.segments[at].used &&
                backend->close(context.segments[at].handle) !=
                    OPENRFSFS_STATUS_OK)
            result = DATA_AEAD_IO;
    if (result != DATA_AEAD_OK) {
        zero_bytes(published, sizeof(*published));
        *slot = 0U;
    }
    zero_bytes(&context, sizeof(context));
    return result;
}
