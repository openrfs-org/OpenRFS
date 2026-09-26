/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_AEAD_H
#define OPENRFS_DATA_AEAD_H

#include <stddef.h>
#include <stdint.h>

#define DATA_AEAD_KEY_BYTES 32U
#define DATA_AEAD_ID_BYTES 16U
#define DATA_AEAD_HEADER_BYTES 96U
#define DATA_AEAD_CHUNK_BYTES 4096U
#define DATA_AEAD_NONCE_BYTES 24U
#define DATA_AEAD_TAG_BYTES 16U
#define DATA_AEAD_SEALED_CHUNK_BYTES 4136U
#define DATA_AEAD_PATH_MAX 255U
#define DATA_AEAD_PHYSICAL_MAX UINT64_C(16777216)

enum data_aead_status {
    DATA_AEAD_OK = 0,
    DATA_AEAD_ARGUMENT,
    DATA_AEAD_FORMAT,
    DATA_AEAD_RANGE,
    DATA_AEAD_AUTHENTICATION,
    DATA_AEAD_IO,
    DATA_AEAD_ENTROPY,
    DATA_AEAD_NOT_FOUND,
    DATA_AEAD_CONFLICT
};

/* The path is canonical; FAT32 uses uppercase. Each revision needs a fresh
 * random revision ID and chunk nonces. Commit a complete shadow before
 * publication. Media rollback needs an external freshness root. */
uint64_t data_aead_physical_size(uint64_t plaintext_bytes);
enum data_aead_status data_aead_make_header(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t plaintext_bytes, const uint8_t file_id[DATA_AEAD_ID_BYTES],
    uint8_t header[DATA_AEAD_HEADER_BYTES]);
enum data_aead_status data_aead_make_header_v2(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t plaintext_bytes,
    const uint8_t stable_id[DATA_AEAD_ID_BYTES],
    const uint8_t revision_id[DATA_AEAD_ID_BYTES],
    uint64_t generation,
    uint8_t header[DATA_AEAD_HEADER_BYTES]);
enum data_aead_status data_aead_check_header(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t physical_bytes,
    uint64_t *plaintext_bytes);
enum data_aead_status data_aead_file_identity(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t physical_bytes,
    uint8_t stable_id[DATA_AEAD_ID_BYTES]);
enum data_aead_status data_aead_generation(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t physical_bytes,
    uint64_t *generation);
enum data_aead_status data_aead_rebind_header(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES], const char *old_path,
    const char *new_path, uint8_t header[DATA_AEAD_HEADER_BYTES],
    uint64_t physical_bytes);
enum data_aead_status data_aead_seal_chunk(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES],
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t index,
    const uint8_t nonce[DATA_AEAD_NONCE_BYTES],
    /* Always 4096 accessible bytes; padding past plaintext_bytes is zero. */
    const uint8_t *plaintext,
    size_t plaintext_bytes,
    uint8_t sealed[DATA_AEAD_SEALED_CHUNK_BYTES]);
enum data_aead_status data_aead_open_chunk(
    const uint8_t data_key[DATA_AEAD_KEY_BYTES],
    const uint8_t header[DATA_AEAD_HEADER_BYTES], uint64_t index,
    const uint8_t sealed[DATA_AEAD_SEALED_CHUNK_BYTES],
    uint8_t plaintext[DATA_AEAD_CHUNK_BYTES], size_t *plaintext_bytes);

#endif
