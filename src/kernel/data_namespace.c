/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_namespace.h>
#include <openrfs/data_aead_manifest.h>

#include "../../vendor/monocypher/src/monocypher.h"

static const uint8_t root_label[] = "OpenRFS/v1/data/namespace-root";
static const uint8_t record_magic[4] = {'D', 'N', 'S', '1'};

static void zero_bytes(void *memory, size_t bytes)
{
    volatile uint8_t *to = memory;
    for (size_t at = 0U; at < bytes; ++at) to[at] = 0U;
}

static void copy_bytes(void *to, const void *from, size_t bytes)
{
    uint8_t *destination = to;
    const uint8_t *source = from;
    for (size_t at = 0U; at < bytes; ++at)
        destination[at] = source[at];
}

static bool zero_id(const uint8_t *id)
{
    uint8_t any = 0U;
    for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at)
        any |= id[at];
    return any == 0U;
}

static void put_u32(uint8_t *to, uint32_t value)
{
    for (size_t at = 0U; at < 4U; ++at)
        to[at] = (uint8_t)(value >> (8U * at));
}

static void put_u64(uint8_t *to, uint64_t value)
{
    for (size_t at = 0U; at < 8U; ++at)
        to[at] = (uint8_t)(value >> (8U * at));
}

static uint32_t get_u32(const uint8_t *from)
{
    uint32_t value = 0U;
    for (size_t at = 0U; at < 4U; ++at)
        value |= (uint32_t)from[at] << (8U * at);
    return value;
}

static uint64_t get_u64(const uint8_t *from)
{
    uint64_t value = 0U;
    for (size_t at = 0U; at < 8U; ++at)
        value |= (uint64_t)from[at] << (8U * at);
    return value;
}

static bool equal_id(const uint8_t *left, const uint8_t *right)
{
    return crypto_verify16(left, right) == 0;
}

static size_t name_length(const char *name)
{
    if (name == NULL) return 0U;
    size_t length = 0U;
    while (length <= DATA_NS_NAME_BYTES && name[length] != '\0') {
        if ((uint8_t)name[length] < 0x20U || name[length] == '/' ||
                name[length] == '\\') return 0U;
        ++length;
    }
    if (length == 0U || length > DATA_NS_NAME_BYTES ||
            (length == 1U && name[0] == '.') ||
            (length == 2U && name[0] == '.' && name[1] == '.'))
        return 0U;
    return length;
}

static uint8_t folded_ascii(uint8_t value, bool case_sensitive)
{
    return !case_sensitive && value >= 'a' && value <= 'z' ?
        (uint8_t)(value - 'a' + 'A') : value;
}

static bool equal_name(const char *left, const char *right,
    bool case_sensitive)
{
    size_t at = 0U;
    while (at <= DATA_NS_NAME_BYTES &&
            folded_ascii((uint8_t)left[at], case_sensitive) ==
                folded_ascii((uint8_t)right[at], case_sensitive)) {
        if (left[at] == '\0') return true;
        ++at;
    }
    return false;
}

static bool event_valid(const struct data_ns_event *event)
{
    if (event == NULL || zero_id(event->parent_id) ||
            zero_id(event->child_id) ||
            name_length(event->name) == 0U ||
            (event->kind != DATA_NS_FILE &&
             event->kind != DATA_NS_DIRECTORY) ||
            (event->mode & ~07777U) != 0U ||
            event->atime_nanos >= 1000000000U ||
            event->mtime_nanos >= 1000000000U ||
            event->ctime_nanos >= 1000000000U ||
            event->atime_seconds > INT64_MAX ||
            event->mtime_seconds > INT64_MAX ||
            event->ctime_seconds > INT64_MAX)
        return false;
    if (event->operation == DATA_NS_RENAME)
        return !zero_id(event->target_parent_id) &&
            name_length(event->target_name) != 0U;
    if (event->operation != DATA_NS_CREATE &&
            event->operation != DATA_NS_DELETE &&
            event->operation != DATA_NS_SET_METADATA)
        return false;
    return zero_id(event->target_parent_id) &&
        event->target_name[0] == '\0';
}

enum data_ns_status data_ns_root_id(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    uint8_t root_id[DATA_AEAD_ID_BYTES])
{
    if (root_id == NULL) return DATA_NS_ARGUMENT;
    zero_bytes(root_id, DATA_AEAD_ID_BYTES);
    if (key == NULL) return DATA_NS_ARGUMENT;
    crypto_blake2b_keyed(root_id, DATA_AEAD_ID_BYTES, key,
        DATA_AEAD_KEY_BYTES, root_label, sizeof(root_label) - 1U);
    return zero_id(root_id) ? DATA_NS_FORMAT : DATA_NS_OK;
}

enum data_ns_status data_ns_event_encode(const struct data_ns_event *event,
    uint8_t record[DATA_NS_RECORD_BYTES])
{
    if (record == NULL) return DATA_NS_ARGUMENT;
    zero_bytes(record, DATA_NS_RECORD_BYTES);
    if (!event_valid(event)) return DATA_NS_ARGUMENT;
    const size_t name_bytes = name_length(event->name);
    const size_t target_bytes = event->operation == DATA_NS_RENAME ?
        name_length(event->target_name) : 0U;
    copy_bytes(record, record_magic, sizeof(record_magic));
    record[4] = (uint8_t)event->operation;
    record[5] = (uint8_t)event->kind;
    record[6] = (uint8_t)event->mode;
    record[7] = (uint8_t)(event->mode >> 8U);
    record[8] = event->attributes;
    copy_bytes(record + 16U, event->parent_id, DATA_AEAD_ID_BYTES);
    copy_bytes(record + 32U, event->child_id, DATA_AEAD_ID_BYTES);
    copy_bytes(record + 48U, event->target_parent_id,
        DATA_AEAD_ID_BYTES);
    record[64] = (uint8_t)name_bytes;
    record[65] = (uint8_t)target_bytes;
    copy_bytes(record + 66U, event->name, name_bytes);
    copy_bytes(record + 321U, event->target_name, target_bytes);
    put_u32(record + 576U, event->uid);
    put_u32(record + 580U, event->gid);
    put_u64(record + 584U, event->atime_seconds);
    put_u32(record + 592U, event->atime_nanos);
    put_u64(record + 596U, event->mtime_seconds);
    put_u32(record + 604U, event->mtime_nanos);
    put_u64(record + 608U, event->ctime_seconds);
    put_u32(record + 616U, event->ctime_nanos);
    return DATA_NS_OK;
}

enum data_ns_status data_ns_event_decode(
    const uint8_t record[DATA_NS_RECORD_BYTES],
    struct data_ns_event *event)
{
    if (event != NULL) zero_bytes(event, sizeof(*event));
    if (record == NULL || event == NULL) return DATA_NS_ARGUMENT;
    for (size_t at = 0U; at < sizeof(record_magic); ++at)
        if (record[at] != record_magic[at]) return DATA_NS_FORMAT;
    for (size_t at = 9U; at < 16U; ++at)
        if (record[at] != 0U) return DATA_NS_FORMAT;
    const size_t name_bytes = record[64];
    const size_t target_bytes = record[65];
    if (name_bytes == 0U) return DATA_NS_FORMAT;
    for (size_t at = name_bytes; at < DATA_NS_NAME_BYTES; ++at)
        if (record[66U + at] != 0U) return DATA_NS_FORMAT;
    for (size_t at = target_bytes; at < DATA_NS_NAME_BYTES; ++at)
        if (record[321U + at] != 0U) return DATA_NS_FORMAT;
    for (size_t at = 620U; at < DATA_NS_RECORD_BYTES; ++at)
        if (record[at] != 0U) return DATA_NS_FORMAT;
    event->operation = (enum data_ns_operation)record[4];
    event->kind = (enum data_ns_kind)record[5];
    event->mode = (uint16_t)record[6] |
        (uint16_t)((uint16_t)record[7] << 8U);
    event->attributes = record[8];
    copy_bytes(event->parent_id, record + 16U, DATA_AEAD_ID_BYTES);
    copy_bytes(event->child_id, record + 32U, DATA_AEAD_ID_BYTES);
    copy_bytes(event->target_parent_id, record + 48U,
        DATA_AEAD_ID_BYTES);
    copy_bytes(event->name, record + 66U, name_bytes);
    copy_bytes(event->target_name, record + 321U, target_bytes);
    event->uid = get_u32(record + 576U);
    event->gid = get_u32(record + 580U);
    event->atime_seconds = get_u64(record + 584U);
    event->atime_nanos = get_u32(record + 592U);
    event->mtime_seconds = get_u64(record + 596U);
    event->mtime_nanos = get_u32(record + 604U);
    event->ctime_seconds = get_u64(record + 608U);
    event->ctime_nanos = get_u32(record + 616U);
    if (!event_valid(event)) {
        zero_bytes(event, sizeof(*event));
        return DATA_NS_FORMAT;
    }
    return DATA_NS_OK;
}

static struct data_ns_entry *find_mutable(struct data_ns_state *state,
    const uint8_t parent_id[DATA_AEAD_ID_BYTES], const char *name)
{
    for (size_t at = 0U; at < state->capacity; ++at)
        if (state->entries[at].active &&
                equal_id(state->entries[at].parent_id, parent_id) &&
                equal_name(state->entries[at].name, name,
                    state->case_sensitive))
            return &state->entries[at];
    return NULL;
}

const struct data_ns_entry *data_ns_find(const struct data_ns_state *state,
    const uint8_t parent_id[DATA_AEAD_ID_BYTES], const char *name)
{
    if (state == NULL || state->entries == NULL || parent_id == NULL ||
            name_length(name) == 0U) return NULL;
    for (size_t at = 0U; at < state->capacity; ++at)
        if (state->entries[at].active &&
                equal_id(state->entries[at].parent_id, parent_id) &&
                equal_name(state->entries[at].name, name,
                    state->case_sensitive))
            return &state->entries[at];
    return NULL;
}

enum data_ns_status data_ns_resolve(const struct data_ns_state *state,
    const char *path, uint8_t child_id[DATA_AEAD_ID_BYTES],
    enum data_ns_kind *kind)
{
    if (child_id != NULL) zero_bytes(child_id, DATA_AEAD_ID_BYTES);
    if (kind != NULL) *kind = 0;
    if (state == NULL || state->entries == NULL ||
            zero_id(state->root_id) || path == NULL ||
            child_id == NULL || kind == NULL)
        return DATA_NS_ARGUMENT;
    if (path[0] == '.' && path[1] == '\0') {
        copy_bytes(child_id, state->root_id, DATA_AEAD_ID_BYTES);
        *kind = DATA_NS_DIRECTORY;
        return DATA_NS_OK;
    }
    uint8_t parent[DATA_AEAD_ID_BYTES];
    copy_bytes(parent, state->root_id, sizeof(parent));
    size_t offset = 0U;
    while (offset <= DATA_AEAD_PATH_MAX && path[offset] != '\0') {
        char name[DATA_NS_NAME_BYTES + 1U] = {0};
        size_t length = 0U;
        while (offset < DATA_AEAD_PATH_MAX &&
                path[offset] != '\0' && path[offset] != '/') {
            if (length == DATA_NS_NAME_BYTES) return DATA_NS_FORMAT;
            name[length++] = path[offset++];
        }
        if (name_length(name) != length) return DATA_NS_FORMAT;
        const struct data_ns_entry *entry = data_ns_find(state,
            parent, name);
        if (entry == NULL) return DATA_NS_NOT_FOUND;
        copy_bytes(parent, entry->child_id, sizeof(parent));
        if (path[offset] == '\0') {
            copy_bytes(child_id, parent, DATA_AEAD_ID_BYTES);
            *kind = entry->kind;
            return DATA_NS_OK;
        }
        if (entry->kind != DATA_NS_DIRECTORY) return DATA_NS_NOT_FOUND;
        ++offset;
    }
    return DATA_NS_FORMAT;
}

enum data_ns_status data_ns_file_binding(
    const uint8_t child_id[DATA_AEAD_ID_BYTES],
    char path[DATA_AEAD_PATH_MAX + 1U])
{
    static const char hex[] = "0123456789ABCDEF";
    if (path == NULL) return DATA_NS_ARGUMENT;
    zero_bytes(path, DATA_AEAD_PATH_MAX + 1U);
    if (child_id == NULL || zero_id(child_id)) return DATA_NS_ARGUMENT;
    copy_bytes(path, "FIL/", 4U);
    for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at) {
        path[4U + 2U * at] = hex[child_id[at] >> 4U];
        path[5U + 2U * at] = hex[child_id[at] & 15U];
    }
    return DATA_NS_OK;
}

static const struct data_ns_entry *find_by_id(
    const struct data_ns_state *state,
    const uint8_t child_id[DATA_AEAD_ID_BYTES])
{
    for (size_t at = 0U; at < state->capacity; ++at)
        if (state->entries[at].active &&
                equal_id(state->entries[at].child_id, child_id))
            return &state->entries[at];
    return NULL;
}

static bool parent_exists(const struct data_ns_state *state,
    const uint8_t parent_id[DATA_AEAD_ID_BYTES])
{
    if (equal_id(parent_id, state->root_id)) return true;
    const struct data_ns_entry *entry = find_by_id(state, parent_id);
    return entry != NULL && entry->kind == DATA_NS_DIRECTORY;
}

static bool causes_cycle(const struct data_ns_state *state,
    const uint8_t child_id[DATA_AEAD_ID_BYTES],
    const uint8_t target_parent_id[DATA_AEAD_ID_BYTES])
{
    const uint8_t *current = target_parent_id;
    for (size_t depth = 0U; depth <= state->capacity; ++depth) {
        if (equal_id(current, child_id)) return true;
        if (equal_id(current, state->root_id)) return false;
        const struct data_ns_entry *parent = find_by_id(state, current);
        if (parent == NULL || parent->kind != DATA_NS_DIRECTORY)
            return true;
        current = parent->parent_id;
    }
    return true;
}

static void apply_metadata(struct data_ns_entry *entry,
    const struct data_ns_event *event)
{
    entry->mode = event->mode;
    entry->attributes = event->attributes;
    entry->uid = event->uid;
    entry->gid = event->gid;
    entry->atime_seconds = event->atime_seconds;
    entry->mtime_seconds = event->mtime_seconds;
    entry->ctime_seconds = event->ctime_seconds;
    entry->atime_nanos = event->atime_nanos;
    entry->mtime_nanos = event->mtime_nanos;
    entry->ctime_nanos = event->ctime_nanos;
}

enum data_ns_status data_ns_apply(struct data_ns_state *state,
    const struct data_ns_event *event)
{
    if (state == NULL || state->entries == NULL ||
            state->capacity == 0U || state->count > state->capacity ||
            zero_id(state->root_id) || !event_valid(event))
        return DATA_NS_ARGUMENT;
    if (event->operation == DATA_NS_CREATE) {
        if (!parent_exists(state, event->parent_id) ||
                equal_id(event->child_id, state->root_id) ||
                find_mutable(state, event->parent_id,
                    event->name) != NULL ||
                find_by_id(state, event->child_id) != NULL)
            return DATA_NS_CONFLICT;
        if (state->count == state->capacity) return DATA_NS_FULL;
        for (size_t at = 0U; at < state->capacity; ++at)
            if (!state->entries[at].active) {
                struct data_ns_entry *entry = &state->entries[at];
                zero_bytes(entry, sizeof(*entry));
                copy_bytes(entry->parent_id, event->parent_id,
                    DATA_AEAD_ID_BYTES);
                copy_bytes(entry->child_id, event->child_id,
                    DATA_AEAD_ID_BYTES);
                copy_bytes(entry->name, event->name,
                    name_length(event->name) + 1U);
                apply_metadata(entry, event);
                entry->kind = event->kind;
                entry->active = true;
                ++state->count;
                return DATA_NS_OK;
            }
        return DATA_NS_FORMAT;
    }
    struct data_ns_entry *entry = find_mutable(state,
        event->parent_id, event->name);
    if (entry == NULL) return DATA_NS_NOT_FOUND;
    if (!equal_id(entry->child_id, event->child_id) ||
            entry->kind != event->kind) return DATA_NS_CONFLICT;
    if (event->operation == DATA_NS_DELETE) {
        if (entry->kind == DATA_NS_DIRECTORY)
            for (size_t at = 0U; at < state->capacity; ++at)
                if (state->entries[at].active &&
                        equal_id(state->entries[at].parent_id,
                            entry->child_id)) return DATA_NS_CONFLICT;
        zero_bytes(entry, sizeof(*entry));
        --state->count;
        return DATA_NS_OK;
    }
    if (event->operation == DATA_NS_SET_METADATA) {
        apply_metadata(entry, event);
        return DATA_NS_OK;
    }
    if (!parent_exists(state, event->target_parent_id) ||
            (entry->kind == DATA_NS_DIRECTORY &&
             causes_cycle(state, entry->child_id,
                event->target_parent_id)))
        return DATA_NS_CONFLICT;
    struct data_ns_entry *target = find_mutable(state,
        event->target_parent_id, event->target_name);
    if (target != NULL && target != entry) return DATA_NS_CONFLICT;
    copy_bytes(entry->parent_id, event->target_parent_id,
        DATA_AEAD_ID_BYTES);
    zero_bytes(entry->name, sizeof(entry->name));
    copy_bytes(entry->name, event->target_name,
        name_length(event->target_name) + 1U);
    apply_metadata(entry, event);
    return DATA_NS_OK;
}

enum data_ns_status data_ns_replay(struct data_ns_state *state,
    uint64_t file_bytes,
    bool (*read_record)(void *context, uint64_t offset,
        uint8_t record[DATA_NS_RECORD_BYTES]), void *context)
{
    if (state == NULL || state->entries == NULL ||
            state->capacity == 0U ||
            state->capacity > SIZE_MAX / sizeof(*state->entries) ||
            zero_id(state->root_id)) return DATA_NS_ARGUMENT;
    zero_bytes(state->entries,
        state->capacity * sizeof(*state->entries));
    state->count = 0U;
    if (read_record == NULL) return DATA_NS_ARGUMENT;
    if (file_bytes >
            DATA_AEAD_SEGMENT_BYTES * DATA_AEAD_SEGMENTS_MAX ||
            file_bytes % DATA_NS_RECORD_BYTES != 0U)
        return DATA_NS_FORMAT;
    uint8_t record[DATA_NS_RECORD_BYTES];
    struct data_ns_event event;
    enum data_ns_status result = DATA_NS_OK;
    for (uint64_t offset = 0U; offset < file_bytes;
            offset += DATA_NS_RECORD_BYTES) {
        zero_bytes(record, sizeof(record));
        if (!read_record(context, offset, record)) {
            result = DATA_NS_IO;
            break;
        }
        result = data_ns_event_decode(record, &event);
        if (result == DATA_NS_OK)
            result = data_ns_apply(state, &event);
        zero_bytes(&event, sizeof(event));
        if (result != DATA_NS_OK) break;
    }
    zero_bytes(record, sizeof(record));
    if (result != DATA_NS_OK) {
        zero_bytes(state->entries,
            state->capacity * sizeof(*state->entries));
        state->count = 0U;
    }
    return result;
}
