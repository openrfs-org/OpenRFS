/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_AEAD_SLOTS_H
#define OPENRFS_DATA_AEAD_SLOTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead_rewrite.h>

#define DATA_AEAD_INDEX_BYTES 96U

struct data_aead_slot_io {
    void *context;
    bool (*index_read)(void *context, unsigned slot,
        uint8_t record[DATA_AEAD_INDEX_BYTES], bool *present);
    bool (*index_write)(void *context, unsigned slot,
        const uint8_t record[DATA_AEAD_INDEX_BYTES]);
    bool (*index_sync)(void *context, unsigned slot);
    bool (*file_size)(void *context, unsigned slot, uint64_t *size,
        bool *present);
    bool (*file_read)(void *context, unsigned slot, uint64_t offset,
        uint8_t *output, size_t bytes);
    bool (*file_sync)(void *context, unsigned slot);
    bool (*source_size)(void *context, uint64_t *size, bool *present);
    bool (*source_remove)(void *context);
    bool (*source_sync)(void *context);
};

struct data_aead_selection {
    uint8_t stable_id[DATA_AEAD_ID_BYTES];
    uint64_t generation;
    uint64_t physical_bytes;
    uint64_t plaintext_bytes;
    unsigned file_slot;
    unsigned index_slot;
};

/* The caller keeps the source and both slots stable under one exclusive file
 * lease through publication. A successful sync callback is a durable barrier. */
enum data_aead_status data_aead_select(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_slot_io *io, uint8_t *workspace,
    size_t workspace_bytes, struct data_aead_selection *selection);

enum data_aead_status data_aead_publish(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_slot_io *io, unsigned candidate_slot,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_selection *selection);

enum data_aead_status data_aead_migrate_one(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_slot_io *io,
    const struct data_aead_rewrite_io *rewrite_io,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_selection *selection);

#endif
