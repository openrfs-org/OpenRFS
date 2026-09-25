/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openrfs/account_kdf.h>
#include <openrfs/account_v2.h>
#include <openrfs/package_state.h>

#include "../vendor/monocypher/src/monocypher.h"

/* Record-format tests use a cheap deterministic KDF substitute. The real
 * bounded Argon2id implementation and its published vectors are exercised by
 * account-kdf-host-test and the account-kdf QEMU scenario. */
static bool refuse_kdf;
static unsigned int kdf_calls;

bool account_kdf_v2_parameters_supported(uint32_t algorithm, uint8_t version,
    uint32_t memory_kib, uint32_t passes, uint32_t lanes)
{
    return algorithm == ACCOUNT_KDF_V2_ALGORITHM &&
        version == ACCOUNT_KDF_V2_VERSION &&
        memory_kib == ACCOUNT_KDF_V2_MEMORY_KIB &&
        passes == ACCOUNT_KDF_V2_PASSES && lanes == ACCOUNT_KDF_V2_LANES;
}

enum account_kdf_status account_kdf_v2_derive(const uint8_t salt[16],
    const uint8_t *password, size_t password_bytes, uint8_t output[32])
{
    uint8_t input[16U + 64U] = {0};
    ++kdf_calls;
    if (refuse_kdf) {
        return ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE;
    }
    memcpy(input, salt, 16U);
    memcpy(input + 16U, password, password_bytes);
    crypto_blake2b(output, 32U, input, sizeof(input));
    crypto_wipe(input, sizeof(input));
    return ACCOUNT_KDF_STATUS_OK;
}

static void recalculate_checksum(uint8_t record[ACCOUNT_V2_RECORD_BYTES])
{
    assert(package_state_sha256(record, 188U, record + 188U) ==
        PACKAGE_STATE_STATUS_OK);
}

int main(void)
{
    static const uint8_t password[] = "correct horse";
    static const uint8_t wrong[] = "correct house";
    uint8_t salt[16];
    uint8_t nonce[ACCOUNT_V2_NONCE_BYTES];
    uint8_t data_key[ACCOUNT_V2_KEY_BYTES];
    uint8_t recovered[ACCOUNT_V2_KEY_BYTES];
    uint8_t record[ACCOUNT_V2_RECORD_BYTES];
    uint8_t altered[ACCOUNT_V2_RECORD_BYTES];
    for (size_t index = 0U; index < sizeof(salt); ++index) {
        salt[index] = (uint8_t)(index + 1U);
    }
    for (size_t index = 0U; index < sizeof(nonce); ++index) {
        nonce[index] = (uint8_t)(index + 21U);
    }
    for (size_t index = 0U; index < sizeof(data_key); ++index) {
        data_key[index] = (uint8_t)(index + 61U);
    }
    assert(account_v2_seal("alice", password, sizeof(password) - 1U, 7U,
        salt, nonce, data_key, record) == ACCOUNT_V2_OK);
    assert(account_v2_validate(record) == ACCOUNT_V2_OK);
    assert(account_v2_generation(record) == 7U);
    assert(account_v2_open(record, "alice", password, sizeof(password) - 1U,
        recovered) == ACCOUNT_V2_OK);
    assert(memcmp(recovered, data_key, sizeof(data_key)) == 0);

    memset(recovered, 0xa5, sizeof(recovered));
    assert(account_v2_open(record, "alice", wrong, sizeof(wrong) - 1U,
        recovered) == ACCOUNT_V2_AUTHENTICATION_FAILED);
    for (size_t index = 0U; index < sizeof(recovered); ++index) {
        assert(recovered[index] == 0U);
    }
    assert(account_v2_open(record, "bob", password, sizeof(password) - 1U,
        recovered) == ACCOUNT_V2_AUTHENTICATION_FAILED);

    uint8_t flags = 0U;
    assert(account_v2_record_flags(record, &flags) == ACCOUNT_V2_OK &&
        flags == 0U);
    assert(account_v2_seal_flags("alice", password, sizeof(password) - 1U,
        8U, ACCOUNT_V2_FLAG_DATA_ENCRYPTED, salt, nonce, data_key,
        altered) == ACCOUNT_V2_OK);
    assert(account_v2_record_flags(altered, &flags) == ACCOUNT_V2_OK &&
        flags == ACCOUNT_V2_FLAG_DATA_ENCRYPTED);
    assert(account_v2_open(altered, "alice", password,
        sizeof(password) - 1U, recovered) == ACCOUNT_V2_OK &&
        memcmp(recovered, data_key, sizeof(data_key)) == 0);
    altered[6] = 0U;
    recalculate_checksum(altered);
    assert(account_v2_open(altered, "alice", password,
        sizeof(password) - 1U, recovered) ==
        ACCOUNT_V2_AUTHENTICATION_FAILED);
    assert(account_v2_seal_flags("alice", password, sizeof(password) - 1U,
        8U, ACCOUNT_V2_FLAG_DATA_MIGRATING, salt, nonce, data_key,
        altered) == ACCOUNT_V2_OK);
    assert(account_v2_record_flags(altered, &flags) == ACCOUNT_V2_OK &&
        flags == ACCOUNT_V2_FLAG_DATA_MIGRATING);
    assert(account_v2_open(altered, "alice", password,
        sizeof(password) - 1U, recovered) == ACCOUNT_V2_OK &&
        memcmp(recovered, data_key, sizeof(data_key)) == 0);
    altered[6] = 3U;
    recalculate_checksum(altered);
    kdf_calls = 0U;
    assert(account_v2_open(altered, "alice", password,
        sizeof(password) - 1U, recovered) == ACCOUNT_V2_MALFORMED &&
        kdf_calls == 0U);
    assert(account_v2_seal_flags("alice", password, sizeof(password) - 1U,
        8U, 3U, salt, nonce, data_key, altered) == ACCOUNT_V2_BAD_ARGUMENT);

    memcpy(altered, record, sizeof(record));
    altered[16] = 1U; /* unsupported work factor must not start the KDF */
    recalculate_checksum(altered);
    kdf_calls = 0U;
    assert(account_v2_open(altered, "alice", password,
        sizeof(password) - 1U, recovered) == ACCOUNT_V2_MALFORMED);
    assert(kdf_calls == 0U);

    memcpy(altered, record, sizeof(record));
    altered[53] = 0U; /* noncanonical username */
    recalculate_checksum(altered);
    assert(account_v2_validate(altered) == ACCOUNT_V2_MALFORMED);
    memcpy(altered, record, sizeof(record));
    altered[60] = 'x'; /* nonzero username padding */
    recalculate_checksum(altered);
    assert(account_v2_validate(altered) == ACCOUNT_V2_MALFORMED);

    memcpy(altered, record, sizeof(record));
    altered[28] ^= 1U; /* generation is bound to the verifier and wrap */
    recalculate_checksum(altered);
    assert(account_v2_open(altered, "alice", password,
        sizeof(password) - 1U, recovered) ==
        ACCOUNT_V2_AUTHENTICATION_FAILED);
    memcpy(altered, record, sizeof(record));
    altered[140] ^= 1U; /* checksum is not the AEAD tag */
    recalculate_checksum(altered);
    assert(account_v2_open(altered, "alice", password,
        sizeof(password) - 1U, recovered) == ACCOUNT_V2_MALFORMED);
    memcpy(altered, record, sizeof(record));
    altered[220] = 1U;
    assert(account_v2_validate(altered) == ACCOUNT_V2_MALFORMED);

    refuse_kdf = true;
    assert(account_v2_open(record, "alice", password,
        sizeof(password) - 1U, recovered) == ACCOUNT_V2_KDF_UNAVAILABLE);
    puts("account v2 record, parameter bounds, domain separation and AEAD controls passed");
    return 0;
}
