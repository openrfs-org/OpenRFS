/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead.h>

#include "../../vendor/monocypher/src/monocypher.h"

/* Header: magic/version 0..4, reserved 5..7, chunk size 8..11,
 * plaintext length 12..19, random file ID 20..35, header MAC 36..67,
 * reserved 68..95. All integers are little endian. The MAC binds the
 * canonical path, but the path is not stored in the file. */
#define LENGTH_OFFSET 12U
#define ID_OFFSET 20U
#define MAC_OFFSET 36U
#define RESERVED_OFFSET 68U
#define CHUNK_CIPHERTEXT_OFFSET 24U
#define CHUNK_TAG_OFFSET 4120U

static const uint8_t header_key_label[] = "OpenRFS/v1/data/header-key";
static const uint8_t chunk_key_label[] = "OpenRFS/v1/data/chunk-key";
static const uint8_t chunk_ad_label[] = "OpenRFS/v1/data/chunk-ad";

static void copy_bytes(uint8_t *to, const uint8_t *from, size_t length)
{
    for (size_t index = 0U; index < length; ++index) to[index] = from[index];
}

static void zero_bytes(uint8_t *to, size_t length)
{
    for (size_t index = 0U; index < length; ++index) to[index] = 0U;
}

static void write_u32(uint8_t *to, uint32_t value)
{
    for (size_t index = 0U; index < 4U; ++index)
        to[index] = (uint8_t)(value >> (index * 8U));
}

static uint32_t read_u32(const uint8_t *from)
{
    uint32_t result = 0U;
    for (size_t index = 0U; index < 4U; ++index)
        result |= (uint32_t)from[index] << (index * 8U);
    return result;
}

static void write_u64(uint8_t *to, uint64_t value)
{
    for (size_t index = 0U; index < 8U; ++index)
        to[index] = (uint8_t)(value >> (index * 8U));
}

static uint64_t read_u64(const uint8_t *from)
{
    uint64_t result = 0U;
    for (size_t index = 0U; index < 8U; ++index)
        result |= (uint64_t)from[index] << (index * 8U);
    return result;
}

static size_t path_length(const char *path)
{
    size_t length = 0U;
    size_t component_start = 0U;
    if (path == NULL || path[0] == '/' || path[0] == '\\')
        return 0U;
    while (length <= DATA_AEAD_PATH_MAX && path[length] != '\0') {
        if ((uint8_t)path[length] < 0x20U || path[length] == '\\')
            return 0U;
        if (path[length] == '/') {
            const size_t component_bytes = length - component_start;
            if (component_bytes == 0U ||
                    (component_bytes == 1U && path[component_start] == '.') ||
                    (component_bytes == 2U && path[component_start] == '.' &&
                     path[component_start + 1U] == '.'))
                return 0U;
            component_start = length + 1U;
        }
        ++length;
    }
    const size_t component_bytes = length - component_start;
    if (length > DATA_AEAD_PATH_MAX || component_bytes == 0U ||
            (component_bytes == 1U && path[component_start] == '.') ||
            (component_bytes == 2U && path[component_start] == '.' &&
             path[component_start + 1U] == '.'))
        return 0U;
    return length;
}

static bool valid_id(const uint8_t id[DATA_AEAD_ID_BYTES])
{
    uint8_t combined = 0U;
    for (size_t index = 0U; index < DATA_AEAD_ID_BYTES; ++index)
        combined |= id[index];
    return combined != 0U;
}

uint64_t data_aead_physical_size(uint64_t plaintext_bytes)
{
    const uint64_t chunks = plaintext_bytes / DATA_AEAD_CHUNK_BYTES +
        (plaintext_bytes % DATA_AEAD_CHUNK_BYTES != 0U ? 1U : 0U);
    if (chunks > (DATA_AEAD_PHYSICAL_MAX - DATA_AEAD_HEADER_BYTES) /
            DATA_AEAD_SEALED_CHUNK_BYTES)
        return 0U;
    return DATA_AEAD_HEADER_BYTES + chunks * DATA_AEAD_SEALED_CHUNK_BYTES;
}

static bool shape_valid(const uint8_t header[DATA_AEAD_HEADER_BYTES],
    uint64_t physical_bytes)
{
    if (header[0] != 'O' || header[1] != 'R' || header[2] != 'D' ||
            header[3] != '1' || header[4] != 1U ||
            read_u32(header + 8U) != DATA_AEAD_CHUNK_BYTES ||
            !valid_id(header + ID_OFFSET))
        return false;
    for (size_t index = 5U; index < 8U; ++index)
        if (header[index] != 0U) return false;
    for (size_t index = RESERVED_OFFSET; index < DATA_AEAD_HEADER_BYTES;
            ++index)
        if (header[index] != 0U) return false;
    const uint64_t size = data_aead_physical_size(read_u64(header + LENGTH_OFFSET));
    return size != 0U && size == physical_bytes;
}

static void header_mac(const uint8_t data_key[DATA_AEAD_KEY_BYTES],
    const char *path, size_t path_bytes,
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint8_t mac[32])
{
    uint8_t key[32];
    uint8_t length[2] = {(uint8_t)path_bytes, (uint8_t)(path_bytes >> 8U)};
    crypto_blake2b_keyed(key, sizeof(key), data_key, DATA_AEAD_KEY_BYTES,
        header_key_label, sizeof(header_key_label) - 1U);
    crypto_blake2b_ctx context;
    crypto_blake2b_keyed_init(&context, sizeof(key), key, sizeof(key));
    crypto_blake2b_update(&context, header, MAC_OFFSET);
    crypto_blake2b_update(&context, length, sizeof(length));
    crypto_blake2b_update(&context, (const uint8_t *)path, path_bytes);
    crypto_blake2b_final(&context, mac);
    crypto_wipe(&context, sizeof(context));
    crypto_wipe(key, sizeof(key));
}

enum data_aead_status data_aead_make_header(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t plaintext_bytes, const uint8_t file_id[DATA_AEAD_ID_BYTES],
    uint8_t header[DATA_AEAD_HEADER_BYTES])
{
    if (header == NULL) return DATA_AEAD_ARGUMENT;
    zero_bytes(header, DATA_AEAD_HEADER_BYTES);
    const size_t path_bytes = path_length(canonical_path);
    if (data_key == NULL || path_bytes == 0U || file_id == NULL ||
            !valid_id(file_id) || data_aead_physical_size(plaintext_bytes) == 0U)
        return DATA_AEAD_ARGUMENT;
    header[0] = 'O'; header[1] = 'R'; header[2] = 'D'; header[3] = '1';
    header[4] = 1U;
    write_u32(header + 8U, DATA_AEAD_CHUNK_BYTES);
    write_u64(header + LENGTH_OFFSET, plaintext_bytes);
    copy_bytes(header + ID_OFFSET, file_id, DATA_AEAD_ID_BYTES);
    header_mac(data_key, canonical_path, path_bytes, header,
        header + MAC_OFFSET);
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_check_header(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t physical_bytes,
    uint64_t *plaintext_bytes)
{
    if (plaintext_bytes != NULL) *plaintext_bytes = 0U;
    const size_t path_bytes = path_length(canonical_path);
    if (data_key == NULL || header == NULL || plaintext_bytes == NULL ||
            path_bytes == 0U)
        return DATA_AEAD_ARGUMENT;
    if (!shape_valid(header, physical_bytes)) return DATA_AEAD_FORMAT;
    uint8_t mac[32];
    header_mac(data_key, canonical_path, path_bytes, header, mac);
    const bool valid = crypto_verify32(mac, header + MAC_OFFSET) == 0;
    crypto_wipe(mac, sizeof(mac));
    if (!valid) return DATA_AEAD_AUTHENTICATION;
    *plaintext_bytes = read_u64(header + LENGTH_OFFSET);
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_rebind_header(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES], const char *old_path,
    const char *new_path, uint8_t header[DATA_AEAD_HEADER_BYTES],
    uint64_t physical_bytes)
{
    uint64_t length = 0U;
    enum data_aead_status status = data_aead_check_header(data_key, old_path,
        header, physical_bytes, &length);
    if (status != DATA_AEAD_OK) return status;
    const size_t new_length = path_length(new_path);
    if (new_length == 0U) return DATA_AEAD_ARGUMENT;
    header_mac(data_key, new_path, new_length, header, header + MAC_OFFSET);
    return DATA_AEAD_OK;
}

static size_t chunk_length(const uint8_t header[DATA_AEAD_HEADER_BYTES],
    uint64_t index)
{
    const uint64_t length = read_u64(header + LENGTH_OFFSET);
    if (index >= (length / DATA_AEAD_CHUNK_BYTES +
            (length % DATA_AEAD_CHUNK_BYTES != 0U ? 1U : 0U))) return 0U;
    const uint64_t remaining = length - index * DATA_AEAD_CHUNK_BYTES;
    return remaining >= DATA_AEAD_CHUNK_BYTES ? DATA_AEAD_CHUNK_BYTES :
        (size_t)remaining;
}

static void chunk_key(const uint8_t data_key[DATA_AEAD_KEY_BYTES],
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint8_t key[32])
{
    uint8_t message[sizeof(chunk_key_label) - 1U + DATA_AEAD_ID_BYTES];
    copy_bytes(message, chunk_key_label, sizeof(chunk_key_label) - 1U);
    copy_bytes(message + sizeof(chunk_key_label) - 1U, header + ID_OFFSET,
        DATA_AEAD_ID_BYTES);
    crypto_blake2b_keyed(key, 32U, data_key, DATA_AEAD_KEY_BYTES,
        message, sizeof(message));
    crypto_wipe(message, sizeof(message));
}

static size_t chunk_ad(const uint8_t header[DATA_AEAD_HEADER_BYTES],
    uint64_t index, size_t length, uint8_t ad[64])
{
    size_t used = sizeof(chunk_ad_label) - 1U;
    copy_bytes(ad, chunk_ad_label, used);
    copy_bytes(ad + used, header + ID_OFFSET, DATA_AEAD_ID_BYTES);
    used += DATA_AEAD_ID_BYTES;
    write_u64(ad + used, index); used += 8U;
    write_u32(ad + used, (uint32_t)length); used += 4U;
    return used;
}

enum data_aead_status data_aead_seal_chunk(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES],
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t index,
    const uint8_t nonce[DATA_AEAD_NONCE_BYTES], const uint8_t *plaintext,
    size_t plaintext_bytes,
    uint8_t sealed[DATA_AEAD_SEALED_CHUNK_BYTES])
{
    if (sealed == NULL) return DATA_AEAD_ARGUMENT;
    zero_bytes(sealed, DATA_AEAD_SEALED_CHUNK_BYTES);
    if (data_key == NULL || header == NULL || nonce == NULL ||
            plaintext == NULL || plaintext_bytes == 0U ||
            plaintext_bytes != chunk_length(header, index))
        return DATA_AEAD_RANGE;
    uint8_t key[32];
    uint8_t ad[64];
    uint8_t padding = 0U;
    for (size_t cursor = plaintext_bytes; cursor < DATA_AEAD_CHUNK_BYTES;
            ++cursor)
        padding |= plaintext[cursor];
    if (padding != 0U) return DATA_AEAD_FORMAT;
    chunk_key(data_key, header, key);
    const size_t ad_length = chunk_ad(header, index, plaintext_bytes, ad);
    copy_bytes(sealed, nonce, DATA_AEAD_NONCE_BYTES);
    crypto_aead_lock(sealed + CHUNK_CIPHERTEXT_OFFSET,
        sealed + CHUNK_TAG_OFFSET, key, nonce, ad, ad_length,
        plaintext, DATA_AEAD_CHUNK_BYTES);
    crypto_wipe(key, sizeof(key));
    crypto_wipe(ad, sizeof(ad));
    return DATA_AEAD_OK;
}

enum data_aead_status data_aead_open_chunk(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES],
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t index,
    const uint8_t sealed[DATA_AEAD_SEALED_CHUNK_BYTES],
    uint8_t plaintext[DATA_AEAD_CHUNK_BYTES], size_t *plaintext_bytes)
{
    if (plaintext_bytes != NULL) *plaintext_bytes = 0U;
    if (plaintext != NULL) zero_bytes(plaintext, DATA_AEAD_CHUNK_BYTES);
    if (data_key == NULL || header == NULL || sealed == NULL ||
            plaintext == NULL || plaintext_bytes == NULL)
        return DATA_AEAD_ARGUMENT;
    const size_t length = chunk_length(header, index);
    if (length == 0U) return DATA_AEAD_RANGE;
    uint8_t key[32];
    uint8_t ad[64];
    chunk_key(data_key, header, key);
    const size_t ad_length = chunk_ad(header, index, length, ad);
    const int opened = crypto_aead_unlock(plaintext,
        sealed + CHUNK_TAG_OFFSET, key, sealed, ad, ad_length,
        sealed + CHUNK_CIPHERTEXT_OFFSET, DATA_AEAD_CHUNK_BYTES);
    crypto_wipe(key, sizeof(key));
    crypto_wipe(ad, sizeof(ad));
    if (opened != 0) {
        crypto_wipe(plaintext, DATA_AEAD_CHUNK_BYTES);
        return DATA_AEAD_AUTHENTICATION;
    }
    uint8_t padding = 0U;
    for (size_t cursor = length; cursor < DATA_AEAD_CHUNK_BYTES; ++cursor)
        padding |= plaintext[cursor];
    if (padding != 0U) {
        crypto_wipe(plaintext, DATA_AEAD_CHUNK_BYTES);
        return DATA_AEAD_FORMAT;
    }
    *plaintext_bytes = length;
    return DATA_AEAD_OK;
}
