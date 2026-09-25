/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_NAMESPACE_H
#define OPENRFS_DATA_NAMESPACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead.h>

#define DATA_NS_RECORD_BYTES 624U
#define DATA_NS_NAME_BYTES 255U

enum data_ns_status {
    DATA_NS_OK = 0,
    DATA_NS_ARGUMENT,
    DATA_NS_FORMAT,
    DATA_NS_CONFLICT,
    DATA_NS_FULL,
    DATA_NS_NOT_FOUND,
    DATA_NS_IO
};

enum data_ns_operation {
    DATA_NS_CREATE = 1,
    DATA_NS_DELETE = 2,
    DATA_NS_RENAME = 3,
    DATA_NS_SET_METADATA = 4,
    DATA_NS_RENAME_REPLACE = 5
};

enum data_ns_kind {
    DATA_NS_FILE = 1,
    DATA_NS_DIRECTORY = 2
};

struct data_ns_event {
    uint8_t parent_id[DATA_AEAD_ID_BYTES];
    uint8_t child_id[DATA_AEAD_ID_BYTES];
    uint8_t target_parent_id[DATA_AEAD_ID_BYTES];
    char name[DATA_NS_NAME_BYTES + 1U];
    char target_name[DATA_NS_NAME_BYTES + 1U];
    uint16_t mode;
    uint8_t attributes;
    uint32_t uid;
    uint32_t gid;
    uint64_t atime_seconds;
    uint64_t mtime_seconds;
    uint64_t ctime_seconds;
    uint32_t atime_nanos;
    uint32_t mtime_nanos;
    uint32_t ctime_nanos;
    enum data_ns_operation operation;
    enum data_ns_kind kind;
};

struct data_ns_entry {
    uint8_t parent_id[DATA_AEAD_ID_BYTES];
    uint8_t child_id[DATA_AEAD_ID_BYTES];
    char name[DATA_NS_NAME_BYTES + 1U];
    uint16_t mode;
    uint8_t attributes;
    uint32_t uid;
    uint32_t gid;
    uint64_t atime_seconds;
    uint64_t mtime_seconds;
    uint64_t ctime_seconds;
    uint32_t atime_nanos;
    uint32_t mtime_nanos;
    uint32_t ctime_nanos;
    enum data_ns_kind kind;
    bool active;
};

struct data_ns_state {
    struct data_ns_entry *entries;
    size_t capacity;
    size_t count;
    uint8_t root_id[DATA_AEAD_ID_BYTES];
    bool case_sensitive;
};

enum data_ns_status data_ns_root_id(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    uint8_t root_id[DATA_AEAD_ID_BYTES]);
enum data_ns_status data_ns_event_encode(const struct data_ns_event *event,
    uint8_t record[DATA_NS_RECORD_BYTES]);
enum data_ns_status data_ns_event_decode(
    const uint8_t record[DATA_NS_RECORD_BYTES],
    struct data_ns_event *event);
enum data_ns_status data_ns_apply(struct data_ns_state *state,
    const struct data_ns_event *event);
/* Read records only from a fully authenticated namespace revision. */
enum data_ns_status data_ns_replay(struct data_ns_state *state,
    uint64_t file_bytes,
    bool (*read_record)(void *context, uint64_t offset,
        uint8_t record[DATA_NS_RECORD_BYTES]), void *context);
const struct data_ns_entry *data_ns_find(const struct data_ns_state *state,
    const uint8_t parent_id[DATA_AEAD_ID_BYTES], const char *name);
enum data_ns_status data_ns_resolve(const struct data_ns_state *state,
    const char *path, uint8_t child_id[DATA_AEAD_ID_BYTES],
    enum data_ns_kind *kind);
enum data_ns_status data_ns_file_binding(
    const uint8_t child_id[DATA_AEAD_ID_BYTES],
    char path[DATA_AEAD_PATH_MAX + 1U]);

#endif
