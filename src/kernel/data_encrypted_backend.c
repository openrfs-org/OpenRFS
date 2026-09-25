/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_encrypted_backend.h>
#include <openrfs/data_encrypted_migration.h>
#include <openrfs/data_namespace_backend.h>
#include <openrfs/random.h>

#include "../../vendor/monocypher/src/monocypher.h"

#define DATA_ENCRYPTED_ENTRIES 384U
#define DATA_ENCRYPTED_HANDLES OPENRFSFS_MAX_HANDLES

struct encrypted_handle {
    struct data_aead_backend_reader reader;
    struct openrfsfs_stat stat_snapshot;
    openrfsfs_handle physical_handle;
    uint8_t stable_id[DATA_AEAD_ID_BYTES];
    char credential_path[OPENRFSFS_MAX_PATH];
    uint64_t generation;
    uint64_t offset;
    uint64_t session_epoch;
    enum openrfsfs_access access;
    bool active;
    bool credential;
    bool directory;
    size_t directory_cursor;
};

static const struct vfs_backend_ops *physical;
static void (*forget_session_key)(void);
static struct vfs_backend_ops encrypted_ops;
static uint8_t data_key[DATA_AEAD_KEY_BYTES];
static uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
static struct data_ns_entry entries[DATA_ENCRYPTED_ENTRIES];
static struct data_ns_entry scratch[DATA_ENCRYPTED_ENTRIES];
static struct data_ns_state namespace_state;
static struct encrypted_handle handles[DATA_ENCRYPTED_HANDLES];
static uint64_t namespace_generation;
static uint64_t session_epoch = 1U;
static uint64_t next_handle_generation = 1U;
static bool active;
static bool busy;
static bool revoke_pending;

static void clear_state_locked(void);

static void wipe(void *memory, size_t bytes)
{
    volatile uint8_t *to = memory;
    for (size_t at = 0U; at < bytes; ++at) to[at] = 0U;
}

static void copy_bytes(void *destination, const void *source, size_t bytes)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    for (size_t at = 0U; at < bytes; ++at) to[at] = from[at];
}

static bool same_text(const char *left, const char *right)
{
    if (left == NULL || right == NULL) return false;
    for (size_t at = 0U; at < OPENRFSFS_MAX_PATH; ++at) {
        if (left[at] != right[at]) return false;
        if (left[at] == '\0') return true;
    }
    return false;
}

static size_t text_length(const char *value)
{
    if (value == NULL) return OPENRFSFS_MAX_PATH;
    size_t at = 0U;
    while (at < OPENRFSFS_MAX_PATH && value[at] != '\0') ++at;
    return at;
}

static void display_name(char *destination, const char *source)
{
    const size_t length = text_length(source);
    for (size_t at = 0U; at <= length; ++at) {
        char ch = source[at];
        if (physical != NULL && !physical->case_sensitive &&
                ch >= 'A' && ch <= 'Z')
            ch = (char)(ch + ('a' - 'A'));
        destination[at] = ch;
    }
}

static bool reserved_root_name(const char *path)
{
    static const char reserved[] = "OPENRFS";
    if (path == NULL || text_length(path) != sizeof(reserved) - 1U)
        return false;
    for (size_t at = 0U; at < sizeof(reserved) - 1U; ++at) {
        char ch = path[at];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        if (ch != reserved[at]) return false;
    }
    return true;
}

static bool credential_path(const char *path)
{
    return same_text(path, "OPENRFS") ||
        same_text(path, "OPENRFS/LOGIN.DAT") ||
        same_text(path, "OPENRFS/LOGIN.NEW") ||
        same_text(path, "OPENRFS/LOGIN.V2A") ||
        same_text(path, "OPENRFS/LOGIN.V2B");
}

static bool credential_file(const char *path)
{
    return credential_path(path) && !same_text(path, "OPENRFS");
}

static bool begin_work(void)
{
    return !__atomic_exchange_n(&busy, true, __ATOMIC_ACQ_REL);
}

static void end_work(void)
{
    const bool revoke = revoke_pending;
    revoke_pending = false;
    __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
    if (revoke && forget_session_key != NULL) forget_session_key();
}

static void fail_session(void)
{
    clear_state_locked();
    revoke_pending = true;
}

static enum openrfsfs_status map_aead(enum data_aead_status status)
{
    if (status == DATA_AEAD_OK) return OPENRFSFS_STATUS_OK;
    if (status == DATA_AEAD_ARGUMENT)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (status == DATA_AEAD_NOT_FOUND)
        return OPENRFSFS_STATUS_NOT_FOUND;
    if (status == DATA_AEAD_RANGE) return OPENRFSFS_STATUS_RANGE;
    if (status == DATA_AEAD_CONFLICT) return OPENRFSFS_STATUS_BUSY;
    if (status == DATA_AEAD_ENTROPY) {
        fail_session();
        return OPENRFSFS_STATUS_IO;
    }
    if (status == DATA_AEAD_IO) {
        fail_session();
        return OPENRFSFS_STATUS_IO;
    }
    fail_session();
    return OPENRFSFS_STATUS_CORRUPT;
}

static enum openrfsfs_status map_ns(enum data_ns_status status)
{
    if (status == DATA_NS_OK) return OPENRFSFS_STATUS_OK;
    if (status == DATA_NS_ARGUMENT)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (status == DATA_NS_NOT_FOUND)
        return OPENRFSFS_STATUS_NOT_FOUND;
    if (status == DATA_NS_FULL) return OPENRFSFS_STATUS_FULL;
    if (status == DATA_NS_CONFLICT) return OPENRFSFS_STATUS_EXISTS;
    if (status == DATA_NS_IO) {
        fail_session();
        return OPENRFSFS_STATUS_IO;
    }
    fail_session();
    return OPENRFSFS_STATUS_CORRUPT;
}

static uint64_t object_id(const uint8_t id[DATA_AEAD_ID_BYTES])
{
    uint64_t result = 0U;
    for (size_t at = 0U; at < 8U; ++at)
        result |= (uint64_t)id[at] << (8U * at);
    return result == 0U ? 1U : result;
}

static const struct data_ns_entry *find_id(
    const uint8_t id[DATA_AEAD_ID_BYTES])
{
    for (size_t at = 0U; at < namespace_state.capacity; ++at)
        if (namespace_state.entries[at].active &&
                crypto_verify16(namespace_state.entries[at].child_id,
                    id) == 0)
            return &namespace_state.entries[at];
    return NULL;
}

static enum openrfsfs_status lookup(const char *path,
    uint8_t id[DATA_AEAD_ID_BYTES], enum data_ns_kind *kind,
    const struct data_ns_entry **entry)
{
    if (entry != NULL) *entry = NULL;
    if (!active) return OPENRFSFS_STATUS_ACCESS;
    const enum data_ns_status result = data_ns_resolve(&namespace_state,
        path, id, kind);
    if (result != DATA_NS_OK) return map_ns(result);
    if (entry != NULL && !same_text(path, ".")) {
        *entry = find_id(id);
        if (*entry == NULL) {
            fail_session();
            return OPENRFSFS_STATUS_CORRUPT;
        }
    }
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status binding_for(
    const uint8_t id[DATA_AEAD_ID_BYTES],
    char binding[DATA_AEAD_PATH_MAX + 1U])
{
    return map_ns(data_ns_file_binding(id, binding));
}

static enum openrfsfs_status file_size(
    const uint8_t id[DATA_AEAD_ID_BYTES], uint64_t *size)
{
    char binding[DATA_AEAD_PATH_MAX + 1U];
    enum openrfsfs_status result = binding_for(id, binding);
    if (result != OPENRFSFS_STATUS_OK) return result;
    struct data_aead_manifest manifest = {0};
    unsigned slot = 0U;
    result = map_aead(data_aead_backend_load_manifest(physical,
        OPENRFSFS_VOLUME_DATA, data_key, binding, workspace,
        sizeof(workspace), &manifest, &slot));
    if (result == OPENRFSFS_STATUS_NOT_FOUND) {
        fail_session();
        result = OPENRFSFS_STATUS_CORRUPT;
    }
    if (result == OPENRFSFS_STATUS_OK &&
            crypto_verify16(manifest.stable_id, id) != 0) {
        fail_session();
        result = OPENRFSFS_STATUS_CORRUPT;
    }
    if (result == OPENRFSFS_STATUS_OK) *size = manifest.plaintext_bytes;
    wipe(&manifest, sizeof(manifest));
    wipe(binding, sizeof(binding));
    return result;
}

static void fill_stat(const struct data_ns_entry *entry,
    const uint8_t id[DATA_AEAD_ID_BYTES], uint64_t size,
    struct openrfsfs_stat *stat)
{
    wipe(stat, sizeof(*stat));
    stat->size = size;
    stat->object_id = object_id(id);
    stat->uid = entry == NULL ? 0U : entry->uid;
    stat->gid = entry == NULL ? 0U : entry->gid;
    stat->mode = (entry != NULL && entry->kind == DATA_NS_FILE ?
        0100000U : 0040000U) |
        (entry == NULL ? 0700U : entry->mode);
    stat->links = 1U;
    stat->attributes = entry == NULL ? 0U : entry->attributes;
    stat->directory = entry == NULL ||
        entry->kind == DATA_NS_DIRECTORY;
    stat->read_only = (stat->attributes & 1U) != 0U;
    if (entry != NULL) {
        stat->atime_seconds = (int64_t)entry->atime_seconds;
        stat->mtime_seconds = (int64_t)entry->mtime_seconds;
        stat->ctime_seconds = (int64_t)entry->ctime_seconds;
        stat->atime_nanos = entry->atime_nanos;
        stat->mtime_nanos = entry->mtime_nanos;
        stat->ctime_nanos = entry->ctime_nanos;
    }
}

static enum openrfsfs_status encrypted_mount(enum openrfsfs_volume volume)
{
    return physical != NULL ? physical->mount(volume) :
        OPENRFSFS_STATUS_NOT_MOUNTED;
}

static enum openrfsfs_status encrypted_unmount(enum openrfsfs_volume volume)
{
    data_encrypted_backend_deactivate();
    return physical != NULL ? physical->unmount(volume) :
        OPENRFSFS_STATUS_NOT_MOUNTED;
}

static enum openrfsfs_status encrypted_sync(enum openrfsfs_volume volume)
{
    return physical != NULL ? physical->sync(volume) :
        OPENRFSFS_STATUS_NOT_MOUNTED;
}

static struct openrfsfs_drive_info encrypted_drive(
    enum openrfsfs_volume volume)
{
    const struct openrfsfs_drive_info absent = {0};
    struct openrfsfs_drive_info drive = physical != NULL ?
        physical->drive(volume) : absent;
    if (physical != NULL)
        drive.filesystem = physical->case_sensitive ?
            OPENRFSFS_FILESYSTEM_EXT4PLUS :
            OPENRFSFS_FILESYSTEM_FAT32;
    return drive;
}

static uint64_t encrypted_completion_count(enum openrfsfs_volume volume)
{
    return physical != NULL && physical->completion_count != NULL ?
        physical->completion_count(volume) : 0U;
}

static enum openrfsfs_status encrypted_stat_path(
    enum openrfsfs_volume volume, const char *path,
    struct openrfsfs_stat *stat)
{
    if (stat == NULL) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    wipe(stat, sizeof(*stat));
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (credential_path(path))
        return physical->stat_path(volume, path, stat);
    if (same_text(path, ".") && !active)
        return physical->stat_path(volume, path, stat);
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    const struct data_ns_entry *entry = NULL;
    enum openrfsfs_status result = lookup(path, id, &kind, &entry);
    uint64_t size = 0U;
    if (result == OPENRFSFS_STATUS_OK && kind == DATA_NS_FILE)
        result = file_size(id, &size);
    if (result == OPENRFSFS_STATUS_OK)
        fill_stat(entry, id, size, stat);
    wipe(id, sizeof(id));
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_list(
    enum openrfsfs_volume volume, const char *path,
    struct openrfsfs_list_entry *output, size_t capacity,
    size_t *entry_count)
{
    if (entry_count != NULL) *entry_count = 0U;
    if (entry_count == NULL || (capacity != 0U && output == NULL) ||
            capacity > SIZE_MAX / sizeof(*output))
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (same_text(path, "OPENRFS"))
        return OPENRFSFS_STATUS_OK;
    if (credential_file(path))
        return OPENRFSFS_STATUS_NOT_DIRECTORY;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    uint8_t parent_id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    enum openrfsfs_status result = lookup(path, parent_id, &kind, NULL);
    if (result == OPENRFSFS_STATUS_OK && kind != DATA_NS_DIRECTORY)
        result = OPENRFSFS_STATUS_NOT_DIRECTORY;
    size_t count = 0U;
    if (result == OPENRFSFS_STATUS_OK)
        for (size_t at = 0U; at < namespace_state.capacity; ++at)
            if (namespace_state.entries[at].active &&
                    crypto_verify16(namespace_state.entries[at].parent_id,
                        parent_id) == 0)
                ++count;
    if (result == OPENRFSFS_STATUS_OK && count > capacity)
        result = OPENRFSFS_STATUS_DIRECTORY_FULL;
    size_t written = 0U;
    if (result == OPENRFSFS_STATUS_OK)
        for (size_t at = 0U; at < namespace_state.capacity; ++at) {
            const struct data_ns_entry *entry =
                &namespace_state.entries[at];
            if (!entry->active ||
                    crypto_verify16(entry->parent_id,
                        parent_id) != 0) continue;
            uint64_t size = 0U;
            if (entry->kind == DATA_NS_FILE) {
                result = file_size(entry->child_id, &size);
                if (result != OPENRFSFS_STATUS_OK) break;
            }
            struct openrfsfs_list_entry *to = &output[written++];
            wipe(to, sizeof(*to));
            display_name(to->name, entry->name);
            to->size = size;
            to->object_id = object_id(entry->child_id);
            to->mode = (entry->kind == DATA_NS_FILE ?
                0100000U : 0040000U) | entry->mode;
            to->attributes = entry->attributes;
            to->directory = entry->kind == DATA_NS_DIRECTORY;
        }
    if (result == OPENRFSFS_STATUS_OK) *entry_count = written;
    else if (output != NULL)
        wipe(output, capacity * sizeof(*output));
    wipe(parent_id, sizeof(parent_id));
    end_work();
    return result;
}

static struct encrypted_handle *handle_state(openrfsfs_handle handle)
{
    const size_t index = (size_t)(handle & UINT64_C(0xff));
    const uint64_t generation = handle >> 8U;
    if (index == 0U || index > DATA_ENCRYPTED_HANDLES)
        return NULL;
    struct encrypted_handle *state = &handles[index - 1U];
    return state->active && state->generation == generation ? state : NULL;
}

static struct encrypted_handle *claim_handle(size_t *index)
{
    for (size_t at = 0U; at < DATA_ENCRYPTED_HANDLES; ++at)
        if (!handles[at].active) {
            *index = at;
            wipe(&handles[at], sizeof(handles[at]));
            handles[at].active = true;
            handles[at].generation = ++next_handle_generation;
            if (next_handle_generation == 0U) {
                next_handle_generation = 1U;
                handles[at].generation = 1U;
            }
            return &handles[at];
        }
    return NULL;
}

static openrfsfs_handle public_handle(size_t index,
    const struct encrypted_handle *state)
{
    return (state->generation << 8U) | (index + 1U);
}

static enum openrfsfs_status raw_open_options(
    const char *path, enum openrfsfs_access access, uint8_t flags,
    uint16_t mode, openrfsfs_handle *handle,
    struct openrfsfs_stat *stat)
{
    if (physical->open_options != NULL)
        return physical->open_options(OPENRFSFS_VOLUME_DATA, path,
            access, flags, mode, handle, stat);
    enum openrfsfs_status result = OPENRFSFS_STATUS_OK;
    if ((flags & OPENRFSFS_OPEN_CREATE) != 0U) {
        result = physical->stat_path(OPENRFSFS_VOLUME_DATA, path, stat);
        if (result == OPENRFSFS_STATUS_OK &&
                (flags & OPENRFSFS_OPEN_EXCLUSIVE) != 0U)
            return OPENRFSFS_STATUS_EXISTS;
        if (result == OPENRFSFS_STATUS_NOT_FOUND)
            result = physical->create(OPENRFSFS_VOLUME_DATA,
                path, mode);
        if (result != OPENRFSFS_STATUS_OK) return result;
    }
    if ((flags & OPENRFSFS_OPEN_TRUNCATE) != 0U) {
        result = physical->truncate(OPENRFSFS_VOLUME_DATA, path, 0U);
        if (result != OPENRFSFS_STATUS_OK) return result;
    }
    result = physical->open(OPENRFSFS_VOLUME_DATA, path,
        access, handle);
    if (result == OPENRFSFS_STATUS_OK)
        result = physical->stat_path(OPENRFSFS_VOLUME_DATA,
            path, stat);
    if (result != OPENRFSFS_STATUS_OK && *handle != 0U) {
        (void)physical->close(*handle);
        *handle = 0U;
    }
    return result;
}

static enum openrfsfs_status parent_name(const char *path,
    uint8_t parent_id[DATA_AEAD_ID_BYTES],
    char name[DATA_NS_NAME_BYTES + 1U])
{
    const size_t length = text_length(path);
    if (length == 0U || length >= OPENRFSFS_MAX_PATH ||
            same_text(path, ".")) return OPENRFSFS_STATUS_PATH;
    size_t slash = length;
    while (slash != 0U && path[slash - 1U] != '/') --slash;
    const size_t name_bytes = length - slash;
    if (name_bytes == 0U || name_bytes > DATA_NS_NAME_BYTES)
        return OPENRFSFS_STATUS_NAME_TOO_LONG;
    char parent_path[OPENRFSFS_MAX_PATH];
    wipe(parent_path, sizeof(parent_path));
    if (slash == 0U) copy_bytes(parent_path, ".", 2U);
    else copy_bytes(parent_path, path, slash - 1U);
    copy_bytes(name, path + slash, name_bytes);
    enum data_ns_kind kind = 0;
    const enum openrfsfs_status result = lookup(parent_path,
        parent_id, &kind, NULL);
    wipe(parent_path, sizeof(parent_path));
    if (result != OPENRFSFS_STATUS_OK) return result;
    return kind == DATA_NS_DIRECTORY ? OPENRFSFS_STATUS_OK :
        OPENRFSFS_STATUS_NOT_DIRECTORY;
}

static enum openrfsfs_status append_event(
    const struct data_ns_event *event)
{
    uint64_t next_generation = 0U;
    const enum openrfsfs_status result = map_ns(data_ns_backend_append(
        physical, OPENRFSFS_VOLUME_DATA, data_key, workspace,
        sizeof(workspace), &namespace_state, scratch,
        namespace_generation, event, &next_generation));
    if (result == OPENRFSFS_STATUS_OK)
        namespace_generation = next_generation;
    return result;
}

static void event_from_entry(const struct data_ns_entry *entry,
    struct data_ns_event *event)
{
    wipe(event, sizeof(*event));
    copy_bytes(event->parent_id, entry->parent_id, DATA_AEAD_ID_BYTES);
    copy_bytes(event->child_id, entry->child_id, DATA_AEAD_ID_BYTES);
    copy_bytes(event->name, entry->name, text_length(entry->name) + 1U);
    event->kind = entry->kind;
    event->mode = entry->mode;
    event->attributes = entry->attributes;
    event->uid = entry->uid;
    event->gid = entry->gid;
    event->atime_seconds = entry->atime_seconds;
    event->mtime_seconds = entry->mtime_seconds;
    event->ctime_seconds = entry->ctime_seconds;
    event->atime_nanos = entry->atime_nanos;
    event->mtime_nanos = entry->mtime_nanos;
    event->ctime_nanos = entry->ctime_nanos;
}

static enum openrfsfs_status create_object(const char *path,
    enum data_ns_kind kind, uint16_t mode)
{
    if (!active) return OPENRFSFS_STATUS_ACCESS;
    if (credential_path(path) || reserved_root_name(path) ||
            same_text(path, "."))
        return OPENRFSFS_STATUS_EXISTS;
    struct data_ns_event event;
    wipe(&event, sizeof(event));
    enum openrfsfs_status result = parent_name(path,
        event.parent_id, event.name);
    if (result != OPENRFSFS_STATUS_OK) goto done;
    if (data_ns_find(&namespace_state, event.parent_id,
            event.name) != NULL) {
        result = OPENRFSFS_STATUS_EXISTS;
        goto done;
    }
    if (namespace_state.count >= namespace_state.capacity) {
        result = OPENRFSFS_STATUS_DIRECTORY_FULL;
        goto done;
    }
    if (random_bytes(event.child_id, DATA_AEAD_ID_BYTES) !=
            RANDOM_STATUS_OK) {
        fail_session();
        result = OPENRFSFS_STATUS_IO;
        goto done;
    }
    event.kind = kind;
    event.operation = DATA_NS_CREATE;
    event.mode = mode & 07777U;
    if (kind == DATA_NS_FILE) {
        struct data_aead_manifest candidate = {0};
        struct data_aead_manifest published = {0};
        char binding[DATA_AEAD_PATH_MAX + 1U];
        result = binding_for(event.child_id, binding);
        if (result == OPENRFSFS_STATUS_OK) {
            copy_bytes(candidate.stable_id, event.child_id,
                DATA_AEAD_ID_BYTES);
            candidate.generation = 1U;
            unsigned slot = 0U;
            result = map_aead(data_aead_backend_publish_manifest(
                physical, OPENRFSFS_VOLUME_DATA, data_key, binding,
                &candidate, workspace, sizeof(workspace), &published,
                &slot));
        }
        wipe(&candidate, sizeof(candidate));
        wipe(&published, sizeof(published));
        wipe(binding, sizeof(binding));
        if (result != OPENRFSFS_STATUS_OK) goto done;
    }
    result = append_event(&event);
done:
    wipe(&event, sizeof(event));
    return result;
}

static enum openrfsfs_status encrypted_create(
    enum openrfsfs_volume volume, const char *path, uint16_t mode)
{
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (credential_file(path))
        return physical->create(volume, path, mode);
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    const enum openrfsfs_status result = create_object(path,
        DATA_NS_FILE, mode);
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_mkdir_mode(
    enum openrfsfs_volume volume, const char *path, uint16_t mode)
{
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (same_text(path, "OPENRFS"))
        return physical->mkdir_mode != NULL ?
            physical->mkdir_mode(volume, path, mode) :
            physical->mkdir(volume, path);
    if (credential_path(path)) return OPENRFSFS_STATUS_ACCESS;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    const enum openrfsfs_status result = create_object(path,
        DATA_NS_DIRECTORY, mode);
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_mkdir(
    enum openrfsfs_volume volume, const char *path)
{
    return encrypted_mkdir_mode(volume, path, 0755U);
}

static enum openrfsfs_status refresh_reader(struct encrypted_handle *state)
{
    char binding[DATA_AEAD_PATH_MAX + 1U];
    enum openrfsfs_status result = binding_for(state->stable_id, binding);
    if (result != OPENRFSFS_STATUS_OK) return result;
    struct data_aead_backend_reader next = {0};
    result = map_aead(data_aead_backend_reader_open(physical,
        OPENRFSFS_VOLUME_DATA, data_key, binding, workspace,
        sizeof(workspace), &next));
    if (result == OPENRFSFS_STATUS_NOT_FOUND) {
        fail_session();
        result = OPENRFSFS_STATUS_CORRUPT;
    }
    if (result == OPENRFSFS_STATUS_OK &&
            crypto_verify16(next.manifest.stable_id,
                state->stable_id) != 0) {
        fail_session();
        result = OPENRFSFS_STATUS_CORRUPT;
    }
    if (result == OPENRFSFS_STATUS_OK) {
        if (data_aead_backend_reader_close(&state->reader) !=
                DATA_AEAD_OK) {
            (void)data_aead_backend_reader_close(&next);
            fail_session();
            wipe(binding, sizeof(binding));
            return OPENRFSFS_STATUS_IO;
        }
        state->reader = next;
        state->stat_snapshot.size = next.manifest.plaintext_bytes;
    } else if (next.active) {
        (void)data_aead_backend_reader_close(&next);
    }
    wipe(binding, sizeof(binding));
    return result;
}

static enum openrfsfs_status rewrite_file(
    const uint8_t id[DATA_AEAD_ID_BYTES], uint64_t expected_generation,
    uint64_t new_size, uint64_t offset, const uint8_t *source,
    size_t source_bytes, bool append)
{
    char binding[DATA_AEAD_PATH_MAX + 1U];
    enum openrfsfs_status result = binding_for(id, binding);
    if (result != OPENRFSFS_STATUS_OK) return result;
    struct data_aead_manifest published = {0};
    if (append)
        result = map_aead(data_aead_backend_append_file(physical,
            OPENRFSFS_VOLUME_DATA, data_key, binding,
            expected_generation, source, source_bytes, workspace,
            sizeof(workspace), &published));
    else
        result = map_aead(data_aead_backend_rewrite_file(physical,
            OPENRFSFS_VOLUME_DATA, data_key, binding,
            expected_generation, new_size, offset, source, source_bytes,
            workspace, sizeof(workspace), &published));
    wipe(&published, sizeof(published));
    wipe(binding, sizeof(binding));
    return result;
}

static enum openrfsfs_status open_file_locked(const char *path,
    enum openrfsfs_access access, uint8_t flags, uint16_t mode,
    openrfsfs_handle *handle, struct openrfsfs_stat *stat)
{
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    const struct data_ns_entry *entry = NULL;
    enum openrfsfs_status result = lookup(path, id, &kind, &entry);
    if (result == OPENRFSFS_STATUS_NOT_FOUND &&
            (flags & OPENRFSFS_OPEN_CREATE) != 0U) {
        result = create_object(path, DATA_NS_FILE, mode);
        if (result == OPENRFSFS_STATUS_OK)
            result = lookup(path, id, &kind, &entry);
    } else if (result == OPENRFSFS_STATUS_OK &&
            (flags & OPENRFSFS_OPEN_EXCLUSIVE) != 0U) {
        result = OPENRFSFS_STATUS_EXISTS;
    }
    if (result != OPENRFSFS_STATUS_OK) goto done;
    if (kind != DATA_NS_FILE) {
        result = OPENRFSFS_STATUS_IS_DIRECTORY;
        goto done;
    }
    if ((flags & OPENRFSFS_OPEN_TRUNCATE) != 0U) {
        char binding[DATA_AEAD_PATH_MAX + 1U];
        struct data_aead_manifest current = {0};
        unsigned slot = 0U;
        result = binding_for(id, binding);
        if (result == OPENRFSFS_STATUS_OK)
            result = map_aead(data_aead_backend_load_manifest(physical,
                OPENRFSFS_VOLUME_DATA, data_key, binding,
                workspace, sizeof(workspace), &current, &slot));
        if (result == OPENRFSFS_STATUS_NOT_FOUND) {
            fail_session();
            result = OPENRFSFS_STATUS_CORRUPT;
        }
        if (result == OPENRFSFS_STATUS_OK &&
                crypto_verify16(current.stable_id, id) != 0) {
            fail_session();
            result = OPENRFSFS_STATUS_CORRUPT;
        }
        if (result == OPENRFSFS_STATUS_OK)
            result = rewrite_file(id, current.generation, 0U,
                0U, NULL, 0U, false);
        wipe(binding, sizeof(binding));
        wipe(&current, sizeof(current));
        if (result != OPENRFSFS_STATUS_OK) goto done;
    }
    size_t index = 0U;
    struct encrypted_handle *state = claim_handle(&index);
    if (state == NULL) {
        result = OPENRFSFS_STATUS_NO_HANDLES;
        goto done;
    }
    copy_bytes(state->stable_id, id, sizeof(id));
    state->session_epoch = session_epoch;
    state->access = access;
    result = refresh_reader(state);
    if (result == OPENRFSFS_STATUS_OK) {
        fill_stat(entry, id, state->reader.manifest.plaintext_bytes,
            &state->stat_snapshot);
        *stat = state->stat_snapshot;
        *handle = public_handle(index, state);
    } else {
        wipe(state, sizeof(*state));
    }
done:
    wipe(id, sizeof(id));
    return result;
}

static enum openrfsfs_status encrypted_open_options(
    enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, uint8_t flags, uint16_t mode,
    openrfsfs_handle *handle, struct openrfsfs_stat *stat)
{
    if (handle == NULL || stat == NULL || path == NULL)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    *handle = 0U;
    wipe(stat, sizeof(*stat));
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (!credential_file(path) && !active)
        return OPENRFSFS_STATUS_ACCESS;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    if (credential_file(path)) {
        size_t index = 0U;
        struct encrypted_handle *state = claim_handle(&index);
        if (state == NULL) {
            end_work();
            return OPENRFSFS_STATUS_NO_HANDLES;
        }
        enum openrfsfs_status result = raw_open_options(path, access,
            flags, mode, &state->physical_handle, stat);
        if (result == OPENRFSFS_STATUS_OK) {
            state->credential = true;
            state->access = access;
            state->stat_snapshot = *stat;
            copy_bytes(state->credential_path, path,
                text_length(path) + 1U);
            *handle = public_handle(index, state);
        } else wipe(state, sizeof(*state));
        end_work();
        return result;
    }
    const enum openrfsfs_status result = open_file_locked(path, access,
        flags, mode, handle, stat);
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_open(
    enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, openrfsfs_handle *handle)
{
    struct openrfsfs_stat stat;
    return encrypted_open_options(volume, path, access, 0U, 0644U,
        handle, &stat);
}

static enum openrfsfs_status encrypted_open_with_stat(
    enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, openrfsfs_handle *handle,
    struct openrfsfs_stat *stat)
{
    return encrypted_open_options(volume, path, access, 0U, 0644U,
        handle, stat);
}

static enum openrfsfs_status encrypted_close(openrfsfs_handle handle)
{
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    enum openrfsfs_status result = OPENRFSFS_STATUS_OK;
    if (state->credential && state->physical_handle != 0U)
        result = physical->close(state->physical_handle);
    else if (state->reader.active)
        result = map_aead(data_aead_backend_reader_close(&state->reader));
    wipe(state, sizeof(*state));
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_fstat(openrfsfs_handle handle,
    struct openrfsfs_stat *stat)
{
    if (stat == NULL) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    wipe(stat, sizeof(*stat));
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        const enum openrfsfs_status result = physical->fstat != NULL ?
            physical->fstat(state->physical_handle, stat) :
            physical->stat_path(OPENRFSFS_VOLUME_DATA,
                state->credential_path, stat);
        end_work();
        return result;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    enum openrfsfs_status result = refresh_reader(state);
    if (result == OPENRFSFS_STATUS_OK) *stat = state->stat_snapshot;
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_pread(openrfsfs_handle handle,
    uint8_t *destination, size_t capacity, uint64_t offset,
    size_t *read_bytes)
{
    if (read_bytes != NULL) *read_bytes = 0U;
    if (read_bytes == NULL || (capacity != 0U && destination == NULL))
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        const enum openrfsfs_status result = physical->pread(
            state->physical_handle, destination,
            capacity, offset, read_bytes);
        end_work();
        return result;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if ((state->access & OPENRFSFS_ACCESS_READ) == 0U) {
        end_work();
        return OPENRFSFS_STATUS_ACCESS;
    }
    if (capacity == 0U) {
        end_work();
        return OPENRFSFS_STATUS_OK;
    }
    enum openrfsfs_status result = refresh_reader(state);
    if (result == OPENRFSFS_STATUS_OK)
        result = map_aead(data_aead_backend_reader_read(&state->reader,
            data_key, offset, destination, capacity, workspace,
            sizeof(workspace), read_bytes));
    if (result != OPENRFSFS_STATUS_OK) {
        wipe(destination, capacity);
        *read_bytes = 0U;
    }
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_read(openrfsfs_handle handle,
    uint8_t *destination, size_t capacity, size_t *read_bytes)
{
    if (read_bytes != NULL) *read_bytes = 0U;
    if (read_bytes == NULL || (capacity != 0U && destination == NULL))
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        const enum openrfsfs_status result = physical->read(
            state->physical_handle, destination,
            capacity, read_bytes);
        end_work();
        return result;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if ((state->access & OPENRFSFS_ACCESS_READ) == 0U) {
        end_work();
        return OPENRFSFS_STATUS_ACCESS;
    }
    enum openrfsfs_status result = OPENRFSFS_STATUS_OK;
    if (capacity != 0U) {
        result = refresh_reader(state);
        if (result == OPENRFSFS_STATUS_OK)
            result = map_aead(data_aead_backend_reader_read(&state->reader,
                data_key, state->offset, destination, capacity, workspace,
                sizeof(workspace), read_bytes));
        if (result == OPENRFSFS_STATUS_OK) state->offset += *read_bytes;
        else {
            wipe(destination, capacity);
            *read_bytes = 0U;
        }
    }
    end_work();
    return result;
}

static enum openrfsfs_status write_file_locked(
    struct encrypted_handle *state, const uint8_t *source,
    size_t source_bytes, size_t *written_bytes, bool append)
{
    enum openrfsfs_status result = refresh_reader(state);
    if (result != OPENRFSFS_STATUS_OK) return result;
    const uint64_t old_size = state->reader.manifest.plaintext_bytes;
    const uint64_t at = append ? old_size : state->offset;
    const size_t amount = source_bytes > DATA_AEAD_CHUNK_BYTES ?
        DATA_AEAD_CHUNK_BYTES : source_bytes;
    if (at > DATA_AEAD_SEGMENT_BYTES * DATA_AEAD_SEGMENTS_MAX ||
            amount > DATA_AEAD_SEGMENT_BYTES *
                DATA_AEAD_SEGMENTS_MAX - at)
        return OPENRFSFS_STATUS_RANGE;
    const uint64_t next_size = old_size > at + amount ?
        old_size : at + amount;
    result = rewrite_file(state->stable_id,
        state->reader.manifest.generation, next_size, at, source,
        amount, append);
    if (result == OPENRFSFS_STATUS_OK)
        result = refresh_reader(state);
    if (result == OPENRFSFS_STATUS_OK) {
        state->offset = at + amount;
        *written_bytes = amount;
    }
    return result;
}

static enum openrfsfs_status encrypted_write_common(
    openrfsfs_handle handle, const uint8_t *source,
    size_t source_bytes, size_t *written_bytes, bool append)
{
    if (written_bytes != NULL) *written_bytes = 0U;
    if (written_bytes == NULL ||
            (source_bytes != 0U && source == NULL))
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        const enum openrfsfs_status result = append &&
                physical->append != NULL ?
            physical->append(state->physical_handle, source,
                source_bytes, written_bytes) :
            physical->write(state->physical_handle, source,
                source_bytes, written_bytes);
        end_work();
        return result;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if ((state->access & OPENRFSFS_ACCESS_WRITE) == 0U) {
        end_work();
        return OPENRFSFS_STATUS_ACCESS;
    }
    if (source_bytes == 0U) {
        end_work();
        return OPENRFSFS_STATUS_OK;
    }
    const enum openrfsfs_status result = write_file_locked(state,
        source, source_bytes, written_bytes, append);
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_write(openrfsfs_handle handle,
    const uint8_t *source, size_t source_bytes, size_t *written_bytes)
{
    return encrypted_write_common(handle, source, source_bytes,
        written_bytes, false);
}

static enum openrfsfs_status encrypted_append(openrfsfs_handle handle,
    const uint8_t *source, size_t source_bytes, size_t *written_bytes)
{
    return encrypted_write_common(handle, source, source_bytes,
        written_bytes, true);
}

static enum openrfsfs_status encrypted_seek(openrfsfs_handle handle,
    int64_t offset, enum openrfsfs_seek_origin origin,
    uint64_t *position)
{
    if (position == NULL) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    *position = 0U;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        const enum openrfsfs_status result = physical->seek(
            state->physical_handle, offset,
            origin, position);
        end_work();
        return result;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    enum openrfsfs_status result = refresh_reader(state);
    if (result == OPENRFSFS_STATUS_OK) {
        uint64_t base = 0U;
        if (origin == OPENRFSFS_SEEK_CURRENT) base = state->offset;
        else if (origin == OPENRFSFS_SEEK_END)
            base = state->reader.manifest.plaintext_bytes;
        else if (origin != OPENRFSFS_SEEK_START)
            result = OPENRFSFS_STATUS_INVALID_ARGUMENT;
        if (result == OPENRFSFS_STATUS_OK) {
            if (offset >= 0) {
                if ((uint64_t)offset > UINT64_MAX - base)
                    result = OPENRFSFS_STATUS_RANGE;
                else base += (uint64_t)offset;
            } else {
                const uint64_t distance = (uint64_t)(-(offset + 1)) + 1U;
                if (distance > base)
                    result = OPENRFSFS_STATUS_RANGE;
                else base -= distance;
            }
        }
        if (result == OPENRFSFS_STATUS_OK) {
            state->offset = base;
            *position = base;
        }
    }
    end_work();
    return result;
}

static enum openrfsfs_status truncate_id(
    const uint8_t id[DATA_AEAD_ID_BYTES], uint64_t size)
{
    char binding[DATA_AEAD_PATH_MAX + 1U];
    enum openrfsfs_status result = binding_for(id, binding);
    if (result != OPENRFSFS_STATUS_OK) return result;
    struct data_aead_manifest current = {0};
    unsigned slot = 0U;
    result = map_aead(data_aead_backend_load_manifest(physical,
        OPENRFSFS_VOLUME_DATA, data_key, binding, workspace,
        sizeof(workspace), &current, &slot));
    if (result == OPENRFSFS_STATUS_NOT_FOUND) {
        fail_session();
        result = OPENRFSFS_STATUS_CORRUPT;
    }
    if (result == OPENRFSFS_STATUS_OK &&
            crypto_verify16(current.stable_id, id) != 0) {
        fail_session();
        result = OPENRFSFS_STATUS_CORRUPT;
    }
    if (result == OPENRFSFS_STATUS_OK)
        result = rewrite_file(id, current.generation, size,
            0U, NULL, 0U, false);
    wipe(binding, sizeof(binding));
    wipe(&current, sizeof(current));
    return result;
}

static enum openrfsfs_status encrypted_truncate(
    enum openrfsfs_volume volume, const char *path, uint64_t size)
{
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (credential_file(path))
        return physical->truncate(volume, path, size);
    if (!active) return OPENRFSFS_STATUS_ACCESS;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    enum openrfsfs_status result = lookup(path, id, &kind, NULL);
    if (result == OPENRFSFS_STATUS_OK)
        result = kind == DATA_NS_FILE ? truncate_id(id, size) :
            OPENRFSFS_STATUS_IS_DIRECTORY;
    wipe(id, sizeof(id));
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_ftruncate(
    openrfsfs_handle handle, uint64_t size)
{
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        const enum openrfsfs_status result = physical->ftruncate != NULL ?
            physical->ftruncate(state->physical_handle, size) :
            physical->truncate(OPENRFSFS_VOLUME_DATA,
                state->credential_path, size);
        end_work();
        return result;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if ((state->access & OPENRFSFS_ACCESS_WRITE) == 0U) {
        end_work();
        return OPENRFSFS_STATUS_ACCESS;
    }
    enum openrfsfs_status result = truncate_id(state->stable_id, size);
    if (result == OPENRFSFS_STATUS_OK) result = refresh_reader(state);
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_remove_kind(
    enum openrfsfs_volume volume, const char *path,
    enum data_ns_kind wanted)
{
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (credential_file(path)) {
        if (wanted == DATA_NS_DIRECTORY)
            return OPENRFSFS_STATUS_NOT_DIRECTORY;
        return physical->unlink(volume, path);
    }
    if (same_text(path, "OPENRFS"))
        return OPENRFSFS_STATUS_ACCESS;
    if (!active) return OPENRFSFS_STATUS_ACCESS;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    const struct data_ns_entry *entry = NULL;
    enum openrfsfs_status result = lookup(path, id, &kind, &entry);
    if (result == OPENRFSFS_STATUS_OK && entry == NULL)
        result = OPENRFSFS_STATUS_ACCESS;
    if (result == OPENRFSFS_STATUS_OK && kind != wanted)
        result = wanted == DATA_NS_FILE ?
            OPENRFSFS_STATUS_IS_DIRECTORY :
            OPENRFSFS_STATUS_NOT_DIRECTORY;
    if (result == OPENRFSFS_STATUS_OK && kind == DATA_NS_DIRECTORY)
        for (size_t at = 0U; at < namespace_state.capacity; ++at)
            if (namespace_state.entries[at].active &&
                    crypto_verify16(namespace_state.entries[at].parent_id,
                        id) == 0) {
                result = OPENRFSFS_STATUS_NOT_EMPTY;
                break;
            }
    if (result == OPENRFSFS_STATUS_OK) {
        struct data_ns_event event;
        event_from_entry(entry, &event);
        event.operation = DATA_NS_DELETE;
        result = append_event(&event);
        wipe(&event, sizeof(event));
    }
    wipe(id, sizeof(id));
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_unlink(
    enum openrfsfs_volume volume, const char *path)
{
    return encrypted_remove_kind(volume, path, DATA_NS_FILE);
}

static enum openrfsfs_status encrypted_rmdir(
    enum openrfsfs_volume volume, const char *path)
{
    return encrypted_remove_kind(volume, path, DATA_NS_DIRECTORY);
}

static enum openrfsfs_status encrypted_remove(
    enum openrfsfs_volume volume, const char *path)
{
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (credential_file(path)) return physical->unlink(volume, path);
    if (same_text(path, "OPENRFS")) return OPENRFSFS_STATUS_ACCESS;
    if (!active) return OPENRFSFS_STATUS_ACCESS;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    const struct data_ns_entry *entry = NULL;
    enum openrfsfs_status result = lookup(path, id, &kind, &entry);
    if (result == OPENRFSFS_STATUS_OK && entry == NULL)
        result = OPENRFSFS_STATUS_ACCESS;
    if (result == OPENRFSFS_STATUS_OK && kind == DATA_NS_DIRECTORY)
        for (size_t at = 0U; at < namespace_state.capacity; ++at)
            if (namespace_state.entries[at].active &&
                    crypto_verify16(namespace_state.entries[at].parent_id,
                        id) == 0) {
                result = OPENRFSFS_STATUS_NOT_EMPTY;
                break;
            }
    if (result == OPENRFSFS_STATUS_OK) {
        struct data_ns_event event;
        event_from_entry(entry, &event);
        event.operation = DATA_NS_DELETE;
        result = append_event(&event);
        wipe(&event, sizeof(event));
    }
    wipe(id, sizeof(id));
    end_work();
    return result;
}

static enum openrfsfs_status rename_locked(const char *source,
    const char *destination, bool replace)
{
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    const struct data_ns_entry *entry = NULL;
    enum openrfsfs_status result = lookup(source, id, &kind, &entry);
    if (result != OPENRFSFS_STATUS_OK) goto done;
    if (entry == NULL) {
        result = OPENRFSFS_STATUS_ACCESS;
        goto done;
    }
    struct data_ns_event event;
    event_from_entry(entry, &event);
    result = parent_name(destination, event.target_parent_id,
        event.target_name);
    if (result == OPENRFSFS_STATUS_OK &&
            reserved_root_name(destination))
        result = OPENRFSFS_STATUS_ACCESS;
    if (result == OPENRFSFS_STATUS_OK) {
        event.operation = replace ? DATA_NS_RENAME_REPLACE :
            DATA_NS_RENAME;
        result = append_event(&event);
    }
    wipe(&event, sizeof(event));
done:
    wipe(id, sizeof(id));
    return result;
}

static enum openrfsfs_status encrypted_rename_common(
    enum openrfsfs_volume volume, const char *source,
    const char *destination, bool replace)
{
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (credential_file(source) && credential_file(destination))
        return replace && physical->rename_replace != NULL ?
            physical->rename_replace(volume, source, destination) :
            physical->rename(volume, source, destination);
    if (credential_path(source) || credential_path(destination))
        return OPENRFSFS_STATUS_ACCESS;
    if (!active) return OPENRFSFS_STATUS_ACCESS;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    const enum openrfsfs_status result = rename_locked(source,
        destination, replace);
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_rename(
    enum openrfsfs_volume volume, const char *source,
    const char *destination)
{
    return encrypted_rename_common(volume, source, destination, false);
}

static enum openrfsfs_status encrypted_rename_replace(
    enum openrfsfs_volume volume, const char *source,
    const char *destination)
{
    return encrypted_rename_common(volume, source, destination, true);
}

static enum openrfsfs_status encrypted_publish_file(
    openrfsfs_handle handle, const char *source,
    const char *destination)
{
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        const enum openrfsfs_status result = physical->publish_file != NULL ?
            physical->publish_file(state->physical_handle, source,
                destination) : OPENRFSFS_STATUS_ACCESS;
        end_work();
        return result;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    enum openrfsfs_status result = lookup(source, id, &kind, NULL);
    if (result == OPENRFSFS_STATUS_OK &&
            (kind != DATA_NS_FILE ||
             crypto_verify16(id, state->stable_id) != 0))
        result = OPENRFSFS_STATUS_STALE_HANDLE;
    if (result == OPENRFSFS_STATUS_OK)
        result = rename_locked(source, destination, true);
    wipe(id, sizeof(id));
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_unlink_held_file(
    openrfsfs_handle handle, const char *path)
{
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        const enum openrfsfs_status result =
            physical->unlink_held_file != NULL ?
            physical->unlink_held_file(state->physical_handle,
                path) : OPENRFSFS_STATUS_ACCESS;
        end_work();
        return result;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    const struct data_ns_entry *entry = NULL;
    enum openrfsfs_status result = lookup(path, id, &kind, &entry);
    if (result == OPENRFSFS_STATUS_OK &&
            (kind != DATA_NS_FILE ||
             crypto_verify16(id, state->stable_id) != 0))
        result = OPENRFSFS_STATUS_STALE_HANDLE;
    if (result == OPENRFSFS_STATUS_OK) {
        struct data_ns_event event;
        event_from_entry(entry, &event);
        event.operation = DATA_NS_DELETE;
        result = append_event(&event);
        wipe(&event, sizeof(event));
    }
    wipe(id, sizeof(id));
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_directory_open_with_stat(
    enum openrfsfs_volume volume, const char *path,
    openrfsfs_handle *handle, struct openrfsfs_stat *stat)
{
    if (handle == NULL || stat == NULL)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    *handle = 0U;
    wipe(stat, sizeof(*stat));
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (!active && !same_text(path, "OPENRFS"))
        return OPENRFSFS_STATUS_ACCESS;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    const struct data_ns_entry *entry = NULL;
    enum openrfsfs_status result = OPENRFSFS_STATUS_OK;
    if (same_text(path, "OPENRFS")) {
        result = physical->stat_path(volume, path, stat);
        wipe(id, sizeof(id));
    } else {
        result = lookup(path, id, &kind, &entry);
        if (result == OPENRFSFS_STATUS_OK && kind != DATA_NS_DIRECTORY)
            result = OPENRFSFS_STATUS_NOT_DIRECTORY;
        if (result == OPENRFSFS_STATUS_OK)
            fill_stat(entry, id, 0U, stat);
    }
    if (result == OPENRFSFS_STATUS_OK) {
        size_t index = 0U;
        struct encrypted_handle *state = claim_handle(&index);
        if (state == NULL) result = OPENRFSFS_STATUS_NO_HANDLES;
        else {
            state->directory = true;
            state->credential = same_text(path, "OPENRFS");
            state->session_epoch = session_epoch;
            state->stat_snapshot = *stat;
            copy_bytes(state->stable_id, id, sizeof(id));
            *handle = public_handle(index, state);
        }
    }
    wipe(id, sizeof(id));
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_directory_open(
    enum openrfsfs_volume volume, const char *path,
    openrfsfs_handle *handle)
{
    struct openrfsfs_stat stat;
    return encrypted_directory_open_with_stat(volume, path,
        handle, &stat);
}

static enum openrfsfs_status encrypted_directory_read(
    openrfsfs_handle handle, struct openrfsfs_list_entry *output,
    bool *present)
{
    if (output == NULL || present == NULL)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    wipe(output, sizeof(*output));
    *present = false;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL || !state->directory) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        end_work();
        return OPENRFSFS_STATUS_OK;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    enum openrfsfs_status result = OPENRFSFS_STATUS_OK;
    while (state->directory_cursor < namespace_state.capacity) {
        const struct data_ns_entry *entry =
            &namespace_state.entries[state->directory_cursor++];
        if (!entry->active ||
                crypto_verify16(entry->parent_id,
                    state->stable_id) != 0) continue;
        uint64_t size = 0U;
        if (entry->kind == DATA_NS_FILE)
            result = file_size(entry->child_id, &size);
        if (result != OPENRFSFS_STATUS_OK) break;
        display_name(output->name, entry->name);
        output->size = size;
        output->object_id = object_id(entry->child_id);
        output->mode = (entry->kind == DATA_NS_FILE ?
            0100000U : 0040000U) | entry->mode;
        output->attributes = entry->attributes;
        output->directory = entry->kind == DATA_NS_DIRECTORY;
        *present = true;
        break;
    }
    if (result != OPENRFSFS_STATUS_OK) {
        wipe(output, sizeof(*output));
        *present = false;
    }
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_directory_close(
    openrfsfs_handle handle)
{
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL || !state->directory) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    wipe(state, sizeof(*state));
    end_work();
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status encrypted_chmod(
    enum openrfsfs_volume volume, const char *path, uint16_t mode)
{
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (credential_path(path))
        return physical->chmod != NULL ?
            physical->chmod(volume, path, mode) :
            OPENRFSFS_STATUS_ACCESS;
    if (!active) return OPENRFSFS_STATUS_ACCESS;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    const struct data_ns_entry *entry = NULL;
    enum openrfsfs_status result = lookup(path, id, &kind, &entry);
    if (result == OPENRFSFS_STATUS_OK && entry == NULL)
        result = OPENRFSFS_STATUS_ACCESS;
    if (result == OPENRFSFS_STATUS_OK) {
        struct data_ns_event event;
        event_from_entry(entry, &event);
        event.operation = DATA_NS_SET_METADATA;
        event.mode = mode & 07777U;
        result = append_event(&event);
        wipe(&event, sizeof(event));
    }
    wipe(id, sizeof(id));
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_set_times(
    enum openrfsfs_volume volume, const char *path,
    const struct openrfsfs_times *times)
{
    if (times == NULL) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (volume != OPENRFSFS_VOLUME_DATA || physical == NULL)
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    if (credential_path(path))
        return physical->set_times != NULL ?
            physical->set_times(volume, path, times) :
            OPENRFSFS_STATUS_ACCESS;
    if (!active) return OPENRFSFS_STATUS_ACCESS;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    uint8_t id[DATA_AEAD_ID_BYTES];
    enum data_ns_kind kind = 0;
    const struct data_ns_entry *entry = NULL;
    enum openrfsfs_status result = lookup(path, id, &kind, &entry);
    if (result == OPENRFSFS_STATUS_OK && entry == NULL)
        result = OPENRFSFS_STATUS_ACCESS;
    if (result == OPENRFSFS_STATUS_OK) {
        struct data_ns_event event;
        event_from_entry(entry, &event);
        event.operation = DATA_NS_SET_METADATA;
        event.atime_seconds = times->atime_seconds;
        event.atime_nanos = times->atime_nanos;
        event.mtime_seconds = times->mtime_seconds;
        event.mtime_nanos = times->mtime_nanos;
        result = append_event(&event);
        wipe(&event, sizeof(event));
    }
    wipe(id, sizeof(id));
    end_work();
    return result;
}

static enum openrfsfs_status encrypted_fsync(openrfsfs_handle handle)
{
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    struct encrypted_handle *state = handle_state(handle);
    if (state == NULL) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (state->credential) {
        const enum openrfsfs_status result = physical->fsync != NULL ?
            physical->fsync(state->physical_handle) :
            physical->sync(OPENRFSFS_VOLUME_DATA);
        end_work();
        return result;
    }
    if (!active || state->session_epoch != session_epoch) {
        end_work();
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    const enum openrfsfs_status result =
        physical->sync(OPENRFSFS_VOLUME_DATA);
    end_work();
    return result;
}

static void clear_state_locked(void)
{
    active = false;
    ++session_epoch;
    for (size_t at = 0U; at < DATA_ENCRYPTED_HANDLES; ++at) {
        if (handles[at].active && physical != NULL) {
            if (handles[at].reader.active)
                (void)data_aead_backend_reader_close(&handles[at].reader);
            if (handles[at].credential &&
                    handles[at].physical_handle != 0U)
                (void)physical->close(handles[at].physical_handle);
        }
        wipe(&handles[at], sizeof(handles[at]));
    }
    wipe(data_key, sizeof(data_key));
    wipe(workspace, sizeof(workspace));
    wipe(entries, sizeof(entries));
    wipe(scratch, sizeof(scratch));
    wipe(&namespace_state, sizeof(namespace_state));
    namespace_generation = 0U;
}

void data_encrypted_backend_deactivate(void)
{
    while (!begin_work()) __asm__ volatile("pause");
    clear_state_locked();
    end_work();
}

void data_encrypted_backend_bind(const struct vfs_backend_ops *backend,
    void (*forget_key)(void))
{
    data_encrypted_backend_deactivate();
    physical = backend;
    forget_session_key = forget_key;
    encrypted_ops.case_sensitive = backend != NULL &&
        backend->case_sensitive;
}

enum openrfsfs_status data_encrypted_backend_activate(
    const uint8_t key[DATA_AEAD_KEY_BYTES])
{
    if (key == NULL || physical == NULL)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (!begin_work()) return OPENRFSFS_STATUS_BUSY;
    clear_state_locked();
    copy_bytes(data_key, key, sizeof(data_key));
    namespace_state.entries = entries;
    namespace_state.capacity = DATA_ENCRYPTED_ENTRIES;
    namespace_state.case_sensitive = physical->case_sensitive;
    const enum openrfsfs_status result = map_ns(data_ns_backend_load(
        physical, OPENRFSFS_VOLUME_DATA, data_key, workspace,
        sizeof(workspace), &namespace_state,
        &namespace_generation));
    if (result == OPENRFSFS_STATUS_OK) active = true;
    else clear_state_locked();
    end_work();
    return result;
}

enum openrfsfs_status data_encrypted_backend_migration_preflight(
    const uint8_t key[DATA_AEAD_KEY_BYTES])
{
    return physical == NULL ? OPENRFSFS_STATUS_NOT_MOUNTED :
        data_encrypted_migration_preflight(physical, key);
}

enum openrfsfs_status data_encrypted_backend_migrate(
    const uint8_t key[DATA_AEAD_KEY_BYTES])
{
    return physical == NULL ? OPENRFSFS_STATUS_NOT_MOUNTED :
        data_encrypted_migration_run(physical, key);
}

const struct vfs_backend_ops *data_encrypted_backend_ops(void)
{
    return &encrypted_ops;
}

static struct vfs_backend_ops encrypted_ops = {
    .open_options = encrypted_open_options,
    .mount = encrypted_mount,
    .unmount = encrypted_unmount,
    .sync = encrypted_sync,
    .drive = encrypted_drive,
    .completion_count = encrypted_completion_count,
    .open = encrypted_open,
    .open_with_stat = encrypted_open_with_stat,
    .close = encrypted_close,
    .fsync = encrypted_fsync,
    .fstat = encrypted_fstat,
    .publish_file = encrypted_publish_file,
    .unlink_held_file = encrypted_unlink_held_file,
    .read = encrypted_read,
    .pread = encrypted_pread,
    .write = encrypted_write,
    .append = encrypted_append,
    .seek = encrypted_seek,
    .stat_path = encrypted_stat_path,
    .lstat_path = encrypted_stat_path,
    .list = encrypted_list,
    .directory_open = encrypted_directory_open,
    .directory_open_with_stat = encrypted_directory_open_with_stat,
    .directory_read = encrypted_directory_read,
    .directory_close = encrypted_directory_close,
    .create = encrypted_create,
    .truncate = encrypted_truncate,
    .ftruncate = encrypted_ftruncate,
    .mkdir = encrypted_mkdir,
    .mkdir_mode = encrypted_mkdir_mode,
    .rename = encrypted_rename,
    .rename_replace = encrypted_rename_replace,
    .unlink = encrypted_unlink,
    .rmdir = encrypted_rmdir,
    .remove = encrypted_remove,
    .chmod = encrypted_chmod,
    .set_times = encrypted_set_times,
    .validates_mutation_paths = true,
};
