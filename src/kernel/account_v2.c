/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/account.h>
#include <openrfs/account_kdf.h>
#include <openrfs/account_v2.h>
#include <openrfs/package_state.h>

#include "../../vendor/monocypher/src/monocypher.h"

/* All integer fields are little endian. Reserved bytes must be zero.
 * 0: magic/version/name length; 8: KDF tuple; 28: generation;
 * 36: salt; 52: padded name; 84: verifier; 116: nonce;
 * 140: wrapped Data key; 172: AEAD tag; 188: SHA-256 corruption screen.
 */
#define V2_HEADER_BYTES 84U
#define V2_SALT_OFFSET 36U
#define V2_NAME_OFFSET 52U
#define V2_VERIFIER_OFFSET 84U
#define V2_NONCE_OFFSET 116U
#define V2_WRAPPED_OFFSET 140U
#define V2_TAG_OFFSET 172U
#define V2_CHECKSUM_OFFSET 188U
#define V2_CHECKSUM_BYTES 32U

static const uint8_t verifier_label[] = "OpenRFS/v2/credential-verifier";
static const uint8_t wrapping_label[] = "OpenRFS/v2/data-key-wrap";

static void copy_bytes(uint8_t *to, const uint8_t *from, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        to[index] = from[index];
    }
}

static void write_u32(uint8_t *to, uint32_t value)
{
    for (size_t index = 0U; index < 4U; ++index) {
        to[index] = (uint8_t)(value >> (8U * index));
    }
}

static uint32_t read_u32(const uint8_t *from)
{
    uint32_t value = 0U;
    for (size_t index = 0U; index < 4U; ++index) {
        value |= (uint32_t)from[index] << (8U * index);
    }
    return value;
}

static void write_u64(uint8_t *to, uint64_t value)
{
    for (size_t index = 0U; index < 8U; ++index) {
        to[index] = (uint8_t)(value >> (8U * index));
    }
}

static uint64_t read_u64(const uint8_t *from)
{
    uint64_t value = 0U;
    for (size_t index = 0U; index < 8U; ++index) {
        value |= (uint64_t)from[index] << (8U * index);
    }
    return value;
}

static size_t name_length(const char *name)
{
    size_t length = 0U;
    if (name == NULL) {
        return ACCOUNT_USERNAME_BYTES;
    }
    while (length < ACCOUNT_USERNAME_BYTES && name[length] != '\0') {
        const char c = name[length];
        const bool alphanumeric = (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if ((!alphanumeric && c != '_' && c != '-') ||
                (length == 0U && !alphanumeric)) {
            return ACCOUNT_USERNAME_BYTES;
        }
        ++length;
    }
    return length == 0U || length >= ACCOUNT_USERNAME_BYTES ?
        ACCOUNT_USERNAME_BYTES : length;
}

static void derive_keys(const uint8_t root[32], uint8_t verifier_key[32],
    uint8_t wrapping_key[32])
{
    crypto_blake2b_keyed(verifier_key, 32U, root, 32U,
        verifier_label, sizeof(verifier_label) - 1U);
    crypto_blake2b_keyed(wrapping_key, 32U, root, 32U,
        wrapping_label, sizeof(wrapping_label) - 1U);
}

static enum account_v2_status derive(const uint8_t record[ACCOUNT_V2_RECORD_BYTES],
    const uint8_t *password, size_t password_bytes, uint8_t verifier_key[32],
    uint8_t wrapping_key[32])
{
    uint8_t root[ACCOUNT_KDF_V2_OUTPUT_BYTES];
    const enum account_kdf_status status = account_kdf_v2_derive(
        record + V2_SALT_OFFSET, password, password_bytes, root);
    if (status != ACCOUNT_KDF_STATUS_OK) {
        crypto_wipe(root, sizeof(root));
        return ACCOUNT_V2_KDF_UNAVAILABLE;
    }
    derive_keys(root, verifier_key, wrapping_key);
    crypto_wipe(root, sizeof(root));
    return ACCOUNT_V2_OK;
}

enum account_v2_status account_v2_validate(
    const uint8_t record[ACCOUNT_V2_RECORD_BYTES])
{
    uint8_t checksum[V2_CHECKSUM_BYTES];
    char name[ACCOUNT_USERNAME_BYTES];
    if (record == NULL || record[0] != 'O' || record[1] != 'R' ||
            record[2] != 'A' || record[3] != '2' || record[4] != 2U ||
            record[5] == 0U || record[5] >= ACCOUNT_USERNAME_BYTES ||
            record[6] != 0U || record[7] != 0U ||
            !account_kdf_v2_parameters_supported(read_u32(record + 8U),
                record[12], read_u32(record + 16U), read_u32(record + 20U),
                read_u32(record + 24U)) ||
            record[13] != 0U || record[14] != 0U || record[15] != 0U ||
            read_u64(record + 28U) == 0U) {
        return ACCOUNT_V2_MALFORMED;
    }
    for (size_t index = 0U; index < ACCOUNT_USERNAME_BYTES; ++index) {
        name[index] = (char)record[V2_NAME_OFFSET + index];
        if (index >= record[5] && name[index] != '\0') {
            return ACCOUNT_V2_MALFORMED;
        }
    }
    if (name_length(name) != record[5]) {
        return ACCOUNT_V2_MALFORMED;
    }
    for (size_t index = V2_CHECKSUM_OFFSET + V2_CHECKSUM_BYTES;
            index < ACCOUNT_V2_RECORD_BYTES; ++index) {
        if (record[index] != 0U) {
            return ACCOUNT_V2_MALFORMED;
        }
    }
    if (package_state_sha256(record, V2_CHECKSUM_OFFSET, checksum) !=
            PACKAGE_STATE_STATUS_OK) {
        return ACCOUNT_V2_MALFORMED;
    }
    const bool correct = crypto_verify32(checksum,
        record + V2_CHECKSUM_OFFSET) == 0;
    crypto_wipe(checksum, sizeof(checksum));
    return correct ? ACCOUNT_V2_OK : ACCOUNT_V2_MALFORMED;
}

enum account_v2_status account_v2_seal(const char *username,
    const uint8_t *password, size_t password_bytes, uint64_t generation,
    const uint8_t salt[16], const uint8_t nonce[ACCOUNT_V2_NONCE_BYTES],
    const uint8_t data_key[ACCOUNT_V2_KEY_BYTES],
    uint8_t record[ACCOUNT_V2_RECORD_BYTES])
{
    uint8_t verifier_key[32] = {0};
    uint8_t wrapping_key[32] = {0};
    const size_t length = name_length(username);
    enum account_v2_status status;

    if (record == NULL || length >= ACCOUNT_USERNAME_BYTES ||
            password == NULL || password_bytes < ACCOUNT_PASSWORD_MIN_BYTES ||
            password_bytes > ACCOUNT_PASSWORD_MAX_BYTES || generation == 0U ||
            salt == NULL || nonce == NULL || data_key == NULL) {
        return ACCOUNT_V2_BAD_ARGUMENT;
    }
    crypto_wipe(record, ACCOUNT_V2_RECORD_BYTES);
    record[0] = 'O'; record[1] = 'R'; record[2] = 'A'; record[3] = '2';
    record[4] = 2U;
    record[5] = (uint8_t)length;
    write_u32(record + 8U, ACCOUNT_KDF_V2_ALGORITHM);
    record[12] = ACCOUNT_KDF_V2_VERSION;
    write_u32(record + 16U, ACCOUNT_KDF_V2_MEMORY_KIB);
    write_u32(record + 20U, ACCOUNT_KDF_V2_PASSES);
    write_u32(record + 24U, ACCOUNT_KDF_V2_LANES);
    write_u64(record + 28U, generation);
    copy_bytes(record + V2_SALT_OFFSET, salt, 16U);
    copy_bytes(record + V2_NAME_OFFSET, (const uint8_t *)username, length);
    copy_bytes(record + V2_NONCE_OFFSET, nonce, ACCOUNT_V2_NONCE_BYTES);

    status = derive(record, password, password_bytes, verifier_key,
        wrapping_key);
    if (status == ACCOUNT_V2_OK) {
        crypto_blake2b_keyed(record + V2_VERIFIER_OFFSET, 32U,
            verifier_key, 32U, record, V2_HEADER_BYTES);
        crypto_aead_lock(record + V2_WRAPPED_OFFSET, record + V2_TAG_OFFSET,
            wrapping_key, record + V2_NONCE_OFFSET, record,
            V2_WRAPPED_OFFSET, data_key, ACCOUNT_V2_KEY_BYTES);
        if (package_state_sha256(record, V2_CHECKSUM_OFFSET,
                record + V2_CHECKSUM_OFFSET) != PACKAGE_STATE_STATUS_OK) {
            status = ACCOUNT_V2_MALFORMED;
        }
    }
    crypto_wipe(verifier_key, sizeof(verifier_key));
    crypto_wipe(wrapping_key, sizeof(wrapping_key));
    if (status != ACCOUNT_V2_OK) {
        crypto_wipe(record, ACCOUNT_V2_RECORD_BYTES);
    }
    return status;
}

enum account_v2_status account_v2_open(
    const uint8_t record[ACCOUNT_V2_RECORD_BYTES], const char *username,
    const uint8_t *password, size_t password_bytes,
    uint8_t data_key[ACCOUNT_V2_KEY_BYTES])
{
    uint8_t verifier_key[32] = {0};
    uint8_t wrapping_key[32] = {0};
    uint8_t verifier[32] = {0};
    enum account_v2_status status;
    if (data_key == NULL) {
        return ACCOUNT_V2_BAD_ARGUMENT;
    }
    crypto_wipe(data_key, ACCOUNT_V2_KEY_BYTES);
    status = account_v2_validate(record);
    if (status != ACCOUNT_V2_OK) {
        return status;
    }
    const size_t length = name_length(username);
    if (password == NULL || length >= ACCOUNT_USERNAME_BYTES ||
            password_bytes < ACCOUNT_PASSWORD_MIN_BYTES ||
            password_bytes > ACCOUNT_PASSWORD_MAX_BYTES ||
            length != record[5]) {
        return ACCOUNT_V2_AUTHENTICATION_FAILED;
    }
    uint8_t difference = 0U;
    for (size_t index = 0U; index < length; ++index) {
        difference |= (uint8_t)username[index] ^ record[V2_NAME_OFFSET + index];
    }
    if (difference != 0U) {
        return ACCOUNT_V2_AUTHENTICATION_FAILED;
    }
    status = derive(record, password, password_bytes, verifier_key,
        wrapping_key);
    if (status == ACCOUNT_V2_OK) {
        crypto_blake2b_keyed(verifier, 32U, verifier_key, 32U, record,
            V2_HEADER_BYTES);
        if (crypto_verify32(verifier, record + V2_VERIFIER_OFFSET) != 0) {
            status = ACCOUNT_V2_AUTHENTICATION_FAILED;
        } else if (crypto_aead_unlock(data_key, record + V2_TAG_OFFSET,
                wrapping_key, record + V2_NONCE_OFFSET, record,
                V2_WRAPPED_OFFSET, record + V2_WRAPPED_OFFSET,
                ACCOUNT_V2_KEY_BYTES) != 0) {
            status = ACCOUNT_V2_MALFORMED;
        }
    }
    crypto_wipe(verifier_key, sizeof(verifier_key));
    crypto_wipe(wrapping_key, sizeof(wrapping_key));
    crypto_wipe(verifier, sizeof(verifier));
    if (status != ACCOUNT_V2_OK) {
        crypto_wipe(data_key, ACCOUNT_V2_KEY_BYTES);
    }
    return status;
}

uint64_t account_v2_generation(
    const uint8_t record[ACCOUNT_V2_RECORD_BYTES])
{
    return record == NULL ? 0U : read_u64(record + 28U);
}
