/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead_rewrite.h>

#include "../../vendor/monocypher/src/monocypher.h"

static void zero_bytes(uint8_t *bytes, size_t length)
{
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void copy_bytes(uint8_t *to, const uint8_t *from, size_t length)
{
    for (size_t index = 0U; index < length; ++index) to[index] = from[index];
}

static bool equal_bytes(const uint8_t *left, const uint8_t *right,
    size_t length)
{
    uint8_t difference = 0U;
    for (size_t index = 0U; index < length; ++index)
        difference |= left[index] ^ right[index];
    return difference == 0U;
}

static uint64_t chunk_count(uint64_t bytes)
{
    return bytes / DATA_AEAD_CHUNK_BYTES +
        (bytes % DATA_AEAD_CHUNK_BYTES != 0U ? 1U : 0U);
}

static size_t chunk_length(uint64_t bytes, uint64_t index)
{
    const uint64_t start = index * DATA_AEAD_CHUNK_BYTES;
    if (start >= bytes) return 0U;
    const uint64_t remaining = bytes - start;
    return remaining >= DATA_AEAD_CHUNK_BYTES ? DATA_AEAD_CHUNK_BYTES :
        (size_t)remaining;
}

static enum data_aead_status open_old_chunk(
    const struct data_aead_rewrite_io *io, const uint8_t *key,
    const uint8_t old_header[DATA_AEAD_HEADER_BYTES], uint64_t index,
    uint8_t sealed[DATA_AEAD_SEALED_CHUNK_BYTES],
    uint8_t plain[DATA_AEAD_CHUNK_BYTES])
{
    const uint64_t offset = DATA_AEAD_HEADER_BYTES +
        index * DATA_AEAD_SEALED_CHUNK_BYTES;
    if (!io->read_old(io->context, offset, sealed,
            DATA_AEAD_SEALED_CHUNK_BYTES)) return DATA_AEAD_IO;
    size_t opened = 0U;
    return data_aead_open_chunk(key, old_header, index, sealed, plain,
        &opened);
}

enum data_aead_status data_aead_rewrite_shadow_paths(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *old_path,
    const char *new_path,
    uint64_t old_physical_bytes, uint64_t new_plaintext_bytes,
    uint64_t patch_offset, const uint8_t *patch, size_t patch_bytes,
    const struct data_aead_rewrite_io *io, uint8_t *workspace,
    size_t workspace_bytes, uint64_t *new_physical_bytes)
{
    enum data_aead_status result = DATA_AEAD_OK;
    uint8_t id[DATA_AEAD_ID_BYTES] = {0};
    uint8_t nonce[DATA_AEAD_NONCE_BYTES] = {0};
    uint64_t old_plaintext_bytes = 0U;
    uint64_t output_size;
    uint8_t *old_header;
    uint8_t *new_header;
    uint8_t *sealed;
    uint8_t *plain;

    if (new_physical_bytes != NULL) *new_physical_bytes = 0U;
    if (key == NULL || old_path == NULL || new_path == NULL || io == NULL ||
            io->begin_shadow == NULL || io->read_old == NULL ||
            io->write_shadow == NULL ||
            io->random == NULL || workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ||
            new_physical_bytes == NULL ||
            (patch_bytes != 0U && patch == NULL) ||
            (patch_bytes != 0U &&
             (patch_offset > new_plaintext_bytes ||
              patch_bytes > new_plaintext_bytes - patch_offset))) {
        if (workspace != NULL) crypto_wipe(workspace,
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ?
                workspace_bytes : DATA_AEAD_REWRITE_WORKSPACE_BYTES);
        return DATA_AEAD_ARGUMENT;
    }
    output_size = data_aead_physical_size(new_plaintext_bytes);
    if (output_size == 0U || old_physical_bytes > DATA_AEAD_PHYSICAL_MAX) {
        crypto_wipe(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
        return DATA_AEAD_RANGE;
    }
    old_header = workspace;
    new_header = old_header + DATA_AEAD_HEADER_BYTES;
    sealed = new_header + DATA_AEAD_HEADER_BYTES;
    plain = sealed + DATA_AEAD_SEALED_CHUNK_BYTES;
    zero_bytes(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);

    if (old_physical_bytes != 0U) {
        if (old_physical_bytes < DATA_AEAD_HEADER_BYTES ||
                !io->read_old(io->context, 0U, old_header,
                    DATA_AEAD_HEADER_BYTES)) {
            result = DATA_AEAD_IO;
            goto done;
        }
        result = data_aead_check_header(key, old_path, old_header,
            old_physical_bytes, &old_plaintext_bytes);
        if (result != DATA_AEAD_OK) goto done;
    }
    if (!io->random(io->context, id, sizeof(id))) {
        result = DATA_AEAD_ENTROPY;
        goto done;
    }
    if (old_physical_bytes != 0U &&
            equal_bytes(id, old_header + 20U, sizeof(id))) {
        result = DATA_AEAD_ENTROPY;
        goto done;
    }
    result = data_aead_make_header(key, new_path, new_plaintext_bytes,
        id, new_header);
    if (result != DATA_AEAD_OK) goto done;
    if (!io->begin_shadow(io->context, output_size)) {
        result = DATA_AEAD_IO;
        goto done;
    }
    if (!io->write_shadow(io->context, 0U, new_header,
            DATA_AEAD_HEADER_BYTES)) {
        result = DATA_AEAD_IO;
        goto done;
    }

    const uint64_t old_chunks = chunk_count(old_plaintext_bytes);
    const uint64_t new_chunks = chunk_count(new_plaintext_bytes);
    const uint64_t total_chunks = old_chunks > new_chunks ? old_chunks :
        new_chunks;
    for (uint64_t index = 0U; index < total_chunks; ++index) {
        zero_bytes(plain, DATA_AEAD_CHUNK_BYTES);
        if (index < old_chunks) {
            result = open_old_chunk(io, key, old_header, index, sealed, plain);
            if (result != DATA_AEAD_OK) goto done;
        }
        if (index >= new_chunks) continue; /* Validate truncated-away input. */

        const uint64_t start = index * DATA_AEAD_CHUNK_BYTES;
        const uint64_t end = start + DATA_AEAD_CHUNK_BYTES;
        if (patch_bytes != 0U && patch_offset < end &&
                patch_offset + patch_bytes > start) {
            const uint64_t overlap_start = patch_offset > start ?
                patch_offset : start;
            const uint64_t patch_end = patch_offset + patch_bytes;
            const uint64_t overlap_end = patch_end < end ? patch_end : end;
            copy_bytes(plain + (size_t)(overlap_start - start),
                patch + (size_t)(overlap_start - patch_offset),
                (size_t)(overlap_end - overlap_start));
        }
        const size_t length = chunk_length(new_plaintext_bytes, index);
        zero_bytes(plain + length, DATA_AEAD_CHUNK_BYTES - length);
        if (!io->random(io->context, nonce, sizeof(nonce))) {
            result = DATA_AEAD_ENTROPY;
            goto done;
        }
        result = data_aead_seal_chunk(key, new_header, index, nonce, plain,
            length, sealed);
        if (result != DATA_AEAD_OK) goto done;
        if (!io->write_shadow(io->context, DATA_AEAD_HEADER_BYTES +
                index * DATA_AEAD_SEALED_CHUNK_BYTES, sealed,
                DATA_AEAD_SEALED_CHUNK_BYTES)) {
            result = DATA_AEAD_IO;
            goto done;
        }
    }
    *new_physical_bytes = output_size;
done:
    crypto_wipe(id, sizeof(id));
    crypto_wipe(nonce, sizeof(nonce));
    crypto_wipe(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    return result;
}

enum data_aead_status data_aead_rewrite_shadow(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t old_physical_bytes, uint64_t new_plaintext_bytes,
    uint64_t patch_offset, const uint8_t *patch, size_t patch_bytes,
    const struct data_aead_rewrite_io *io, uint8_t *workspace,
    size_t workspace_bytes, uint64_t *new_physical_bytes)
{
    return data_aead_rewrite_shadow_paths(key, canonical_path,
        canonical_path, old_physical_bytes, new_plaintext_bytes,
        patch_offset, patch, patch_bytes, io, workspace, workspace_bytes,
        new_physical_bytes);
}

enum data_aead_status data_aead_migrate_plain_shadow(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t old_plaintext_bytes, const struct data_aead_rewrite_io *io,
    uint8_t *workspace, size_t workspace_bytes,
    uint64_t *new_physical_bytes)
{
    enum data_aead_status result = DATA_AEAD_OK;
    uint8_t id[DATA_AEAD_ID_BYTES] = {0};
    uint8_t nonce[DATA_AEAD_NONCE_BYTES] = {0};
    uint8_t *header;
    uint8_t *sealed;
    uint8_t *plain;
    uint64_t output_size;

    if (new_physical_bytes != NULL) *new_physical_bytes = 0U;
    if (key == NULL || canonical_path == NULL || io == NULL ||
            io->begin_shadow == NULL || io->read_old == NULL ||
            io->write_shadow == NULL || io->random == NULL ||
            workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ||
            new_physical_bytes == NULL) {
        if (workspace != NULL) crypto_wipe(workspace,
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ?
                workspace_bytes : DATA_AEAD_REWRITE_WORKSPACE_BYTES);
        return DATA_AEAD_ARGUMENT;
    }
    output_size = data_aead_physical_size(old_plaintext_bytes);
    if (output_size == 0U) {
        crypto_wipe(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
        return DATA_AEAD_RANGE;
    }
    header = workspace;
    sealed = header + DATA_AEAD_HEADER_BYTES;
    plain = sealed + DATA_AEAD_SEALED_CHUNK_BYTES;
    zero_bytes(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    if (!io->random(io->context, id, sizeof(id))) {
        result = DATA_AEAD_ENTROPY;
        goto done;
    }
    result = data_aead_make_header(key, canonical_path, old_plaintext_bytes,
        id, header);
    if (result != DATA_AEAD_OK) goto done;
    if (!io->begin_shadow(io->context, output_size) ||
            !io->write_shadow(io->context, 0U, header,
                DATA_AEAD_HEADER_BYTES)) {
        result = DATA_AEAD_IO;
        goto done;
    }
    for (uint64_t index = 0U; index < chunk_count(old_plaintext_bytes);
            ++index) {
        const size_t length = chunk_length(old_plaintext_bytes, index);
        zero_bytes(plain, DATA_AEAD_CHUNK_BYTES);
        if (!io->read_old(io->context, index * DATA_AEAD_CHUNK_BYTES,
                plain, length)) {
            result = DATA_AEAD_IO;
            goto done;
        }
        if (!io->random(io->context, nonce, sizeof(nonce))) {
            result = DATA_AEAD_ENTROPY;
            goto done;
        }
        result = data_aead_seal_chunk(key, header, index, nonce, plain,
            length, sealed);
        if (result != DATA_AEAD_OK) goto done;
        if (!io->write_shadow(io->context, DATA_AEAD_HEADER_BYTES +
                index * DATA_AEAD_SEALED_CHUNK_BYTES, sealed,
                DATA_AEAD_SEALED_CHUNK_BYTES)) {
            result = DATA_AEAD_IO;
            goto done;
        }
    }
    *new_physical_bytes = output_size;
done:
    crypto_wipe(id, sizeof(id));
    crypto_wipe(nonce, sizeof(nonce));
    crypto_wipe(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    return result;
}

enum data_aead_status data_aead_verify_shadow(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t physical_bytes,
    bool (*read_shadow)(void *context, uint64_t offset, uint8_t *to,
        size_t bytes), void *context, uint8_t *workspace,
    size_t workspace_bytes, uint64_t *plaintext_bytes)
{
    enum data_aead_status result = DATA_AEAD_OK;
    uint64_t length = 0U;
    uint8_t *header;
    uint8_t *sealed;
    uint8_t *plain;

    if (plaintext_bytes != NULL) *plaintext_bytes = 0U;
    if (key == NULL || canonical_path == NULL || read_shadow == NULL ||
            workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ||
            plaintext_bytes == NULL) {
        if (workspace != NULL) crypto_wipe(workspace,
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ?
                workspace_bytes : DATA_AEAD_REWRITE_WORKSPACE_BYTES);
        return DATA_AEAD_ARGUMENT;
    }
    if (physical_bytes < DATA_AEAD_HEADER_BYTES ||
            physical_bytes > DATA_AEAD_PHYSICAL_MAX) {
        crypto_wipe(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
        return DATA_AEAD_RANGE;
    }
    header = workspace;
    sealed = header + DATA_AEAD_HEADER_BYTES;
    plain = sealed + DATA_AEAD_SEALED_CHUNK_BYTES;
    zero_bytes(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    if (!read_shadow(context, 0U, header, DATA_AEAD_HEADER_BYTES)) {
        result = DATA_AEAD_IO;
        goto done;
    }
    result = data_aead_check_header(key, canonical_path, header,
        physical_bytes, &length);
    if (result != DATA_AEAD_OK) goto done;
    for (uint64_t index = 0U; index < chunk_count(length); ++index) {
        size_t opened = 0U;
        const uint64_t offset = DATA_AEAD_HEADER_BYTES +
            index * DATA_AEAD_SEALED_CHUNK_BYTES;
        if (!read_shadow(context, offset, sealed,
                DATA_AEAD_SEALED_CHUNK_BYTES)) {
            result = DATA_AEAD_IO;
            goto done;
        }
        result = data_aead_open_chunk(key, header, index, sealed, plain,
            &opened);
        if (result != DATA_AEAD_OK || opened != chunk_length(length, index)) {
            if (result == DATA_AEAD_OK) result = DATA_AEAD_FORMAT;
            goto done;
        }
    }
    *plaintext_bytes = length;
done:
    crypto_wipe(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    return result;
}

enum data_aead_status data_aead_read_range(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t physical_bytes, uint64_t offset, uint8_t *destination,
    size_t capacity, bool (*read_file)(void *context, uint64_t offset,
        uint8_t *to, size_t bytes), void *context, uint8_t *workspace,
    size_t workspace_bytes, size_t *read_bytes)
{
    enum data_aead_status result = DATA_AEAD_OK;
    uint64_t length = 0U;
    size_t produced = 0U;
    uint8_t *header;
    uint8_t *sealed;
    uint8_t *plain;

    if (read_bytes != NULL) *read_bytes = 0U;
    if (key == NULL || canonical_path == NULL || read_file == NULL ||
            workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ||
            read_bytes == NULL || (capacity != 0U && destination == NULL)) {
        if (workspace != NULL) crypto_wipe(workspace,
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ?
                workspace_bytes : DATA_AEAD_REWRITE_WORKSPACE_BYTES);
        return DATA_AEAD_ARGUMENT;
    }
    if (physical_bytes < DATA_AEAD_HEADER_BYTES ||
            physical_bytes > DATA_AEAD_PHYSICAL_MAX) {
        crypto_wipe(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
        return DATA_AEAD_RANGE;
    }
    header = workspace;
    sealed = header + DATA_AEAD_HEADER_BYTES;
    plain = sealed + DATA_AEAD_SEALED_CHUNK_BYTES;
    zero_bytes(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    if (!read_file(context, 0U, header, DATA_AEAD_HEADER_BYTES)) {
        result = DATA_AEAD_IO;
        goto done;
    }
    result = data_aead_check_header(key, canonical_path, header,
        physical_bytes, &length);
    if (result != DATA_AEAD_OK || offset >= length || capacity == 0U)
        goto done;
    const uint64_t available = length - offset;
    const size_t wanted = available < capacity ? (size_t)available : capacity;
    while (produced < wanted) {
        const uint64_t position = offset + produced;
        const uint64_t index = position / DATA_AEAD_CHUNK_BYTES;
        const size_t start = (size_t)(position % DATA_AEAD_CHUNK_BYTES);
        const uint64_t physical_offset = DATA_AEAD_HEADER_BYTES +
            index * DATA_AEAD_SEALED_CHUNK_BYTES;
        size_t opened = 0U;
        if (!read_file(context, physical_offset, sealed,
                DATA_AEAD_SEALED_CHUNK_BYTES)) {
            result = DATA_AEAD_IO;
            goto done;
        }
        result = data_aead_open_chunk(key, header, index, sealed, plain,
            &opened);
        if (result != DATA_AEAD_OK) goto done;
        if (start >= opened) {
            result = DATA_AEAD_FORMAT;
            goto done;
        }
        const size_t span = wanted - produced < opened - start ?
            wanted - produced : opened - start;
        copy_bytes(destination + produced, plain + start, span);
        produced += span;
    }
    *read_bytes = produced;
done:
    if (result != DATA_AEAD_OK && produced != 0U)
        crypto_wipe(destination, produced);
    crypto_wipe(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    return result;
}
