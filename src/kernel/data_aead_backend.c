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

static bool is_symlink(const struct openrfsfs_stat *stat)
{
    return (stat->mode & 0170000U) == 0120000U;
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
    if (found != OPENRFSFS_STATUS_OK || stat.directory ||
            is_symlink(&stat)) return false;
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
                entry->path) != DATA_AEAD_OK)
        return false;
    struct openrfsfs_stat stat;
    const enum openrfsfs_status found =
        context->backend->lstat_path != NULL ?
        context->backend->lstat_path(context->volume, entry->path,
            &stat) : context->backend->stat_path(context->volume,
            entry->path, &stat);
    if (found != OPENRFSFS_STATUS_OK || stat.directory ||
            is_symlink(&stat) ||
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

struct held_segment_reader {
    const struct vfs_backend_ops *backend;
    openrfsfs_handle handle;
};

static bool held_segment_read(void *opaque, uint64_t offset, uint8_t *to,
    size_t bytes)
{
    const struct held_segment_reader *reader = opaque;
    size_t got = 0U;
    return reader->backend->pread(reader->handle, to, bytes, offset,
        &got) == OPENRFSFS_STATUS_OK && got == bytes;
}

enum data_aead_status data_aead_backend_load_manifest(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *manifest, unsigned *slot)
{
    if (manifest != NULL) zero_bytes(manifest, sizeof(*manifest));
    if (slot != NULL) *slot = 0U;
    if (!required_ops(backend) || volume != OPENRFSFS_VOLUME_DATA ||
            key == NULL || workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ||
            manifest == NULL || slot == NULL)
        return DATA_AEAD_ARGUMENT;
    struct backend_context context = {0};
    context.backend = backend;
    context.volume = volume;
    context.key = key;
    for (unsigned at = 0U; at < 2U; ++at)
        if (data_aead_manifest_storage_path(key, canonical_path, at,
                context.slot_paths[at]) != DATA_AEAD_OK) {
            zero_bytes(&context, sizeof(context));
            return DATA_AEAD_ARGUMENT;
        }
    const struct data_aead_manifest_slot_io io = {&context, slot_read};
    enum data_aead_status result = data_aead_manifest_select(key,
        canonical_path, &io, manifest, slot);
    if (result == DATA_AEAD_OK)
        for (unsigned at = 0U; at < manifest->segment_count; ++at) {
            const uint8_t *revision_id = manifest->segments[at].revision_id;
            uint64_t physical_bytes = 0U;
            if (!segment_sync(&context, revision_id) ||
                    !segment_size(&context, revision_id, &physical_bytes)) {
                result = DATA_AEAD_IO;
                break;
            }
            struct held_segment *entry = find_segment(&context,
                revision_id);
            if (entry == NULL) {
                result = DATA_AEAD_IO;
                break;
            }
            struct held_segment_reader reader = {
                backend, entry->handle
            };
            result = data_aead_manifest_verify_segment(key, manifest,
                at, physical_bytes, held_segment_read, &reader,
                workspace, workspace_bytes);
            if (result != DATA_AEAD_OK) break;
        }
    for (unsigned at = 0U; at < DATA_AEAD_SEGMENTS_MAX; ++at)
        if (context.segments[at].used &&
                backend->close(context.segments[at].handle) !=
                    OPENRFSFS_STATUS_OK)
            result = DATA_AEAD_IO;
    if (result != DATA_AEAD_OK) {
        zero_bytes(manifest, sizeof(*manifest));
        *slot = 0U;
    }
    zero_bytes(&context, sizeof(context));
    zero_bytes(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    return result;
}

enum data_aead_status data_aead_backend_read_file(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t offset, uint8_t *destination, size_t capacity,
    uint8_t *workspace, size_t workspace_bytes, size_t *read_bytes)
{
    if (read_bytes != NULL) *read_bytes = 0U;
    if (destination == NULL || read_bytes == NULL ||
            workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES) {
        if (destination != NULL) zero_bytes(destination, capacity);
        return DATA_AEAD_ARGUMENT;
    }
    struct data_aead_manifest manifest;
    unsigned slot = 0U;
    enum data_aead_status result = data_aead_backend_load_manifest(backend,
        volume, key, canonical_path, workspace, workspace_bytes,
        &manifest, &slot);
    if (result != DATA_AEAD_OK) goto done;
    if (offset >= manifest.plaintext_bytes || capacity == 0U)
        goto done;
    const uint64_t available = manifest.plaintext_bytes - offset;
    const size_t total = available < capacity ? (size_t)available : capacity;
    struct backend_context context = {0};
    context.backend = backend;
    context.volume = volume;
    context.key = key;
    size_t done_bytes = 0U;
    while (done_bytes < total) {
        const uint64_t position = offset + done_bytes;
        const unsigned index = (unsigned)(position /
            DATA_AEAD_SEGMENT_BYTES);
        if (index >= manifest.segment_count) {
            result = DATA_AEAD_FORMAT;
            break;
        }
        const struct data_aead_segment *segment =
            &manifest.segments[index];
        const uint64_t local = position % DATA_AEAD_SEGMENT_BYTES;
        if (local >= segment->plaintext_bytes) {
            result = DATA_AEAD_FORMAT;
            break;
        }
        size_t amount = total - done_bytes;
        const uint64_t segment_left = segment->plaintext_bytes - local;
        if (segment_left < amount) amount = (size_t)segment_left;
        uint64_t physical_bytes = 0U;
        if (!segment_sync(&context, segment->revision_id) ||
                !segment_size(&context, segment->revision_id,
                    &physical_bytes)) {
            result = DATA_AEAD_IO;
            break;
        }
        struct held_segment *entry = find_segment(&context,
            segment->revision_id);
        if (entry == NULL) {
            result = DATA_AEAD_IO;
            break;
        }
        struct held_segment_reader reader = {backend, entry->handle};
        uint8_t header[DATA_AEAD_HEADER_BYTES];
        if (!held_segment_read(&reader, 0U, header,
                sizeof(header))) {
            result = DATA_AEAD_IO;
        } else {
            result = data_aead_manifest_check_segment(key, &manifest,
                index, header, physical_bytes);
            if (result == DATA_AEAD_OK) {
                char binding[DATA_AEAD_PATH_MAX + 1U];
                result = data_aead_segment_binding(manifest.stable_id,
                    index, binding);
                if (result == DATA_AEAD_OK) {
                    size_t got = 0U;
                    result = data_aead_read_range(key, binding,
                        physical_bytes, local, destination + done_bytes,
                        amount, held_segment_read, &reader, workspace,
                        workspace_bytes, &got);
                    if (result == DATA_AEAD_OK && got != amount)
                        result = DATA_AEAD_IO;
                }
                zero_bytes(binding, sizeof(binding));
            }
        }
        zero_bytes(header, sizeof(header));
        if (result != DATA_AEAD_OK) break;
        done_bytes += amount;
    }
    for (unsigned at = 0U; at < DATA_AEAD_SEGMENTS_MAX; ++at)
        if (context.segments[at].used &&
                backend->close(context.segments[at].handle) !=
                    OPENRFSFS_STATUS_OK)
            result = DATA_AEAD_IO;
    zero_bytes(&context, sizeof(context));
    if (result == DATA_AEAD_OK) *read_bytes = total;
done:
    if (result != DATA_AEAD_OK) zero_bytes(destination, capacity);
    zero_bytes(&manifest, sizeof(manifest));
    zero_bytes(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    return result;
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

struct migration_context {
    struct backend_context storage;
    openrfsfs_handle source;
    openrfsfs_handle shadow;
    char shadow_path[DATA_AEAD_PATH_MAX + 1U];
    uint64_t source_offset;
    uint64_t shadow_offset;
    uint64_t shadow_size;
    bool source_open;
    bool shadow_open;
};

static bool migration_begin(void *opaque, uint64_t physical_bytes)
{
    struct migration_context *migration = opaque;
    struct backend_context *storage = &migration->storage;
    const enum openrfsfs_status removed = storage->backend->unlink(
        storage->volume, migration->shadow_path);
    if ((removed != OPENRFSFS_STATUS_OK &&
            removed != OPENRFSFS_STATUS_NOT_FOUND) ||
            (removed == OPENRFSFS_STATUS_OK &&
             storage->backend->sync(storage->volume) !=
                OPENRFSFS_STATUS_OK) ||
            !ensure_parent_directories(storage, migration->shadow_path))
        return false;
    enum openrfsfs_status status;
    if (storage->backend->open_options != NULL) {
        struct openrfsfs_stat stat;
        status = storage->backend->open_options(storage->volume,
            migration->shadow_path, OPENRFSFS_ACCESS_WRITE,
            OPENRFSFS_OPEN_CREATE | OPENRFSFS_OPEN_EXCLUSIVE,
            0600U, &migration->shadow, &stat);
    } else {
        status = storage->backend->create(storage->volume,
            migration->shadow_path, 0600U);
        if (status == OPENRFSFS_STATUS_OK)
            status = storage->backend->open(storage->volume,
                migration->shadow_path, OPENRFSFS_ACCESS_WRITE,
                &migration->shadow);
    }
    migration->shadow_open = status == OPENRFSFS_STATUS_OK;
    migration->shadow_offset = 0U;
    migration->shadow_size = physical_bytes;
    return migration->shadow_open;
}

static bool migration_read(void *opaque, uint64_t offset, uint8_t *to,
    size_t bytes)
{
    struct migration_context *migration = opaque;
    size_t got = 0U;
    return migration->storage.backend->pread(migration->source, to, bytes,
        migration->source_offset + offset, &got) == OPENRFSFS_STATUS_OK &&
        got == bytes;
}

static bool migration_write(void *opaque, uint64_t offset,
    const uint8_t *from, size_t bytes)
{
    struct migration_context *migration = opaque;
    if (!migration->shadow_open || offset != migration->shadow_offset ||
            migration->shadow_offset > migration->shadow_size ||
            bytes > migration->shadow_size - migration->shadow_offset)
        return false;
    size_t done = 0U;
    while (done < bytes) {
        size_t written = 0U;
        if (migration->storage.backend->write(migration->shadow,
                from + done, bytes - done, &written) !=
                OPENRFSFS_STATUS_OK || written == 0U ||
                written > bytes - done) return false;
        done += written;
    }
    migration->shadow_offset += bytes;
    return true;
}

static enum data_aead_status migration_remove_source(
    const struct vfs_backend_ops *backend, const char *canonical_path)
{
    struct openrfsfs_stat stat;
    const enum openrfsfs_status found = backend->lstat_path != NULL ?
        backend->lstat_path(OPENRFSFS_VOLUME_DATA, canonical_path,
            &stat) : backend->stat_path(OPENRFSFS_VOLUME_DATA,
            canonical_path, &stat);
    if (found == OPENRFSFS_STATUS_NOT_FOUND) return DATA_AEAD_OK;
    if (found != OPENRFSFS_STATUS_OK || stat.directory ||
            is_symlink(&stat))
        return DATA_AEAD_IO;
    if (backend->unlink(OPENRFSFS_VOLUME_DATA, canonical_path) !=
            OPENRFSFS_STATUS_OK ||
            backend->sync(OPENRFSFS_VOLUME_DATA) != OPENRFSFS_STATUS_OK)
        return DATA_AEAD_IO;
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_backend_migrate_plain(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *manifest)
{
    if (manifest != NULL) zero_bytes(manifest, sizeof(*manifest));
    if (!required_ops(backend) || volume != OPENRFSFS_VOLUME_DATA ||
            key == NULL || workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ||
            manifest == NULL) return DATA_AEAD_ARGUMENT;
    unsigned slot = 0U;
    enum data_aead_status result = data_aead_backend_load_manifest(backend,
        volume, key, canonical_path, workspace, workspace_bytes,
        manifest, &slot);
    if (result == DATA_AEAD_OK) {
        result = migration_remove_source(backend, canonical_path);
        if (result != DATA_AEAD_OK) zero_bytes(manifest, sizeof(*manifest));
        return result;
    }
    if (result != DATA_AEAD_NOT_FOUND) return result;
    struct migration_context migration = {0};
    struct data_aead_manifest candidate = {0};
    migration.storage.backend = backend;
    migration.storage.volume = volume;
    migration.storage.key = key;
    struct openrfsfs_stat source_stat;
    enum openrfsfs_status status = backend->lstat_path != NULL ?
        backend->lstat_path(volume, canonical_path, &source_stat) :
        backend->stat_path(volume, canonical_path, &source_stat);
    if (status == OPENRFSFS_STATUS_NOT_FOUND) return DATA_AEAD_NOT_FOUND;
    if (status != OPENRFSFS_STATUS_OK || source_stat.directory ||
            is_symlink(&source_stat))
        return DATA_AEAD_IO;
    if (source_stat.size >
            DATA_AEAD_SEGMENT_BYTES * DATA_AEAD_SEGMENTS_MAX)
        return DATA_AEAD_RANGE;
    if (backend->open(volume, canonical_path, OPENRFSFS_ACCESS_READ,
            &migration.source) != OPENRFSFS_STATUS_OK)
        return DATA_AEAD_IO;
    migration.source_open = true;
    if (backend->fstat != NULL &&
            (backend->fstat(migration.source, &source_stat) !=
                OPENRFSFS_STATUS_OK || source_stat.directory)) {
        result = DATA_AEAD_IO;
        goto done;
    }
    const uint64_t source_size = source_stat.size;
    if (source_size > DATA_AEAD_SEGMENT_BYTES * DATA_AEAD_SEGMENTS_MAX) {
        result = DATA_AEAD_RANGE;
        goto done;
    }
    candidate.generation = 1U;
    candidate.plaintext_bytes = source_size;
    candidate.segment_count = (unsigned)((source_size +
        DATA_AEAD_SEGMENT_BYTES - 1U) / DATA_AEAD_SEGMENT_BYTES);
    uint8_t unused_id[DATA_AEAD_ID_BYTES];
    result = data_aead_migration_ids(key, canonical_path, 0U,
        candidate.stable_id, unused_id);
    zero_bytes(unused_id, sizeof(unused_id));
    if (result != DATA_AEAD_OK) goto done;
    const struct data_aead_rewrite_io io = {
        .context = &migration,
        .begin_shadow = migration_begin,
        .read_old = migration_read,
        .write_shadow = migration_write,
        .random = random_nonce,
    };
    for (unsigned at = 0U; at < candidate.segment_count; ++at) {
        struct data_aead_segment *segment = &candidate.segments[at];
        const uint64_t remaining = source_size -
            (uint64_t)at * DATA_AEAD_SEGMENT_BYTES;
        segment->plaintext_bytes = remaining < DATA_AEAD_SEGMENT_BYTES ?
            remaining : DATA_AEAD_SEGMENT_BYTES;
        segment->generation = 1U;
        uint8_t stable_id[DATA_AEAD_ID_BYTES];
        result = data_aead_migration_ids(key, canonical_path, at,
            stable_id, segment->revision_id);
        if (result != DATA_AEAD_OK ||
                !same_id(stable_id, candidate.stable_id)) {
            zero_bytes(stable_id, sizeof(stable_id));
            result = DATA_AEAD_CONFLICT;
            goto done;
        }
        zero_bytes(stable_id, sizeof(stable_id));
        result = data_aead_segment_storage_path(key, segment->revision_id,
            migration.shadow_path);
        if (result != DATA_AEAD_OK) goto done;
        char binding[DATA_AEAD_PATH_MAX + 1U];
        result = data_aead_segment_binding(candidate.stable_id, at,
            binding);
        if (result != DATA_AEAD_OK) goto done;
        migration.source_offset = (uint64_t)at * DATA_AEAD_SEGMENT_BYTES;
        uint64_t physical_bytes = 0U;
        result = data_aead_migrate_plain_shadow_identified(key, binding,
            segment->plaintext_bytes, candidate.stable_id,
            segment->revision_id, 1U, &io, workspace, workspace_bytes,
            &physical_bytes);
        zero_bytes(binding, sizeof(binding));
        if (result != DATA_AEAD_OK) goto done;
        if (!migration.shadow_open ||
                migration.shadow_offset != physical_bytes ||
                backend->close(migration.shadow) != OPENRFSFS_STATUS_OK) {
            migration.shadow_open = false;
            result = DATA_AEAD_IO;
            goto done;
        }
        migration.shadow_open = false;
        if (backend->sync(volume) != OPENRFSFS_STATUS_OK) {
            result = DATA_AEAD_IO;
            goto done;
        }
    }
    {
        struct data_aead_manifest published;
        result = data_aead_backend_publish_manifest(backend, volume, key,
            canonical_path, &candidate, workspace, workspace_bytes,
            &published, &slot);
        if (result == DATA_AEAD_OK) {
            if (backend->close(migration.source) != OPENRFSFS_STATUS_OK)
                result = DATA_AEAD_IO;
            migration.source_open = false;
        }
        if (result == DATA_AEAD_OK)
            result = migration_remove_source(backend, canonical_path);
        if (result == DATA_AEAD_OK) *manifest = published;
        zero_bytes(&published, sizeof(published));
    }
done:
    if (migration.shadow_open &&
            backend->close(migration.shadow) != OPENRFSFS_STATUS_OK)
        result = DATA_AEAD_IO;
    if (migration.source_open &&
            backend->close(migration.source) != OPENRFSFS_STATUS_OK)
        result = DATA_AEAD_IO;
    if (result != DATA_AEAD_OK) zero_bytes(manifest, sizeof(*manifest));
    zero_bytes(&candidate, sizeof(candidate));
    zero_bytes(&migration, sizeof(migration));
    zero_bytes(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    return result;
}

static void encode_u64(uint8_t to[8], uint64_t value)
{
    for (size_t at = 0U; at < 8U; ++at)
        to[at] = (uint8_t)(value >> (8U * at));
}

static enum data_aead_status append_revision_id(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const uint8_t stable_id[DATA_AEAD_ID_BYTES],
    const uint8_t old_revision[DATA_AEAD_ID_BYTES],
    uint64_t manifest_generation, unsigned segment_index,
    uint64_t old_bytes, const uint8_t *addition, size_t addition_bytes,
    uint8_t revision_id[DATA_AEAD_ID_BYTES])
{
    static const uint8_t label[] = "OpenRFS/v1/data/append-revision";
    uint8_t numbers[32] = {0};
    encode_u64(numbers, manifest_generation);
    encode_u64(numbers + 8U, segment_index);
    encode_u64(numbers + 16U, old_bytes);
    encode_u64(numbers + 24U, addition_bytes);
    crypto_blake2b_ctx hash;
    crypto_blake2b_keyed_init(&hash, DATA_AEAD_ID_BYTES, key,
        DATA_AEAD_KEY_BYTES);
    crypto_blake2b_update(&hash, label, sizeof(label) - 1U);
    crypto_blake2b_update(&hash, stable_id, DATA_AEAD_ID_BYTES);
    crypto_blake2b_update(&hash, old_revision, DATA_AEAD_ID_BYTES);
    crypto_blake2b_update(&hash, numbers, sizeof(numbers));
    crypto_blake2b_update(&hash, addition, addition_bytes);
    crypto_blake2b_final(&hash, revision_id);
    crypto_wipe(&hash, sizeof(hash));
    zero_bytes(numbers, sizeof(numbers));
    uint8_t any = 0U;
    for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at)
        any |= revision_id[at];
    if (any == 0U || same_id(revision_id, old_revision)) {
        zero_bytes(revision_id, DATA_AEAD_ID_BYTES);
        return DATA_AEAD_ENTROPY;
    }
    return DATA_AEAD_OK;
}

static enum data_aead_status append_candidate(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const struct data_aead_manifest *active, const uint8_t *addition,
    size_t addition_bytes, struct data_aead_manifest *candidate)
{
    if (active->generation == UINT64_MAX ||
            active->plaintext_bytes >
                DATA_AEAD_SEGMENT_BYTES * DATA_AEAD_SEGMENTS_MAX -
                    addition_bytes)
        return DATA_AEAD_RANGE;
    *candidate = *active;
    candidate->generation++;
    candidate->plaintext_bytes += addition_bytes;
    size_t consumed = 0U;
    while (consumed < addition_bytes) {
        const uint64_t position = active->plaintext_bytes + consumed;
        const unsigned index = (unsigned)(position /
            DATA_AEAD_SEGMENT_BYTES);
        const bool replacing = index < active->segment_count;
        const struct data_aead_segment *previous = replacing ?
            &active->segments[index] : NULL;
        const uint64_t old_bytes = replacing ?
            previous->plaintext_bytes : 0U;
        size_t amount = addition_bytes - consumed;
        if (DATA_AEAD_SEGMENT_BYTES - old_bytes < amount)
            amount = (size_t)(DATA_AEAD_SEGMENT_BYTES - old_bytes);
        struct data_aead_segment *next = &candidate->segments[index];
        uint8_t old_revision[DATA_AEAD_ID_BYTES] = {0};
        if (replacing)
            for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at)
                old_revision[at] = previous->revision_id[at];
        enum data_aead_status result = append_revision_id(key,
            active->stable_id, old_revision, candidate->generation,
            index, old_bytes, addition + consumed, amount,
            next->revision_id);
        zero_bytes(old_revision, sizeof(old_revision));
        if (result != DATA_AEAD_OK) return result;
        next->plaintext_bytes = old_bytes + amount;
        next->generation = replacing ? previous->generation + 1U : 1U;
        if (next->generation == 0U) return DATA_AEAD_RANGE;
        if (candidate->segment_count <= index)
            candidate->segment_count = index + 1U;
        consumed += amount;
    }
    return DATA_AEAD_OK;
}

static bool same_manifest(const struct data_aead_manifest *left,
    const struct data_aead_manifest *right)
{
    if (!same_id(left->stable_id, right->stable_id) ||
            left->generation != right->generation ||
            left->plaintext_bytes != right->plaintext_bytes ||
            left->segment_count != right->segment_count)
        return false;
    for (unsigned at = 0U; at < left->segment_count; ++at)
        if (!same_id(left->segments[at].revision_id,
                right->segments[at].revision_id) ||
                left->segments[at].generation !=
                    right->segments[at].generation ||
                left->segments[at].plaintext_bytes !=
                    right->segments[at].plaintext_bytes)
            return false;
    return true;
}

enum data_aead_status data_aead_backend_append_file(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t expected_generation, const uint8_t *addition,
    size_t addition_bytes, uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *published)
{
    if (published != NULL) zero_bytes(published, sizeof(*published));
    if (published == NULL || key == NULL || addition == NULL ||
            addition_bytes == 0U ||
            addition_bytes > DATA_AEAD_CHUNK_BYTES ||
            expected_generation == 0U || workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES)
        return DATA_AEAD_ARGUMENT;
    struct data_aead_manifest active = {0};
    unsigned slot = 0U;
    enum data_aead_status result = data_aead_backend_load_manifest(backend,
        volume, key, canonical_path, workspace, workspace_bytes,
        &active, &slot);
    if (result != DATA_AEAD_OK) goto done;
    if (expected_generation != UINT64_MAX &&
            active.generation == expected_generation + 1U) {
        struct backend_context context = {0};
        context.backend = backend;
        context.volume = volume;
        context.key = key;
        struct data_aead_manifest previous = {0};
        struct data_aead_manifest predicted = {0};
        uint8_t record[DATA_AEAD_MANIFEST_BYTES] = {0};
        bool present = false;
        result = data_aead_manifest_storage_path(key, canonical_path,
            slot ^ 1U, context.slot_paths[slot ^ 1U]);
        if (result == DATA_AEAD_OK &&
                !slot_read(&context, slot ^ 1U, record, &present))
            result = DATA_AEAD_IO;
        if (result == DATA_AEAD_OK)
            result = present ? data_aead_manifest_open(key,
                canonical_path, record, &previous) : DATA_AEAD_CONFLICT;
        if (result == DATA_AEAD_OK &&
                previous.generation != expected_generation)
            result = DATA_AEAD_CONFLICT;
        if (result == DATA_AEAD_OK)
            result = append_candidate(key, &previous, addition,
                addition_bytes, &predicted);
        if (result == DATA_AEAD_OK && !same_manifest(&active, &predicted))
            result = DATA_AEAD_CONFLICT;
        if (result == DATA_AEAD_OK) *published = active;
        zero_bytes(record, sizeof(record));
        zero_bytes(&previous, sizeof(previous));
        zero_bytes(&predicted, sizeof(predicted));
        zero_bytes(&context, sizeof(context));
        goto done;
    }
    if (active.generation != expected_generation ||
            active.generation == UINT64_MAX) {
        result = DATA_AEAD_CONFLICT;
        goto done;
    }
    struct data_aead_manifest candidate = {0};
    result = append_candidate(key, &active, addition, addition_bytes,
        &candidate);
    if (result != DATA_AEAD_OK) goto done;
    const struct data_aead_rewrite_io io = {
        .begin_shadow = migration_begin,
        .read_old = migration_read,
        .write_shadow = migration_write,
        .random = random_nonce,
    };
    size_t consumed = 0U;
    while (consumed < addition_bytes) {
        const uint64_t position = active.plaintext_bytes + consumed;
        const unsigned index = (unsigned)(position /
            DATA_AEAD_SEGMENT_BYTES);
        if (index >= DATA_AEAD_SEGMENTS_MAX) {
            result = DATA_AEAD_RANGE;
            break;
        }
        const bool replacing = index < active.segment_count;
        const struct data_aead_segment *previous = replacing ?
            &active.segments[index] : NULL;
        const uint64_t old_bytes = replacing ?
            previous->plaintext_bytes : 0U;
        size_t amount = addition_bytes - consumed;
        if (DATA_AEAD_SEGMENT_BYTES - old_bytes < amount)
            amount = (size_t)(DATA_AEAD_SEGMENT_BYTES - old_bytes);
        const struct data_aead_segment *next = &candidate.segments[index];
        struct migration_context migration = {0};
        migration.storage.backend = backend;
        migration.storage.volume = volume;
        migration.storage.key = key;
        result = data_aead_segment_storage_path(key, next->revision_id,
            migration.shadow_path);
        if (result != DATA_AEAD_OK) break;
        uint64_t old_physical_bytes = 0U;
        if (replacing) {
            char old_path[DATA_AEAD_PATH_MAX + 1U];
            result = data_aead_segment_storage_path(key,
                previous->revision_id, old_path);
            if (result == DATA_AEAD_OK) {
                struct openrfsfs_stat stat;
                const enum openrfsfs_status found =
                    backend->lstat_path != NULL ?
                    backend->lstat_path(volume, old_path, &stat) :
                    backend->stat_path(volume, old_path, &stat);
                old_physical_bytes = data_aead_physical_size(old_bytes);
                if (found != OPENRFSFS_STATUS_OK || stat.directory ||
                        is_symlink(&stat) ||
                        stat.size != old_physical_bytes ||
                        backend->open(volume, old_path,
                            OPENRFSFS_ACCESS_READ, &migration.source) !=
                            OPENRFSFS_STATUS_OK)
                    result = DATA_AEAD_IO;
                else
                    migration.source_open = true;
            }
            zero_bytes(old_path, sizeof(old_path));
        }
        if (result == DATA_AEAD_OK) {
            char binding[DATA_AEAD_PATH_MAX + 1U];
            result = data_aead_segment_binding(active.stable_id, index,
                binding);
            if (result == DATA_AEAD_OK) {
                struct data_aead_rewrite_io rewrite = io;
                rewrite.context = &migration;
                uint64_t new_physical_bytes = 0U;
                result = data_aead_rewrite_shadow_paths_identified(key,
                    binding, binding, old_physical_bytes,
                    next->plaintext_bytes, old_bytes,
                    addition + consumed, amount, active.stable_id,
                    next->revision_id, next->generation, &rewrite,
                    workspace, workspace_bytes, &new_physical_bytes);
                if (result == DATA_AEAD_OK &&
                        (!migration.shadow_open ||
                         migration.shadow_offset != new_physical_bytes ||
                         backend->close(migration.shadow) !=
                            OPENRFSFS_STATUS_OK)) {
                    result = DATA_AEAD_IO;
                }
                if (result == DATA_AEAD_OK) {
                    migration.shadow_open = false;
                    if (backend->sync(volume) != OPENRFSFS_STATUS_OK)
                        result = DATA_AEAD_IO;
                }
            }
            zero_bytes(binding, sizeof(binding));
        }
        if (migration.shadow_open &&
                backend->close(migration.shadow) != OPENRFSFS_STATUS_OK)
            result = DATA_AEAD_IO;
        if (migration.source_open &&
                backend->close(migration.source) != OPENRFSFS_STATUS_OK)
            result = DATA_AEAD_IO;
        zero_bytes(&migration, sizeof(migration));
        if (result != DATA_AEAD_OK) break;
        consumed += amount;
    }
    if (result == DATA_AEAD_OK)
        result = data_aead_backend_publish_manifest(backend, volume, key,
            canonical_path, &candidate, workspace, workspace_bytes,
            published, &slot);
    zero_bytes(&candidate, sizeof(candidate));
done:
    if (result != DATA_AEAD_OK)
        zero_bytes(published, sizeof(*published));
    zero_bytes(&active, sizeof(active));
    zero_bytes(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    return result;
}
