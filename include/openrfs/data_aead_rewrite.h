/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_AEAD_REWRITE_H
#define OPENRFS_DATA_AEAD_REWRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead.h>

#define DATA_AEAD_REWRITE_WORKSPACE_BYTES \
    (2U * DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES + \
     DATA_AEAD_CHUNK_BYTES)

struct data_aead_rewrite_io {
    void *context;
    /* These callbacks transfer the full requested span or refuse. The old
     * source and new shadow must be distinct objects. The caller must discard
     * a partial shadow on any non-OK result and durably publish it only after
     * this function and a separate full-file verification succeed. */
    /* Create an exclusive, empty shadow with this exact intended size. */
    bool (*begin_shadow)(void *context, uint64_t physical_bytes);
    bool (*read_old)(void *context, uint64_t offset, uint8_t *to,
        size_t bytes);
    bool (*write_shadow)(void *context, uint64_t offset, const uint8_t *from,
        size_t bytes);
    /* Production callers must use the strict DRBG for every invocation. */
    bool (*random)(void *context, uint8_t *to, size_t bytes);
};

/* Stream an authenticated old file into a new complete revision. An old size
 * of zero denotes a newly created file. patch_bytes may be zero for truncate
 * or sparse extension. No old file is ever modified here. The caller owns
 * publish/recovery, metadata, and plaintext migration; this helper alone is
 * not a VFS encryption boundary. Workspace, patch and output pointer
 * must not overlap; one operation owns the workspace until return. */
enum data_aead_status data_aead_rewrite_shadow(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t old_physical_bytes, uint64_t new_plaintext_bytes,
    uint64_t patch_offset, const uint8_t *patch, size_t patch_bytes,
    const struct data_aead_rewrite_io *io, uint8_t *workspace,
    size_t workspace_bytes, uint64_t *new_physical_bytes);

#endif
