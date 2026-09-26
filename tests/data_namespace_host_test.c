/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openrfs/data_namespace.h>

static bool check(bool condition, const char *message)
{
    if (!condition) fprintf(stderr, "Data namespace: %s\n", message);
    return condition;
}

static bool empty(const void *memory, size_t bytes)
{
    const uint8_t *from = memory;
    for (size_t at = 0U; at < bytes; ++at)
        if (from[at] != 0U) return false;
    return true;
}

static bool contains(const uint8_t *bytes, size_t length,
    const char *needle, size_t needle_bytes)
{
    for (size_t at = 0U; at + needle_bytes <= length; ++at)
        if (memcmp(bytes + at, needle, needle_bytes) == 0) return true;
    return false;
}

struct log_fixture {
    uint8_t records[3][DATA_NS_RECORD_BYTES];
    unsigned fail_index;
};

static bool read_log_record(void *context, uint64_t offset,
    uint8_t record[DATA_NS_RECORD_BYTES])
{
    const struct log_fixture *log = context;
    if (offset % DATA_NS_RECORD_BYTES != 0U) return false;
    const uint64_t index = offset / DATA_NS_RECORD_BYTES;
    if (index >= 3U || index == log->fail_index) return false;
    memcpy(record, log->records[index], DATA_NS_RECORD_BYTES);
    return true;
}

static enum data_ns_status roundtrip_apply(struct data_ns_state *state,
    const struct data_ns_event *input)
{
    uint8_t record[DATA_NS_RECORD_BYTES];
    struct data_ns_event opened;
    if (data_ns_event_encode(input, record) != DATA_NS_OK ||
            data_ns_event_decode(record, &opened) != DATA_NS_OK)
        return DATA_NS_FORMAT;
    return data_ns_apply(state, &opened);
}

int main(void)
{
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t wrong_key[DATA_AEAD_KEY_BYTES];
    uint8_t wrong_root[DATA_AEAD_ID_BYTES];
    uint8_t record[DATA_NS_RECORD_BYTES];
    struct data_ns_entry entries[4] = {0};
    struct data_ns_state state = {entries, 4U, 0U, {0}, false};
    struct data_ns_event event = {0};
    struct data_ns_event opened;
    for (size_t at = 0U; at < sizeof(key); ++at) {
        key[at] = (uint8_t)(at + 1U);
        wrong_key[at] = (uint8_t)(at + 2U);
    }
    if (!check(data_ns_root_id(key, state.root_id) == DATA_NS_OK &&
            data_ns_root_id(wrong_key, wrong_root) == DATA_NS_OK &&
            memcmp(state.root_id, wrong_root, sizeof(wrong_root)) != 0,
            "root identity is derived from the Data key")) return 1;
    event.operation = DATA_NS_CREATE;
    event.kind = DATA_NS_DIRECTORY;
    event.mode = 0700U;
    memcpy(event.parent_id, state.root_id, DATA_AEAD_ID_BYTES);
    event.child_id[0] = 1U;
    strcpy(event.name, "PRIVATE");
    if (!check(roundtrip_apply(&state, &event) == DATA_NS_OK &&
            state.count == 1U, "create private directory")) return 1;
    if (!check(data_ns_find(&state, state.root_id, "private") != NULL,
            "FAT32 lookup folds ASCII case")) return 1;
    struct data_ns_event duplicate = event;
    duplicate.child_id[0] = 2U;
    strcpy(duplicate.name, "private");
    if (!check(roundtrip_apply(&state, &duplicate) == DATA_NS_CONFLICT,
            "FAT32 case collision refuses")) return 1;
    struct data_ns_entry ext4_entries[2] = {0};
    struct data_ns_state ext4_state = {
        ext4_entries, 2U, 0U, {0}, true
    };
    memcpy(ext4_state.root_id, state.root_id, DATA_AEAD_ID_BYTES);
    if (!check(roundtrip_apply(&ext4_state, &event) == DATA_NS_OK &&
            roundtrip_apply(&ext4_state, &duplicate) == DATA_NS_OK &&
            data_ns_find(&ext4_state, state.root_id, "private") != NULL &&
            data_ns_find(&ext4_state, state.root_id, "PRIVATE") != NULL,
            "ext4 keeps distinct case")) return 1;
    strcpy(duplicate.name, "PRIVATE");
    duplicate = event;
    strcpy(duplicate.name, "OTHER");
    if (!check(roundtrip_apply(&state, &duplicate) == DATA_NS_CONFLICT,
            "duplicate identity refuses")) return 1;
    struct data_ns_event file = {0};
    file.operation = DATA_NS_CREATE;
    file.kind = DATA_NS_FILE;
    file.mode = 0600U;
    memcpy(file.parent_id, event.child_id, DATA_AEAD_ID_BYTES);
    file.child_id[0] = 3U;
    strcpy(file.name, "notes.txt");
    if (!check(roundtrip_apply(&state, &file) == DATA_NS_OK &&
            data_ns_find(&state, event.child_id, "notes.txt") != NULL,
            "file resolves under stable directory ID")) return 1;
    event.operation = DATA_NS_DELETE;
    if (!check(roundtrip_apply(&state, &event) == DATA_NS_CONFLICT,
            "nonempty directory cannot disappear")) return 1;
    event.operation = DATA_NS_RENAME;
    memcpy(event.target_parent_id, state.root_id, DATA_AEAD_ID_BYTES);
    strcpy(event.target_name, "RENAMED");
    if (!check(roundtrip_apply(&state, &event) == DATA_NS_OK &&
            data_ns_find(&state, state.root_id, "PRIVATE") == NULL &&
            data_ns_find(&state, state.root_id, "RENAMED") != NULL &&
            data_ns_find(&state, event.child_id, "notes.txt") != NULL,
            "directory rename keeps descendant identity")) return 1;
    uint8_t resolved[DATA_AEAD_ID_BYTES];
    enum data_ns_kind resolved_kind = 0;
    char file_binding[DATA_AEAD_PATH_MAX + 1U];
    if (!check(data_ns_resolve(&state, "renamed/NOTES.TXT",
            resolved, &resolved_kind) == DATA_NS_OK &&
            resolved_kind == DATA_NS_FILE &&
            memcmp(resolved, file.child_id, sizeof(resolved)) == 0 &&
            data_ns_file_binding(resolved, file_binding) == DATA_NS_OK &&
            strstr(file_binding, "notes") == NULL &&
            data_ns_resolve(&state, "PRIVATE/notes.txt",
                resolved, &resolved_kind) == DATA_NS_NOT_FOUND &&
            empty(resolved, sizeof(resolved)) && resolved_kind == 0,
            "renamed path resolves to a name-free file binding")) return 1;
    strcpy(event.name, "RENAMED");
    memcpy(event.parent_id, state.root_id, DATA_AEAD_ID_BYTES);
    memcpy(event.target_parent_id, event.child_id, DATA_AEAD_ID_BYTES);
    strcpy(event.target_name, "LOOP");
    if (!check(roundtrip_apply(&state, &event) == DATA_NS_CONFLICT,
            "directory cycle refuses")) return 1;
    file.operation = DATA_NS_SET_METADATA;
    file.mode = 0640U;
    file.attributes = 1U;
    file.uid = 1000U;
    file.gid = 1001U;
    file.mtime_seconds = 1790000000U;
    file.mtime_nanos = 123456789U;
    if (!check(roundtrip_apply(&state, &file) == DATA_NS_OK &&
            data_ns_find(&state, event.child_id, "notes.txt")->mode ==
                0640U &&
            data_ns_find(&state, event.child_id, "notes.txt")->uid ==
                1000U &&
            data_ns_find(&state, event.child_id, "notes.txt")->gid ==
                1001U &&
            data_ns_find(&state, event.child_id, "notes.txt")
                ->mtime_seconds == 1790000000U &&
            data_ns_find(&state, event.child_id, "notes.txt")
                ->mtime_nanos == 123456789U,
            "metadata update authenticates mode, owner and time"))
        return 1;
    file.operation = DATA_NS_RENAME;
    memcpy(file.target_parent_id, state.root_id, DATA_AEAD_ID_BYTES);
    strcpy(file.target_name, "new.txt");
    if (!check(roundtrip_apply(&state, &file) == DATA_NS_OK &&
            data_ns_find(&state, state.root_id, "new.txt") != NULL &&
            data_ns_find(&state, event.child_id, "notes.txt") == NULL,
            "cross-directory rename is one event")) return 1;
    file.operation = DATA_NS_DELETE;
    memcpy(file.parent_id, state.root_id, DATA_AEAD_ID_BYTES);
    strcpy(file.name, "new.txt");
    memset(file.target_parent_id, 0, DATA_AEAD_ID_BYTES);
    file.target_name[0] = '\0';
    if (!check(roundtrip_apply(&state, &file) == DATA_NS_OK &&
            state.count == 1U, "delete file")) return 1;
    event.operation = DATA_NS_DELETE;
    memset(event.target_parent_id, 0, DATA_AEAD_ID_BYTES);
    event.target_name[0] = '\0';
    if (!check(roundtrip_apply(&state, &event) == DATA_NS_OK &&
            state.count == 0U, "delete empty directory")) return 1;
    struct data_ns_event replace_source = {0};
    replace_source.operation = DATA_NS_CREATE;
    replace_source.kind = DATA_NS_FILE;
    replace_source.mode = 0600U;
    memcpy(replace_source.parent_id, state.root_id,
        DATA_AEAD_ID_BYTES);
    replace_source.child_id[0] = 21U;
    strcpy(replace_source.name, "SOURCE");
    struct data_ns_event replace_target = replace_source;
    replace_target.child_id[0] = 22U;
    strcpy(replace_target.name, "TARGET");
    if (!check(roundtrip_apply(&state, &replace_source) == DATA_NS_OK &&
            roundtrip_apply(&state, &replace_target) == DATA_NS_OK,
            "replacement fixture has two files")) return 1;
    replace_source.operation = DATA_NS_RENAME_REPLACE;
    memcpy(replace_source.target_parent_id, state.root_id,
        DATA_AEAD_ID_BYTES);
    strcpy(replace_source.target_name, "TARGET");
    if (!check(roundtrip_apply(&state, &replace_source) == DATA_NS_OK &&
            state.count == 1U &&
            data_ns_find(&state, state.root_id, "SOURCE") == NULL &&
            data_ns_find(&state, state.root_id, "TARGET") != NULL &&
            memcmp(data_ns_find(&state, state.root_id,
                "TARGET")->child_id, replace_source.child_id,
                DATA_AEAD_ID_BYTES) == 0,
            "replace rename publishes source identity in one event"))
        return 1;
    event.operation = DATA_NS_CREATE;
    strcpy(event.name, "../escape");
    if (!check(data_ns_event_encode(&event, record) == DATA_NS_ARGUMENT &&
            empty(record, sizeof(record)), "invalid name never serializes"))
        return 1;
    strcpy(event.name, "SAFE");
    if (!check(data_ns_event_encode(&event, record) == DATA_NS_OK,
            "valid record serializes")) return 1;
    uint8_t header[DATA_AEAD_HEADER_BYTES];
    uint8_t sealed[DATA_AEAD_SEALED_CHUNK_BYTES];
    uint8_t plaintext[DATA_AEAD_CHUNK_BYTES] = {0};
    uint8_t recovered[DATA_AEAD_CHUNK_BYTES] = {0};
    uint8_t nonce[DATA_AEAD_NONCE_BYTES] = {1U};
    uint8_t revision_id[DATA_AEAD_ID_BYTES] = {9U};
    size_t recovered_bytes = 0U;
    memcpy(plaintext, record, sizeof(record));
    if (!check(data_aead_make_header_v2(key, "NAMESPACE",
            sizeof(record), state.root_id, revision_id, 1U,
            header) == DATA_AEAD_OK &&
            data_aead_seal_chunk(key, header, 0U, nonce, plaintext,
                sizeof(record), sealed) == DATA_AEAD_OK &&
            !contains(header, sizeof(header), "SAFE", 4U) &&
            !contains(sealed, sizeof(sealed), "SAFE", 4U) &&
            data_aead_open_chunk(key, header, 0U, sealed, recovered,
                &recovered_bytes) == DATA_AEAD_OK &&
            recovered_bytes == sizeof(record) &&
            memcmp(recovered, record, sizeof(record)) == 0,
            "namespace names stay inside authenticated ciphertext"))
        return 1;
    sealed[DATA_AEAD_SEALED_CHUNK_BYTES - 1U] ^= 1U;
    if (!check(data_aead_open_chunk(key, header, 0U, sealed, recovered,
            &recovered_bytes) == DATA_AEAD_AUTHENTICATION,
            "tampered namespace ciphertext refuses")) return 1;
    record[66U + 5U] = 'X';
    memset(&opened, 0xa5, sizeof(opened));
    if (!check(data_ns_event_decode(record, &opened) == DATA_NS_FORMAT &&
            empty(&opened, sizeof(opened)),
            "noncanonical record padding refuses")) return 1;
    struct log_fixture log = {{{0}}, 3U};
    memset(&event, 0, sizeof(event));
    event.operation = DATA_NS_CREATE;
    event.kind = DATA_NS_DIRECTORY;
    event.mode = 0700U;
    memcpy(event.parent_id, state.root_id, DATA_AEAD_ID_BYTES);
    event.child_id[0] = 1U;
    strcpy(event.name, "SAFE");
    if (!check(data_ns_event_encode(&event, log.records[0]) == DATA_NS_OK,
            "log directory record")) return 1;
    struct data_ns_event log_file = {0};
    log_file.operation = DATA_NS_CREATE;
    log_file.kind = DATA_NS_FILE;
    log_file.mode = 0600U;
    memcpy(log_file.parent_id, event.child_id, DATA_AEAD_ID_BYTES);
    log_file.child_id[0] = 2U;
    strcpy(log_file.name, "NAME.SEC");
    if (!check(data_ns_event_encode(&log_file, log.records[1]) ==
            DATA_NS_OK, "log file record")) return 1;
    event.operation = DATA_NS_RENAME;
    memcpy(event.target_parent_id, state.root_id, DATA_AEAD_ID_BYTES);
    strcpy(event.target_name, "HIDDEN");
    if (!check(data_ns_event_encode(&event, log.records[2]) == DATA_NS_OK,
            "log rename record") ||
        !check(data_ns_replay(&state, sizeof(log.records), read_log_record,
            &log) == DATA_NS_OK && state.count == 2U &&
            data_ns_find(&state, state.root_id, "HIDDEN") != NULL &&
            data_ns_find(&state, event.child_id, "NAME.SEC") != NULL,
            "complete log replay preserves descendants")) return 1;
    log.fail_index = 1U;
    if (!check(data_ns_replay(&state, sizeof(log.records), read_log_record,
            &log) == DATA_NS_IO && state.count == 0U &&
            empty(entries, sizeof(entries)),
            "read cut clears partial namespace")) return 1;
    log.fail_index = 3U;
    if (!check(data_ns_replay(&state, sizeof(log.records) - 1U,
            read_log_record, &log) == DATA_NS_FORMAT &&
            state.count == 0U, "truncated log refuses")) return 1;
    log.records[1][620U] = 1U;
    if (!check(data_ns_replay(&state, sizeof(log.records),
            read_log_record, &log) == DATA_NS_FORMAT &&
            state.count == 0U && empty(entries, sizeof(entries)),
            "malformed event clears partial namespace")) return 1;
    puts("Data namespace identity, hidden-name record, replay, rename, metadata and malformed-input controls passed");
    return 0;
}
