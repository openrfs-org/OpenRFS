/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead_slots.h>

#include "../../vendor/monocypher/src/monocypher.h"

#define INDEX_MAC_OFFSET 40U
#define INDEX_RESERVED_OFFSET 72U

static const uint8_t index_label[] = "OpenRFS/v2/data/index";

struct index_entry {
    uint8_t stable_id[DATA_AEAD_ID_BYTES];
    uint64_t generation;
    uint64_t physical_bytes;
    unsigned file_slot;
    unsigned index_slot;
    bool valid;
};

struct file_reader {
    const struct data_aead_slot_io *io;
    unsigned slot;
};

static void copy_bytes(uint8_t *to, const uint8_t *from, size_t count)
{
    for (size_t at = 0U; at < count; ++at) to[at] = from[at];
}

static void zero_bytes(void *memory, size_t count)
{
    volatile uint8_t *to = memory;
    for (size_t at = 0U; at < count; ++at) to[at] = 0U;
}

static uint64_t read_u64(const uint8_t *from)
{
    uint64_t value = 0U;
    for (size_t at = 0U; at < 8U; ++at)
        value |= (uint64_t)from[at] << (at * 8U);
    return value;
}

static void write_u64(uint8_t *to, uint64_t value)
{
    for (size_t at = 0U; at < 8U; ++at)
        to[at] = (uint8_t)(value >> (at * 8U));
}

static size_t path_bytes(const char *path)
{
    size_t count = 0U;
    if (path == NULL) return 0U;
    while (count <= DATA_AEAD_PATH_MAX && path[count] != '\0') ++count;
    return count == 0U || count > DATA_AEAD_PATH_MAX ? 0U : count;
}

static void index_mac(const uint8_t key[DATA_AEAD_KEY_BYTES],
    const char *path, size_t length,
    const uint8_t record[DATA_AEAD_INDEX_BYTES], uint8_t mac[32])
{
    uint8_t derived[32];
    const uint8_t encoded_length[2] = {
        (uint8_t)length, (uint8_t)(length >> 8U)
    };
    crypto_blake2b_keyed(derived, sizeof(derived), key,
        DATA_AEAD_KEY_BYTES, index_label, sizeof(index_label) - 1U);
    crypto_blake2b_ctx context;
    crypto_blake2b_keyed_init(&context, sizeof(derived), derived,
        sizeof(derived));
    crypto_blake2b_update(&context, record, INDEX_MAC_OFFSET);
    crypto_blake2b_update(&context, encoded_length, sizeof(encoded_length));
    crypto_blake2b_update(&context, (const uint8_t *)path, length);
    crypto_blake2b_final(&context, mac);
    crypto_wipe(&context, sizeof(context));
    crypto_wipe(derived, sizeof(derived));
}

static enum data_aead_status index_open(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *path,
    const uint8_t record[DATA_AEAD_INDEX_BYTES], unsigned index_slot,
    struct index_entry *entry)
{
    const size_t length = path_bytes(path);
    if (key == NULL || record == NULL || entry == NULL || length == 0U)
        return DATA_AEAD_ARGUMENT;
    if (record[0] != 'O' || record[1] != 'R' || record[2] != 'D' ||
            record[3] != 'I' || record[4] != 1U || record[5] > 1U ||
            record[6] != 0U || record[7] != 0U ||
            read_u64(record + 8U) == 0U ||
            data_aead_physical_size(0U) > read_u64(record + 32U) ||
            read_u64(record + 32U) > DATA_AEAD_PHYSICAL_MAX)
        return DATA_AEAD_FORMAT;
    uint8_t id_or = 0U;
    for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at)
        id_or |= record[16U + at];
    if (id_or == 0U) return DATA_AEAD_FORMAT;
    for (size_t at = INDEX_RESERVED_OFFSET; at < DATA_AEAD_INDEX_BYTES;
            ++at)
        if (record[at] != 0U) return DATA_AEAD_FORMAT;
    uint8_t expected[32];
    uint8_t observed_mac = 0U;
    for (size_t at = 0U; at < 32U; ++at)
        observed_mac |= record[INDEX_MAC_OFFSET + at];
    if (observed_mac == 0U) return DATA_AEAD_FORMAT;
    index_mac(key, path, length, record, expected);
    const bool authentic = crypto_verify32(expected,
        record + INDEX_MAC_OFFSET) == 0;
    crypto_wipe(expected, sizeof(expected));
    if (!authentic) return DATA_AEAD_AUTHENTICATION;
    copy_bytes(entry->stable_id, record + 16U, DATA_AEAD_ID_BYTES);
    entry->generation = read_u64(record + 8U);
    entry->physical_bytes = read_u64(record + 32U);
    entry->file_slot = record[5];
    entry->index_slot = index_slot;
    entry->valid = true;
    return DATA_AEAD_OK;
}

static enum data_aead_status index_seal(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *path,
    const struct index_entry *entry,
    uint8_t record[DATA_AEAD_INDEX_BYTES])
{
    const size_t length = path_bytes(path);
    if (key == NULL || entry == NULL || record == NULL || length == 0U ||
            entry->file_slot > 1U || entry->generation == 0U ||
            entry->physical_bytes < DATA_AEAD_HEADER_BYTES ||
            entry->physical_bytes > DATA_AEAD_PHYSICAL_MAX)
        return DATA_AEAD_ARGUMENT;
    zero_bytes(record, DATA_AEAD_INDEX_BYTES);
    record[0] = 'O'; record[1] = 'R'; record[2] = 'D'; record[3] = 'I';
    record[4] = 1U;
    record[5] = (uint8_t)entry->file_slot;
    write_u64(record + 8U, entry->generation);
    copy_bytes(record + 16U, entry->stable_id, DATA_AEAD_ID_BYTES);
    write_u64(record + 32U, entry->physical_bytes);
    index_mac(key, path, length, record, record + INDEX_MAC_OFFSET);
    return DATA_AEAD_OK;
}

static enum data_aead_status scan_indices(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *path,
    const struct data_aead_slot_io *io, struct index_entry *selected)
{
    uint8_t record[DATA_AEAD_INDEX_BYTES];
    struct index_entry entries[2] = {0};
    bool any_present = false;
    bool authentication_failed = false;
    for (unsigned slot = 0U; slot < 2U; ++slot) {
        bool present = false;
        zero_bytes(record, sizeof(record));
        if (!io->index_read(io->context, slot, record, &present)) {
            zero_bytes(record, sizeof(record));
            return DATA_AEAD_IO;
        }
        if (present) {
            any_present = true;
            const enum data_aead_status status = index_open(key, path,
                record, slot, &entries[slot]);
            if (status == DATA_AEAD_AUTHENTICATION)
                authentication_failed = true;
        }
    }
    zero_bytes(record, sizeof(record));
    if (authentication_failed) return DATA_AEAD_AUTHENTICATION;
    if (!entries[0].valid && !entries[1].valid)
        return any_present ? DATA_AEAD_FORMAT : DATA_AEAD_NOT_FOUND;
    if (entries[0].valid && entries[1].valid) {
        if (entries[0].generation == entries[1].generation ||
                entries[0].file_slot == entries[1].file_slot ||
                (entries[0].generation > entries[1].generation ?
                    entries[0].generation - entries[1].generation :
                    entries[1].generation - entries[0].generation) != 1U ||
                crypto_verify16(entries[0].stable_id,
                    entries[1].stable_id) != 0)
            return DATA_AEAD_CONFLICT;
    }
    const unsigned chosen = !entries[0].valid ? 1U :
        !entries[1].valid ? 0U :
        entries[0].generation > entries[1].generation ? 0U : 1U;
    *selected = entries[chosen];
    return DATA_AEAD_OK;
}

static bool read_file(void *context, uint64_t offset, uint8_t *to,
    size_t bytes)
{
    const struct file_reader *reader = context;
    return reader->io->file_read(reader->io->context, reader->slot,
        offset, to, bytes);
}

static enum data_aead_status inspect_file(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *path,
    const struct data_aead_slot_io *io, unsigned slot,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_selection *selection)
{
    bool present = false;
    uint64_t size = 0U;
    if (!io->file_size(io->context, slot, &size, &present))
        return DATA_AEAD_IO;
    if (!present || size < DATA_AEAD_HEADER_BYTES)
        return DATA_AEAD_FORMAT;
    struct file_reader reader = {io, slot};
    uint64_t plaintext_bytes = 0U;
    enum data_aead_status status = data_aead_verify_shadow(key, path,
        size, read_file, &reader, workspace, workspace_bytes,
        &plaintext_bytes);
    if (status != DATA_AEAD_OK) return status;
    uint8_t header[DATA_AEAD_HEADER_BYTES];
    if (!read_file(&reader, 0U, header, sizeof(header)))
        return DATA_AEAD_IO;
    status = data_aead_file_identity(key, path, header, size,
        selection->stable_id);
    if (status == DATA_AEAD_OK)
        status = data_aead_generation(key, path, header, size,
            &selection->generation);
    zero_bytes(header, sizeof(header));
    if (status != DATA_AEAD_OK) return status;
    selection->physical_bytes = size;
    selection->plaintext_bytes = plaintext_bytes;
    selection->file_slot = slot;
    return DATA_AEAD_OK;
}

static bool callbacks_valid(const struct data_aead_slot_io *io,
    uint8_t *workspace, size_t workspace_bytes)
{
    return io != NULL && io->index_read != NULL && io->file_size != NULL &&
        io->file_read != NULL && workspace != NULL &&
        workspace_bytes >= DATA_AEAD_REWRITE_WORKSPACE_BYTES;
}

enum data_aead_status data_aead_select(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_slot_io *io, uint8_t *workspace,
    size_t workspace_bytes, struct data_aead_selection *selection)
{
    if (selection != NULL) zero_bytes(selection, sizeof(*selection));
    if (workspace != NULL)
        crypto_wipe(workspace, workspace_bytes <
            DATA_AEAD_REWRITE_WORKSPACE_BYTES ? workspace_bytes :
            DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    if (key == NULL || path_bytes(canonical_path) == 0U ||
            !callbacks_valid(io, workspace, workspace_bytes) ||
            selection == NULL)
        return DATA_AEAD_ARGUMENT;
    struct index_entry selected = {0};
    enum data_aead_status status = scan_indices(key, canonical_path, io,
        &selected);
    if (status == DATA_AEAD_NOT_FOUND) {
        for (unsigned slot = 0U; slot < 2U; ++slot) {
            uint64_t size = 0U;
            bool present = false;
            if (!io->file_size(io->context, slot, &size, &present))
                return DATA_AEAD_IO;
            if (present) return DATA_AEAD_FORMAT;
        }
        return DATA_AEAD_NOT_FOUND;
    }
    if (status != DATA_AEAD_OK) return status;
    status = inspect_file(key, canonical_path, io, selected.file_slot,
        workspace, workspace_bytes, selection);
    if (status != DATA_AEAD_OK ||
            selection->generation != selected.generation ||
            selection->physical_bytes != selected.physical_bytes ||
            crypto_verify16(selection->stable_id, selected.stable_id) != 0) {
        zero_bytes(selection, sizeof(*selection));
        return status == DATA_AEAD_OK ? DATA_AEAD_AUTHENTICATION : status;
    }
    selection->index_slot = selected.index_slot;
    return DATA_AEAD_OK;
}

static enum data_aead_status publish_candidate(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_slot_io *io, unsigned candidate_slot,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_selection *selection, bool recover_incomplete_index)
{
    if (selection != NULL) zero_bytes(selection, sizeof(*selection));
    if (workspace != NULL)
        crypto_wipe(workspace, workspace_bytes <
            DATA_AEAD_REWRITE_WORKSPACE_BYTES ? workspace_bytes :
            DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    if (key == NULL || path_bytes(canonical_path) == 0U ||
            !callbacks_valid(io, workspace, workspace_bytes) ||
            io->index_write == NULL || io->index_sync == NULL ||
            io->file_sync == NULL || candidate_slot > 1U ||
            selection == NULL)
        return DATA_AEAD_ARGUMENT;
    struct index_entry active = {0};
    enum data_aead_status status = scan_indices(key, canonical_path, io,
        &active);
    if (recover_incomplete_index && status == DATA_AEAD_FORMAT)
        status = DATA_AEAD_NOT_FOUND;
    if (status != DATA_AEAD_OK && status != DATA_AEAD_NOT_FOUND)
        return status;
    const bool exists = status == DATA_AEAD_OK;
    if (exists && (candidate_slot == active.file_slot ||
            active.generation == UINT64_MAX))
        return DATA_AEAD_CONFLICT;
    if (exists) {
        struct data_aead_selection current = {0};
        status = data_aead_select(key, canonical_path, io, workspace,
            workspace_bytes, &current);
        if (status != DATA_AEAD_OK) return status;
    }
    if (!io->file_sync(io->context, candidate_slot)) return DATA_AEAD_IO;
    struct data_aead_selection candidate = {0};
    status = inspect_file(key, canonical_path, io, candidate_slot,
        workspace, workspace_bytes, &candidate);
    if (status != DATA_AEAD_OK) return status;
    if (candidate.generation != (exists ? active.generation + 1U : 1U) ||
            (exists && crypto_verify16(candidate.stable_id,
                active.stable_id) != 0))
        return DATA_AEAD_CONFLICT;
    struct index_entry next = {0};
    copy_bytes(next.stable_id, candidate.stable_id,
        DATA_AEAD_ID_BYTES);
    next.generation = candidate.generation;
    next.physical_bytes = candidate.physical_bytes;
    next.file_slot = candidate_slot;
    next.index_slot = exists ? active.index_slot ^ 1U : 0U;
    uint8_t record[DATA_AEAD_INDEX_BYTES];
    status = index_seal(key, canonical_path, &next, record);
    if (status != DATA_AEAD_OK) return status;
    const bool written = io->index_write(io->context, next.index_slot,
        record);
    zero_bytes(record, sizeof(record));
    if (!written || !io->index_sync(io->context, next.index_slot))
        return DATA_AEAD_IO;
    status = data_aead_select(key, canonical_path, io, workspace,
        workspace_bytes, selection);
    if (status != DATA_AEAD_OK ||
            selection->generation != candidate.generation ||
            selection->file_slot != candidate_slot) {
        zero_bytes(selection, sizeof(*selection));
        return status == DATA_AEAD_OK ? DATA_AEAD_CONFLICT : status;
    }
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_publish(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_slot_io *io, unsigned candidate_slot,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_selection *selection)
{
    return publish_candidate(key, canonical_path, io, candidate_slot,
        workspace, workspace_bytes, selection, false);
}

enum data_aead_status data_aead_migrate_one(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_slot_io *io,
    const struct data_aead_rewrite_io *rewrite_io,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_selection *selection)
{
    if (selection != NULL) zero_bytes(selection, sizeof(*selection));
    if (workspace != NULL)
        crypto_wipe(workspace, workspace_bytes <
            DATA_AEAD_REWRITE_WORKSPACE_BYTES ? workspace_bytes :
            DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    if (key == NULL || path_bytes(canonical_path) == 0U ||
            !callbacks_valid(io, workspace, workspace_bytes) ||
            io->source_size == NULL || io->source_remove == NULL ||
            io->source_sync == NULL || rewrite_io == NULL ||
            selection == NULL)
        return DATA_AEAD_ARGUMENT;
    uint64_t source_bytes = 0U;
    bool source_present = false;
    if (!io->source_size(io->context, &source_bytes, &source_present))
        return DATA_AEAD_IO;
    enum data_aead_status status = data_aead_select(key, canonical_path,
        io, workspace, workspace_bytes, selection);
    if (status == DATA_AEAD_OK) {
        if (!source_present) {
            if (io->source_sync(io->context)) return DATA_AEAD_OK;
            zero_bytes(selection, sizeof(*selection));
            return DATA_AEAD_IO;
        }
    } else if (status == DATA_AEAD_NOT_FOUND ||
            (status == DATA_AEAD_FORMAT && source_present)) {
        if (!source_present) return DATA_AEAD_NOT_FOUND;
        if (status == DATA_AEAD_FORMAT) {
            struct index_entry indexed = {0};
            status = scan_indices(key, canonical_path, io, &indexed);
            if (status != DATA_AEAD_NOT_FOUND &&
                    status != DATA_AEAD_FORMAT)
                return status == DATA_AEAD_OK ? DATA_AEAD_FORMAT : status;
        }
        uint64_t physical_bytes = 0U;
        status = data_aead_migrate_plain_shadow(key, canonical_path,
            source_bytes, rewrite_io, workspace, workspace_bytes,
            &physical_bytes);
        if (status != DATA_AEAD_OK) return status;
        status = publish_candidate(key, canonical_path, io, 0U,
            workspace, workspace_bytes, selection, true);
        if (status != DATA_AEAD_OK) return status;
    } else {
        return status;
    }
    if (!io->source_remove(io->context) ||
            !io->source_sync(io->context)) {
        zero_bytes(selection, sizeof(*selection));
        return DATA_AEAD_IO;
    }
    return DATA_AEAD_OK;
}
