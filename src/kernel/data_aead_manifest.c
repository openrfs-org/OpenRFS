/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead_manifest.h>

#include "../../vendor/monocypher/src/monocypher.h"

#define MANIFEST_PREFIX_BYTES 8U
#define MANIFEST_NONCE_OFFSET 8U
#define MANIFEST_TAG_OFFSET 32U
#define MANIFEST_CIPHERTEXT_OFFSET 48U
#define MANIFEST_PLAINTEXT_BYTES (DATA_AEAD_MANIFEST_BYTES - MANIFEST_CIPHERTEXT_OFFSET)
#define SEGMENT_ENTRY_BYTES 32U

static const uint8_t manifest_key_label[] = "OpenRFS/v1/data/manifest-key";
static const uint8_t manifest_ad_label[] = "OpenRFS/v1/data/manifest-ad";
static const uint8_t storage_root_label[] = "OpenRFS/v1/data/storage-root";
static const uint8_t manifest_path_label[] = "OpenRFS/v1/data/manifest-path";
static const char hex_digits[] = "0123456789ABCDEF";

static void copy_bytes(uint8_t *to, const uint8_t *from, size_t count)
{
    for (size_t at = 0U; at < count; ++at) to[at] = from[at];
}

static void zero_bytes(void *memory, size_t count)
{
    volatile uint8_t *to = memory;
    for (size_t at = 0U; at < count; ++at) to[at] = 0U;
}

static void write_u64(uint8_t *to, uint64_t value)
{
    for (size_t at = 0U; at < 8U; ++at)
        to[at] = (uint8_t)(value >> (8U * at));
}

static uint64_t read_u64(const uint8_t *from)
{
    uint64_t value = 0U;
    for (size_t at = 0U; at < 8U; ++at)
        value |= (uint64_t)from[at] << (8U * at);
    return value;
}

static bool valid_id(const uint8_t id[DATA_AEAD_ID_BYTES])
{
    uint8_t any = 0U;
    for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at) any |= id[at];
    return any != 0U;
}

static size_t path_length(const char *path)
{
    size_t at = 0U;
    size_t component = 0U;
    if (path == NULL || path[0] == '/' || path[0] == '\\') return 0U;
    while (at <= DATA_AEAD_PATH_MAX && path[at] != '\0') {
        if ((uint8_t)path[at] < 0x20U || path[at] == '\\') return 0U;
        if (path[at] == '/') {
            const size_t length = at - component;
            if (length == 0U ||
                    (length == 1U && path[component] == '.') ||
                    (length == 2U && path[component] == '.' &&
                     path[component + 1U] == '.')) return 0U;
            component = at + 1U;
        }
        ++at;
    }
    const size_t last = at - component;
    return at == 0U || at > DATA_AEAD_PATH_MAX || last == 0U ||
        (last == 1U && path[component] == '.') ||
        (last == 2U && path[component] == '.' &&
         path[component + 1U] == '.') ? 0U : at;
}

static bool manifest_valid(const struct data_aead_manifest *manifest)
{
    if (manifest == NULL || !valid_id(manifest->stable_id) ||
            manifest->generation == 0U ||
            manifest->segment_count > DATA_AEAD_SEGMENTS_MAX ||
            manifest->plaintext_bytes >
                DATA_AEAD_SEGMENT_BYTES * DATA_AEAD_SEGMENTS_MAX)
        return false;
    uint64_t total = 0U;
    for (unsigned at = 0U; at < manifest->segment_count; ++at) {
        const struct data_aead_segment *segment = &manifest->segments[at];
        if (!valid_id(segment->revision_id) ||
                segment->plaintext_bytes == 0U ||
                segment->plaintext_bytes > DATA_AEAD_SEGMENT_BYTES ||
                segment->generation == 0U ||
                (at + 1U < manifest->segment_count &&
                 segment->plaintext_bytes != DATA_AEAD_SEGMENT_BYTES))
            return false;
        for (unsigned earlier = 0U; earlier < at; ++earlier)
            if (crypto_verify16(segment->revision_id,
                    manifest->segments[earlier].revision_id) == 0)
                return false;
        total += segment->plaintext_bytes;
    }
    return total == manifest->plaintext_bytes &&
        (manifest->plaintext_bytes == 0U) ==
            (manifest->segment_count == 0U);
}

static void encode_manifest(const struct data_aead_manifest *manifest,
    uint8_t plain[MANIFEST_PLAINTEXT_BYTES])
{
    zero_bytes(plain, MANIFEST_PLAINTEXT_BYTES);
    copy_bytes(plain, manifest->stable_id, DATA_AEAD_ID_BYTES);
    write_u64(plain + 16U, manifest->plaintext_bytes);
    write_u64(plain + 24U, manifest->generation);
    plain[32] = (uint8_t)manifest->segment_count;
    for (unsigned at = 0U; at < manifest->segment_count; ++at) {
        uint8_t *entry = plain + 40U + at * SEGMENT_ENTRY_BYTES;
        copy_bytes(entry, manifest->segments[at].revision_id,
            DATA_AEAD_ID_BYTES);
        write_u64(entry + 16U, manifest->segments[at].plaintext_bytes);
        write_u64(entry + 24U, manifest->segments[at].generation);
    }
}

static bool decode_manifest(const uint8_t plain[MANIFEST_PLAINTEXT_BYTES],
    struct data_aead_manifest *manifest)
{
    zero_bytes(manifest, sizeof(*manifest));
    for (size_t at = 33U; at < 40U; ++at)
        if (plain[at] != 0U) return false;
    if (plain[32] > DATA_AEAD_SEGMENTS_MAX) return false;
    manifest->segment_count = plain[32];
    copy_bytes(manifest->stable_id, plain, DATA_AEAD_ID_BYTES);
    manifest->plaintext_bytes = read_u64(plain + 16U);
    manifest->generation = read_u64(plain + 24U);
    for (unsigned at = 0U; at < DATA_AEAD_SEGMENTS_MAX; ++at) {
        const uint8_t *entry = plain + 40U + at * SEGMENT_ENTRY_BYTES;
        if (at < manifest->segment_count) {
            copy_bytes(manifest->segments[at].revision_id, entry,
                DATA_AEAD_ID_BYTES);
            manifest->segments[at].plaintext_bytes = read_u64(entry + 16U);
            manifest->segments[at].generation = read_u64(entry + 24U);
        } else {
            for (size_t byte = 0U; byte < SEGMENT_ENTRY_BYTES; ++byte)
                if (entry[byte] != 0U) return false;
        }
    }
    return manifest_valid(manifest);
}

static size_t make_ad(const char *path, size_t path_bytes,
    const uint8_t prefix[MANIFEST_PREFIX_BYTES],
    uint8_t ad[sizeof(manifest_ad_label) - 1U + MANIFEST_PREFIX_BYTES +
        2U + DATA_AEAD_PATH_MAX])
{
    size_t used = sizeof(manifest_ad_label) - 1U;
    copy_bytes(ad, manifest_ad_label, used);
    copy_bytes(ad + used, prefix, MANIFEST_PREFIX_BYTES);
    used += MANIFEST_PREFIX_BYTES;
    ad[used++] = (uint8_t)path_bytes;
    ad[used++] = (uint8_t)(path_bytes >> 8U);
    copy_bytes(ad + used, (const uint8_t *)path, path_bytes);
    return used + path_bytes;
}

enum data_aead_status data_aead_manifest_seal(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_manifest *manifest,
    bool (*random)(void *context, uint8_t *to, size_t bytes),
    void *context, uint8_t record[DATA_AEAD_MANIFEST_BYTES])
{
    if (record == NULL) return DATA_AEAD_ARGUMENT;
    zero_bytes(record, DATA_AEAD_MANIFEST_BYTES);
    const size_t length = path_length(canonical_path);
    if (key == NULL || length == 0U || !manifest_valid(manifest) ||
            random == NULL) return DATA_AEAD_ARGUMENT;
    record[0] = 'O'; record[1] = 'R'; record[2] = 'D'; record[3] = 'M';
    record[4] = 1U;
    if (!random(context, record + MANIFEST_NONCE_OFFSET,
            DATA_AEAD_NONCE_BYTES)) {
        zero_bytes(record, DATA_AEAD_MANIFEST_BYTES);
        return DATA_AEAD_ENTROPY;
    }
    uint8_t derived[DATA_AEAD_KEY_BYTES];
    uint8_t plain[MANIFEST_PLAINTEXT_BYTES];
    uint8_t ad[sizeof(manifest_ad_label) - 1U + MANIFEST_PREFIX_BYTES +
        2U + DATA_AEAD_PATH_MAX];
    crypto_blake2b_keyed(derived, sizeof(derived), key,
        DATA_AEAD_KEY_BYTES, manifest_key_label,
        sizeof(manifest_key_label) - 1U);
    encode_manifest(manifest, plain);
    const size_t ad_bytes = make_ad(canonical_path, length, record, ad);
    crypto_aead_lock(record + MANIFEST_CIPHERTEXT_OFFSET,
        record + MANIFEST_TAG_OFFSET, derived,
        record + MANIFEST_NONCE_OFFSET, ad, ad_bytes,
        plain, sizeof(plain));
    crypto_wipe(derived, sizeof(derived));
    crypto_wipe(plain, sizeof(plain));
    crypto_wipe(ad, sizeof(ad));
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_manifest_open(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const uint8_t record[DATA_AEAD_MANIFEST_BYTES],
    struct data_aead_manifest *manifest)
{
    if (manifest != NULL) zero_bytes(manifest, sizeof(*manifest));
    const size_t length = path_length(canonical_path);
    if (key == NULL || length == 0U || record == NULL || manifest == NULL)
        return DATA_AEAD_ARGUMENT;
    if (record[0] != 'O' || record[1] != 'R' || record[2] != 'D' ||
            record[3] != 'M' || record[4] != 1U || record[5] != 0U ||
            record[6] != 0U || record[7] != 0U)
        return DATA_AEAD_FORMAT;
    uint8_t derived[DATA_AEAD_KEY_BYTES];
    uint8_t plain[MANIFEST_PLAINTEXT_BYTES];
    uint8_t ad[sizeof(manifest_ad_label) - 1U + MANIFEST_PREFIX_BYTES +
        2U + DATA_AEAD_PATH_MAX];
    crypto_blake2b_keyed(derived, sizeof(derived), key,
        DATA_AEAD_KEY_BYTES, manifest_key_label,
        sizeof(manifest_key_label) - 1U);
    const size_t ad_bytes = make_ad(canonical_path, length, record, ad);
    const int opened = crypto_aead_unlock(plain,
        record + MANIFEST_TAG_OFFSET, derived,
        record + MANIFEST_NONCE_OFFSET, ad, ad_bytes,
        record + MANIFEST_CIPHERTEXT_OFFSET, sizeof(plain));
    const bool valid = opened == 0 && decode_manifest(plain, manifest);
    crypto_wipe(derived, sizeof(derived));
    crypto_wipe(plain, sizeof(plain));
    crypto_wipe(ad, sizeof(ad));
    if (!valid) {
        zero_bytes(manifest, sizeof(*manifest));
        return opened == 0 ? DATA_AEAD_FORMAT : DATA_AEAD_AUTHENTICATION;
    }
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_manifest_select(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_manifest_slot_io *io,
    struct data_aead_manifest *manifest, unsigned *slot)
{
    if (manifest != NULL) zero_bytes(manifest, sizeof(*manifest));
    if (slot != NULL) *slot = 0U;
    if (key == NULL || path_length(canonical_path) == 0U || io == NULL ||
            io->read == NULL || manifest == NULL || slot == NULL)
        return DATA_AEAD_ARGUMENT;
    struct data_aead_manifest candidates[2] = {0};
    uint8_t record[DATA_AEAD_MANIFEST_BYTES];
    bool valid[2] = {false, false};
    bool present_any = false;
    bool authentication_failed = false;
    for (unsigned at = 0U; at < 2U; ++at) {
        bool present = false;
        zero_bytes(record, sizeof(record));
        if (!io->read(io->context, at, record, &present)) {
            zero_bytes(record, sizeof(record));
            zero_bytes(candidates, sizeof(candidates));
            return DATA_AEAD_IO;
        }
        if (present) {
            present_any = true;
            const enum data_aead_status status = data_aead_manifest_open(
                key, canonical_path, record, &candidates[at]);
            valid[at] = status == DATA_AEAD_OK;
            if (status == DATA_AEAD_AUTHENTICATION)
                authentication_failed = true;
        }
    }
    zero_bytes(record, sizeof(record));
    if (authentication_failed) {
        zero_bytes(candidates, sizeof(candidates));
        return DATA_AEAD_AUTHENTICATION;
    }
    if (!valid[0] && !valid[1]) {
        zero_bytes(candidates, sizeof(candidates));
        return present_any ? DATA_AEAD_FORMAT : DATA_AEAD_NOT_FOUND;
    }
    if (valid[0] && valid[1] &&
            (crypto_verify16(candidates[0].stable_id,
                candidates[1].stable_id) != 0 ||
             candidates[0].generation == candidates[1].generation ||
             (candidates[0].generation > candidates[1].generation ?
                candidates[0].generation - candidates[1].generation :
                candidates[1].generation - candidates[0].generation) != 1U)) {
        zero_bytes(candidates, sizeof(candidates));
        return DATA_AEAD_CONFLICT;
    }
    const unsigned chosen = !valid[0] ? 1U : !valid[1] ? 0U :
        candidates[0].generation > candidates[1].generation ? 0U : 1U;
    *manifest = candidates[chosen];
    *slot = chosen;
    zero_bytes(candidates, sizeof(candidates));
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_segment_binding(
    const uint8_t stable_id[DATA_AEAD_ID_BYTES], unsigned index,
    char path[DATA_AEAD_PATH_MAX + 1U])
{
    if (path == NULL) return DATA_AEAD_ARGUMENT;
    zero_bytes(path, DATA_AEAD_PATH_MAX + 1U);
    if (stable_id == NULL || !valid_id(stable_id) ||
            index >= DATA_AEAD_SEGMENTS_MAX) return DATA_AEAD_ARGUMENT;
    size_t used = 0U;
    path[used++] = 'S'; path[used++] = 'E'; path[used++] = 'G';
    path[used++] = '/';
    for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at) {
        if (at != 0U && at % 4U == 0U) path[used++] = '/';
        path[used++] = hex_digits[stable_id[at] >> 4U];
        path[used++] = hex_digits[stable_id[at] & 15U];
    }
    path[used++] = '/'; path[used++] = 'S';
    path[used++] = (char)('0' + index);
    path[used] = '\0';
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_segment_storage_path(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const uint8_t revision_id[DATA_AEAD_ID_BYTES],
    char path[DATA_AEAD_PATH_MAX + 1U])
{
    if (path == NULL) return DATA_AEAD_ARGUMENT;
    zero_bytes(path, DATA_AEAD_PATH_MAX + 1U);
    if (key == NULL || revision_id == NULL || !valid_id(revision_id))
        return DATA_AEAD_ARGUMENT;
    uint8_t root[4];
    crypto_blake2b_keyed(root, sizeof(root), key,
        DATA_AEAD_KEY_BYTES, storage_root_label,
        sizeof(storage_root_label) - 1U);
    size_t used = 0U;
    for (size_t at = 0U; at < sizeof(root); ++at) {
        path[used++] = hex_digits[root[at] >> 4U];
        path[used++] = hex_digits[root[at] & 15U];
    }
    crypto_wipe(root, sizeof(root));
    path[used++] = '/';
    for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at) {
        if (at != 0U && at % 4U == 0U) path[used++] = '/';
        path[used++] = hex_digits[revision_id[at] >> 4U];
        path[used++] = hex_digits[revision_id[at] & 15U];
    }
    path[used++] = '/';
    path[used++] = 'S'; path[used++] = 'E'; path[used++] = 'G';
    path[used++] = '.'; path[used++] = 'D';
    path[used++] = 'A'; path[used++] = 'T';
    path[used] = '\0';
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_manifest_storage_path(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    unsigned slot, char path[DATA_AEAD_PATH_MAX + 1U])
{
    if (path == NULL) return DATA_AEAD_ARGUMENT;
    zero_bytes(path, DATA_AEAD_PATH_MAX + 1U);
    const size_t length = path_length(canonical_path);
    if (key == NULL || length == 0U || slot > 2U)
        return DATA_AEAD_ARGUMENT;
    uint8_t root[4];
    uint8_t digest[DATA_AEAD_ID_BYTES];
    crypto_blake2b_keyed(root, sizeof(root), key,
        DATA_AEAD_KEY_BYTES, storage_root_label,
        sizeof(storage_root_label) - 1U);
    crypto_blake2b_ctx context;
    crypto_blake2b_keyed_init(&context, sizeof(digest), key,
        DATA_AEAD_KEY_BYTES);
    crypto_blake2b_update(&context, manifest_path_label,
        sizeof(manifest_path_label) - 1U);
    const uint8_t encoded_length[2] = {
        (uint8_t)length, (uint8_t)(length >> 8U)
    };
    crypto_blake2b_update(&context, encoded_length,
        sizeof(encoded_length));
    crypto_blake2b_update(&context,
        (const uint8_t *)canonical_path, length);
    crypto_blake2b_final(&context, digest);
    crypto_wipe(&context, sizeof(context));
    size_t used = 0U;
    for (size_t at = 0U; at < sizeof(root); ++at) {
        path[used++] = hex_digits[root[at] >> 4U];
        path[used++] = hex_digits[root[at] & 15U];
    }
    crypto_wipe(root, sizeof(root));
    path[used++] = '/'; path[used++] = 'M'; path[used++] = '/';
    for (size_t at = 0U; at < sizeof(digest); ++at) {
        if (at != 0U && at % 4U == 0U) path[used++] = '/';
        path[used++] = hex_digits[digest[at] >> 4U];
        path[used++] = hex_digits[digest[at] & 15U];
    }
    crypto_wipe(digest, sizeof(digest));
    path[used++] = '/';
    if (slot == 2U) {
        path[used++] = 'T'; path[used++] = 'M';
        path[used++] = 'P';
    } else {
        path[used++] = 'M'; path[used++] = (char)('0' + slot);
    }
    path[used++] = '.'; path[used++] = 'D';
    path[used++] = 'A'; path[used++] = 'T';
    path[used] = '\0';
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_manifest_check_segment(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const struct data_aead_manifest *manifest, unsigned index,
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t physical_bytes)
{
    if (key == NULL || !manifest_valid(manifest) ||
            index >= manifest->segment_count || header == NULL)
        return DATA_AEAD_ARGUMENT;
    const struct data_aead_segment *segment = &manifest->segments[index];
    if (physical_bytes != data_aead_physical_size(
            segment->plaintext_bytes)) return DATA_AEAD_FORMAT;
    char path[DATA_AEAD_PATH_MAX + 1U];
    enum data_aead_status status = data_aead_segment_binding(
        manifest->stable_id, index, path);
    if (status != DATA_AEAD_OK) return status;
    uint8_t observed_id[DATA_AEAD_ID_BYTES] = {0};
    uint64_t length = 0U;
    uint64_t generation = 0U;
    status = data_aead_check_header(key, path, header, physical_bytes,
        &length);
    if (status == DATA_AEAD_OK)
        status = data_aead_file_identity(key, path, header,
            physical_bytes, observed_id);
    if (status == DATA_AEAD_OK)
        status = data_aead_generation(key, path, header, physical_bytes,
            &generation);
    if (status == DATA_AEAD_OK &&
            (header[4] != 2U || length != segment->plaintext_bytes ||
             generation != segment->generation ||
             crypto_verify16(observed_id, manifest->stable_id) != 0 ||
             crypto_verify16(header + 20U, segment->revision_id) != 0))
        status = DATA_AEAD_AUTHENTICATION;
    crypto_wipe(observed_id, sizeof(observed_id));
    zero_bytes(path, sizeof(path));
    return status;
}

enum data_aead_status data_aead_manifest_verify_segment(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const struct data_aead_manifest *manifest, unsigned index,
    uint64_t physical_bytes,
    bool (*read_file)(void *context, uint64_t offset, uint8_t *to,
        size_t bytes), void *context, uint8_t *workspace,
    size_t workspace_bytes)
{
    if (key == NULL || !manifest_valid(manifest) ||
            index >= manifest->segment_count || read_file == NULL ||
            workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES)
        return DATA_AEAD_ARGUMENT;
    if (physical_bytes != data_aead_physical_size(
            manifest->segments[index].plaintext_bytes))
        return DATA_AEAD_FORMAT;
    char path[DATA_AEAD_PATH_MAX + 1U];
    enum data_aead_status status = data_aead_segment_binding(
        manifest->stable_id, index, path);
    if (status != DATA_AEAD_OK) return status;
    uint64_t verified_length = 0U;
    status = data_aead_verify_shadow(key, path, physical_bytes,
        read_file, context, workspace, workspace_bytes, &verified_length);
    if (status == DATA_AEAD_OK &&
            verified_length != manifest->segments[index].plaintext_bytes)
        status = DATA_AEAD_AUTHENTICATION;
    if (status == DATA_AEAD_OK) {
        uint8_t header[DATA_AEAD_HEADER_BYTES];
        status = read_file(context, 0U, header, sizeof(header)) ?
            data_aead_manifest_check_segment(key, manifest, index,
                header, physical_bytes) : DATA_AEAD_IO;
        crypto_wipe(header, sizeof(header));
    }
    zero_bytes(path, sizeof(path));
    return status;
}

static bool same_manifest(const struct data_aead_manifest *left,
    const struct data_aead_manifest *right)
{
    if (crypto_verify16(left->stable_id, right->stable_id) != 0 ||
            left->plaintext_bytes != right->plaintext_bytes ||
            left->generation != right->generation ||
            left->segment_count != right->segment_count)
        return false;
    for (unsigned at = 0U; at < left->segment_count; ++at)
        if (crypto_verify16(left->segments[at].revision_id,
                right->segments[at].revision_id) != 0 ||
                left->segments[at].plaintext_bytes !=
                    right->segments[at].plaintext_bytes ||
                left->segments[at].generation !=
                    right->segments[at].generation)
            return false;
    return true;
}

struct publication_segment_reader {
    const struct data_aead_manifest_publish_io *io;
    const uint8_t *revision_id;
};

static bool read_publication_segment(void *context, uint64_t offset,
    uint8_t *to, size_t bytes)
{
    const struct publication_segment_reader *reader = context;
    return reader->io->segment_read(reader->io->context,
        reader->revision_id, offset, to, bytes);
}

static enum data_aead_status verify_publication_segments(
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const struct data_aead_manifest *candidate,
    const struct data_aead_manifest_publish_io *io,
    uint8_t *workspace, size_t workspace_bytes)
{
    for (unsigned at = 0U; at < candidate->segment_count; ++at) {
        const uint8_t *revision_id = candidate->segments[at].revision_id;
        uint64_t size = 0U;
        if (!io->segment_sync(io->context, revision_id) ||
                !io->segment_size(io->context, revision_id, &size))
            return DATA_AEAD_IO;
        struct publication_segment_reader reader = {io, revision_id};
        const enum data_aead_status status = data_aead_manifest_verify_segment(
            key, candidate, at, size, read_publication_segment, &reader,
            workspace, workspace_bytes);
        if (status != DATA_AEAD_OK) return status;
    }
    return DATA_AEAD_OK;
}

static bool publication_io_valid(const struct data_aead_manifest_publish_io *io)
{
    return io != NULL && io->slot_read != NULL &&
        io->slot_remove != NULL && io->slot_sync != NULL &&
        io->temp_write != NULL && io->temp_sync != NULL &&
        io->temp_read != NULL && io->temp_publish != NULL &&
        io->segment_sync != NULL && io->segment_size != NULL &&
        io->segment_read != NULL && io->random != NULL;
}

enum data_aead_status data_aead_manifest_publish(
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_manifest *candidate,
    const struct data_aead_manifest_publish_io *io,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *published, unsigned *slot)
{
    if (candidate == published) return DATA_AEAD_ARGUMENT;
    if (published != NULL) zero_bytes(published, sizeof(*published));
    if (slot != NULL) *slot = 0U;
    if (workspace != NULL)
        crypto_wipe(workspace, workspace_bytes <
            DATA_AEAD_REWRITE_WORKSPACE_BYTES ? workspace_bytes :
            DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    if (key == NULL || path_length(canonical_path) == 0U ||
            !manifest_valid(candidate) || !publication_io_valid(io) ||
            workspace == NULL ||
            workspace_bytes < DATA_AEAD_REWRITE_WORKSPACE_BYTES ||
            published == NULL || slot == NULL)
        return DATA_AEAD_ARGUMENT;
    struct data_aead_manifest_slot_io reader = {io->context, io->slot_read};
    struct data_aead_manifest active = {0};
    uint8_t record[DATA_AEAD_MANIFEST_BYTES] = {0};
    uint8_t readback[DATA_AEAD_MANIFEST_BYTES] = {0};
    unsigned active_slot = 0U;
    enum data_aead_status status = data_aead_manifest_select(key,
        canonical_path, &reader, &active, &active_slot);
    const bool exists = status == DATA_AEAD_OK;
    if (status != DATA_AEAD_OK && status != DATA_AEAD_NOT_FOUND)
        goto done;
    if (exists && same_manifest(&active, candidate)) {
        status = verify_publication_segments(key, candidate, io,
            workspace, workspace_bytes);
        if (status != DATA_AEAD_OK) goto done;
        if (!io->slot_sync(io->context, active_slot)) {
            status = DATA_AEAD_IO;
            goto done;
        }
        status = data_aead_manifest_select(key, canonical_path,
            &reader, published, slot);
        if (status == DATA_AEAD_OK && !same_manifest(published, candidate))
            status = DATA_AEAD_CONFLICT;
        goto done;
    }
    if ((exists && (active.generation == UINT64_MAX ||
            candidate->generation != active.generation + 1U ||
            crypto_verify16(active.stable_id,
                candidate->stable_id) != 0)) ||
            (!exists && candidate->generation != 1U)) {
        status = DATA_AEAD_CONFLICT;
        goto done;
    }
    status = verify_publication_segments(key, candidate, io,
        workspace, workspace_bytes);
    if (status != DATA_AEAD_OK) goto done;
    status = data_aead_manifest_seal(key, canonical_path, candidate,
        io->random, io->context, record);
    if (status != DATA_AEAD_OK) goto done;
    if (!io->temp_write(io->context, record) ||
            !io->temp_sync(io->context) ||
            !io->temp_read(io->context, readback)) {
        status = DATA_AEAD_IO;
        goto done;
    }
    uint8_t difference = 0U;
    for (size_t at = 0U; at < sizeof(record); ++at)
        difference |= record[at] ^ readback[at];
    if (difference != 0U) {
        status = DATA_AEAD_AUTHENTICATION;
        goto done;
    }
    const unsigned next_slot = exists ? active_slot ^ 1U : 0U;
    if (!io->slot_remove(io->context, next_slot) ||
            !io->slot_sync(io->context, next_slot) ||
            !io->temp_publish(io->context, next_slot) ||
            !io->slot_sync(io->context, next_slot)) {
        status = DATA_AEAD_IO;
        goto done;
    }
    status = data_aead_manifest_select(key, canonical_path, &reader,
        published, slot);
    if (status == DATA_AEAD_OK &&
            (!same_manifest(published, candidate) || *slot != next_slot))
        status = DATA_AEAD_CONFLICT;
done:
    if (status != DATA_AEAD_OK) {
        zero_bytes(published, sizeof(*published));
        *slot = 0U;
    }
    zero_bytes(&active, sizeof(active));
    crypto_wipe(record, sizeof(record));
    crypto_wipe(readback, sizeof(readback));
    crypto_wipe(workspace, DATA_AEAD_REWRITE_WORKSPACE_BYTES);
    return status;
}
