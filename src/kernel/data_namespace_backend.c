/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_namespace_backend.h>

#include "../../vendor/monocypher/src/monocypher.h"

static const char namespace_binding[] = "NAMESPACE";

static enum data_ns_status map_status(enum data_aead_status status)
{
    if (status == DATA_AEAD_OK) return DATA_NS_OK;
    if (status == DATA_AEAD_NOT_FOUND) return DATA_NS_NOT_FOUND;
    if (status == DATA_AEAD_CONFLICT) return DATA_NS_CONFLICT;
    if (status == DATA_AEAD_ARGUMENT) return DATA_NS_ARGUMENT;
    if (status == DATA_AEAD_RANGE) return DATA_NS_FULL;
    if (status == DATA_AEAD_IO) return DATA_NS_IO;
    return DATA_NS_FORMAT;
}

static bool same_id(const uint8_t *left, const uint8_t *right)
{
    return crypto_verify16(left, right) == 0;
}

struct replay_reader {
    const struct data_aead_backend_reader *reader;
    const uint8_t *key;
    uint8_t *workspace;
    size_t workspace_bytes;
};

static bool replay_record(void *opaque, uint64_t offset,
    uint8_t record[DATA_NS_RECORD_BYTES])
{
    struct replay_reader *context = opaque;
    size_t got = 0U;
    return data_aead_backend_reader_read(context->reader, context->key,
        offset, record, DATA_NS_RECORD_BYTES, context->workspace,
        context->workspace_bytes, &got) == DATA_AEAD_OK &&
        got == DATA_NS_RECORD_BYTES;
}

enum data_ns_status data_ns_backend_create_empty(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], uint8_t *workspace,
    size_t workspace_bytes, uint64_t *generation)
{
    if (generation != NULL) *generation = 0U;
    if (generation == NULL || key == NULL) return DATA_NS_ARGUMENT;
    struct data_aead_manifest candidate = {0};
    struct data_aead_manifest published = {0};
    enum data_ns_status result = data_ns_root_id(key,
        candidate.stable_id);
    if (result != DATA_NS_OK) return result;
    candidate.generation = 1U;
    unsigned slot = 0U;
    result = map_status(data_aead_backend_publish_manifest(backend,
        volume, key, namespace_binding, &candidate, workspace,
        workspace_bytes, &published, &slot));
    if (result == DATA_NS_OK) *generation = published.generation;
    crypto_wipe(&candidate, sizeof(candidate));
    crypto_wipe(&published, sizeof(published));
    return result;
}

enum data_ns_status data_ns_backend_load(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], uint8_t *workspace,
    size_t workspace_bytes, struct data_ns_state *state,
    uint64_t *generation)
{
    if (generation != NULL) *generation = 0U;
    if (state == NULL || state->entries == NULL || state->capacity == 0U ||
            state->capacity > SIZE_MAX / sizeof(*state->entries) ||
            generation == NULL || key == NULL)
        return DATA_NS_ARGUMENT;
    crypto_wipe(state->entries,
        state->capacity * sizeof(*state->entries));
    state->count = 0U;
    enum data_ns_status result = data_ns_root_id(key, state->root_id);
    if (result != DATA_NS_OK) return result;
    struct data_aead_backend_reader reader = {0};
    result = map_status(data_aead_backend_reader_open(backend, volume, key,
        namespace_binding, workspace, workspace_bytes, &reader));
    if (result != DATA_NS_OK) return result;
    if (!same_id(reader.manifest.stable_id, state->root_id))
        result = DATA_NS_FORMAT;
    if (result == DATA_NS_OK) {
        struct replay_reader context = {
            &reader, key, workspace, workspace_bytes
        };
        result = data_ns_replay(state, reader.manifest.plaintext_bytes,
            replay_record, &context);
    }
    if (result == DATA_NS_OK)
        *generation = reader.manifest.generation;
    if (data_aead_backend_reader_close(&reader) != DATA_AEAD_OK)
        result = DATA_NS_IO;
    if (result != DATA_NS_OK) {
        crypto_wipe(state->entries,
            state->capacity * sizeof(*state->entries));
        state->count = 0U;
        *generation = 0U;
    }
    return result;
}

enum data_ns_status data_ns_backend_append(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], uint8_t *workspace,
    size_t workspace_bytes, struct data_ns_state *state,
    struct data_ns_entry *scratch_entries, uint64_t expected_generation,
    const struct data_ns_event *event, uint64_t *generation)
{
    if (generation != NULL) *generation = 0U;
    if (state == NULL || state->entries == NULL ||
            state->capacity == 0U ||
            state->capacity > SIZE_MAX / sizeof(*state->entries) ||
            scratch_entries == NULL || scratch_entries == state->entries ||
            event == NULL || generation == NULL || key == NULL)
        return DATA_NS_ARGUMENT;
    struct data_ns_state preview = *state;
    preview.entries = scratch_entries;
    for (size_t at = 0U; at < state->capacity; ++at)
        scratch_entries[at] = state->entries[at];
    enum data_ns_status result = data_ns_apply(&preview, event);
    uint8_t record[DATA_NS_RECORD_BYTES] = {0};
    if (result == DATA_NS_OK)
        result = data_ns_event_encode(event, record);
    struct data_aead_manifest published = {0};
    if (result == DATA_NS_OK)
        result = map_status(data_aead_backend_append_file(backend,
            volume, key, namespace_binding, expected_generation,
            record, sizeof(record), workspace, workspace_bytes,
            &published));
    if (result == DATA_NS_OK) {
        for (size_t at = 0U; at < state->capacity; ++at)
            state->entries[at] = scratch_entries[at];
        state->count = preview.count;
        *generation = published.generation;
    }
    crypto_wipe(record, sizeof(record));
    crypto_wipe(scratch_entries,
        state->capacity * sizeof(*scratch_entries));
    crypto_wipe(&published, sizeof(published));
    return result;
}
