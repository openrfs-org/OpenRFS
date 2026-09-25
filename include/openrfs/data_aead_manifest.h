/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_AEAD_MANIFEST_H
#define OPENRFS_DATA_AEAD_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead.h>
#include <openrfs/data_aead_rewrite.h>

#define DATA_AEAD_SEGMENT_BYTES UINT64_C(8388608)
#define DATA_AEAD_SEGMENTS_MAX 8U
#define DATA_AEAD_MANIFEST_BYTES 344U

struct data_aead_segment {
    uint8_t revision_id[DATA_AEAD_ID_BYTES];
    uint64_t plaintext_bytes;
    uint64_t generation;
};

struct data_aead_manifest {
    uint8_t stable_id[DATA_AEAD_ID_BYTES];
    uint64_t plaintext_bytes;
    uint64_t generation;
    unsigned segment_count;
    struct data_aead_segment segments[DATA_AEAD_SEGMENTS_MAX];
};

struct data_aead_manifest_slot_io {
    void *context;
    bool (*read)(void *context, unsigned slot,
        uint8_t record[DATA_AEAD_MANIFEST_BYTES], bool *present);
};

/* Temp writes never touch a slot; temp_publish renames into an absent slot. */
struct data_aead_manifest_publish_io {
    void *context;
    bool (*slot_read)(void *context, unsigned slot,
        uint8_t record[DATA_AEAD_MANIFEST_BYTES], bool *present);
    bool (*slot_remove)(void *context, unsigned slot);
    bool (*slot_sync)(void *context, unsigned slot);
    bool (*temp_write)(void *context,
        const uint8_t record[DATA_AEAD_MANIFEST_BYTES]);
    bool (*temp_sync)(void *context);
    bool (*temp_read)(void *context,
        uint8_t record[DATA_AEAD_MANIFEST_BYTES]);
    bool (*temp_publish)(void *context, unsigned slot);
    bool (*segment_sync)(void *context,
        const uint8_t revision_id[DATA_AEAD_ID_BYTES]);
    bool (*segment_size)(void *context,
        const uint8_t revision_id[DATA_AEAD_ID_BYTES], uint64_t *size);
    bool (*segment_read)(void *context,
        const uint8_t revision_id[DATA_AEAD_ID_BYTES], uint64_t offset,
        uint8_t *to, size_t bytes);
    bool (*random)(void *context, uint8_t *to, size_t bytes);
};

enum data_aead_status data_aead_manifest_seal(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_manifest *manifest,
    bool (*random)(void *context, uint8_t *to, size_t bytes),
    void *context, uint8_t record[DATA_AEAD_MANIFEST_BYTES]);
enum data_aead_status data_aead_manifest_open(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const uint8_t record[DATA_AEAD_MANIFEST_BYTES],
    struct data_aead_manifest *manifest);
/* Selection authenticates metadata; verify every referenced segment before use. */
enum data_aead_status data_aead_manifest_select(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_manifest_slot_io *io,
    struct data_aead_manifest *manifest, unsigned *slot);
/* Temp and inactive slot names are distinct; each sync is a durable barrier.
 * Candidate, published output, and workspace do not overlap. */
enum data_aead_status data_aead_manifest_publish(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_manifest *candidate,
    const struct data_aead_manifest_publish_io *io,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *published, unsigned *slot);

/* A segment envelope is bound to this stable path, independent of renames. */
enum data_aead_status data_aead_segment_binding(
    const uint8_t stable_id[DATA_AEAD_ID_BYTES], unsigned index,
    char path[DATA_AEAD_PATH_MAX + 1U]);
enum data_aead_status data_aead_segment_storage_path(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const uint8_t revision_id[DATA_AEAD_ID_BYTES],
    char path[DATA_AEAD_PATH_MAX + 1U]);
/* slot 0/1 selects the committed record; slot 2 names the temporary record. */
enum data_aead_status data_aead_manifest_storage_path(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    unsigned slot, char path[DATA_AEAD_PATH_MAX + 1U]);
enum data_aead_status data_aead_manifest_check_segment(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const struct data_aead_manifest *manifest, unsigned index,
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t physical_bytes);
/* Keep the backend object stable for all callback reads. */
enum data_aead_status data_aead_manifest_verify_segment(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const struct data_aead_manifest *manifest, unsigned index,
    uint64_t physical_bytes,
    bool (*read_file)(void *context, uint64_t offset, uint8_t *to,
        size_t bytes), void *context, uint8_t *workspace,
    size_t workspace_bytes);

#endif
