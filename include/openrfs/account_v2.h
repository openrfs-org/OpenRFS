/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_ACCOUNT_V2_H
#define OPENRFS_ACCOUNT_V2_H

#include <stddef.h>
#include <stdint.h>

#define ACCOUNT_V2_RECORD_BYTES 224U
#define ACCOUNT_V2_KEY_BYTES 32U
#define ACCOUNT_V2_NONCE_BYTES 24U

enum account_v2_status {
    ACCOUNT_V2_OK = 0,
    ACCOUNT_V2_BAD_ARGUMENT,
    ACCOUNT_V2_MALFORMED,
    ACCOUNT_V2_AUTHENTICATION_FAILED,
    ACCOUNT_V2_KDF_UNAVAILABLE
};

/* The record checksum is only a cheap corruption screen. The password-derived
 * verifier and AEAD tag authenticate the header and wrapped Data key. */
enum account_v2_status account_v2_validate(
    const uint8_t record[ACCOUNT_V2_RECORD_BYTES]);
enum account_v2_status account_v2_seal(
    const char *username, const uint8_t *password, size_t password_bytes,
    uint64_t generation, const uint8_t salt[16],
    const uint8_t nonce[ACCOUNT_V2_NONCE_BYTES],
    const uint8_t data_key[ACCOUNT_V2_KEY_BYTES],
    uint8_t record[ACCOUNT_V2_RECORD_BYTES]);
enum account_v2_status account_v2_open(
    const uint8_t record[ACCOUNT_V2_RECORD_BYTES],
    const char *username, const uint8_t *password, size_t password_bytes,
    uint8_t data_key[ACCOUNT_V2_KEY_BYTES]);
uint64_t account_v2_generation(
    const uint8_t record[ACCOUNT_V2_RECORD_BYTES]);

#endif
