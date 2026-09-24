/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openrfs/data_aead.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #expression); \
        abort(); \
    } \
} while (0)

static void expect_zero(const uint8_t *data, size_t length)
{
    for (size_t index = 0U; index < length; ++index) CHECK(data[index] == 0U);
}

int main(void)
{
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t wrong_key[DATA_AEAD_KEY_BYTES];
    uint8_t id[DATA_AEAD_ID_BYTES];
    uint8_t other_id[DATA_AEAD_ID_BYTES];
    uint8_t nonce[DATA_AEAD_NONCE_BYTES];
    uint8_t header[DATA_AEAD_HEADER_BYTES];
    uint8_t other_header[DATA_AEAD_HEADER_BYTES];
    uint8_t sealed[DATA_AEAD_SEALED_CHUNK_BYTES];
    uint8_t changed[DATA_AEAD_SEALED_CHUNK_BYTES];
    uint8_t opened[DATA_AEAD_CHUNK_BYTES];
    uint8_t plain[DATA_AEAD_CHUNK_BYTES];
    size_t opened_bytes = 0U;
    uint64_t file_bytes = 0U;

    for (size_t index = 0U; index < sizeof(key); ++index) {
        key[index] = (uint8_t)index;
        wrong_key[index] = (uint8_t)(index ^ 0x55U);
    }
    for (size_t index = 0U; index < sizeof(id); ++index) {
        id[index] = (uint8_t)(index + 1U);
        other_id[index] = (uint8_t)(index + 16U);
    }
    for (size_t index = 0U; index < sizeof(nonce); ++index)
        nonce[index] = (uint8_t)(index + 33U);
    for (size_t index = 0U; index < sizeof(plain); ++index)
        plain[index] = (uint8_t)(index % 251U);

    CHECK(data_aead_physical_size(0U) == DATA_AEAD_HEADER_BYTES);
    CHECK(data_aead_physical_size(4097U) ==
        DATA_AEAD_HEADER_BYTES + 2U * DATA_AEAD_SEALED_CHUNK_BYTES);
    CHECK(data_aead_physical_size(UINT64_MAX) == 0U);
    CHECK(data_aead_make_header(key, "HOME//NOTE", 0U, id, header) ==
        DATA_AEAD_ARGUMENT);
    CHECK(data_aead_make_header(key, "HOME/./NOTE", 0U, id, header) ==
        DATA_AEAD_ARGUMENT);
    CHECK(data_aead_make_header(key, "HOME/../NOTE", 0U, id, header) ==
        DATA_AEAD_ARGUMENT);
    CHECK(data_aead_make_header(key, "HOME/NOTE/", 0U, id, header) ==
        DATA_AEAD_ARGUMENT);
    CHECK(data_aead_make_header(key, "HOME/NOTE.TXT", 4097U, id, header) ==
        DATA_AEAD_OK);
    CHECK(data_aead_check_header(key, "HOME/NOTE.TXT", header,
        data_aead_physical_size(4097U), &file_bytes) == DATA_AEAD_OK);
    CHECK(file_bytes == 4097U);
    CHECK(data_aead_check_header(key, "HOME/OTHER.TXT", header,
        data_aead_physical_size(4097U), &file_bytes) ==
        DATA_AEAD_AUTHENTICATION);
    CHECK(file_bytes == 0U);
    CHECK(data_aead_check_header(wrong_key, "HOME/NOTE.TXT", header,
        data_aead_physical_size(4097U), &file_bytes) ==
        DATA_AEAD_AUTHENTICATION);
    CHECK(data_aead_check_header(key, "HOME/NOTE.TXT", header,
        data_aead_physical_size(4097U) - 1U, &file_bytes) == DATA_AEAD_FORMAT);
    CHECK(data_aead_check_header(key, "HOME/NOTE.TXT", header,
        data_aead_physical_size(4097U) + 1U, &file_bytes) == DATA_AEAD_FORMAT);
    memcpy(other_header, header, sizeof(header));
    other_header[5] = 1U;
    CHECK(data_aead_check_header(key, "HOME/NOTE.TXT", other_header,
        data_aead_physical_size(4097U), &file_bytes) == DATA_AEAD_FORMAT);
    memcpy(other_header, header, sizeof(header));
    other_header[12] ^= 1U;
    CHECK(data_aead_check_header(key, "HOME/NOTE.TXT", other_header,
        data_aead_physical_size(4097U), &file_bytes) == DATA_AEAD_FORMAT);

    CHECK(data_aead_seal_chunk(key, header, 0U, nonce, plain,
        DATA_AEAD_CHUNK_BYTES, sealed) == DATA_AEAD_OK);
    CHECK(memcmp(sealed + DATA_AEAD_NONCE_BYTES, plain, sizeof(plain)) != 0);
    CHECK(data_aead_open_chunk(key, header, 0U, sealed, opened,
        &opened_bytes) == DATA_AEAD_OK);
    CHECK(opened_bytes == DATA_AEAD_CHUNK_BYTES);
    CHECK(memcmp(opened, plain, sizeof(plain)) == 0);
    memcpy(changed, sealed, sizeof(changed));
    changed[DATA_AEAD_NONCE_BYTES + 5U] ^= 1U;
    CHECK(data_aead_open_chunk(key, header, 0U, changed, opened,
        &opened_bytes) == DATA_AEAD_AUTHENTICATION);
    CHECK(opened_bytes == 0U);
    expect_zero(opened, sizeof(opened));
    changed[DATA_AEAD_NONCE_BYTES + 5U] ^= 1U;
    changed[DATA_AEAD_SEALED_CHUNK_BYTES - 1U] ^= 1U;
    CHECK(data_aead_open_chunk(key, header, 0U, changed, opened,
        &opened_bytes) == DATA_AEAD_AUTHENTICATION);
    CHECK(data_aead_open_chunk(wrong_key, header, 0U, sealed, opened,
        &opened_bytes) == DATA_AEAD_AUTHENTICATION);
    CHECK(data_aead_open_chunk(key, header, 1U, sealed, opened,
        &opened_bytes) == DATA_AEAD_AUTHENTICATION);

    memset(plain + 1U, 0, sizeof(plain) - 1U);
    CHECK(data_aead_seal_chunk(key, header, 1U, nonce, plain, 1U, sealed) ==
        DATA_AEAD_OK);
    CHECK(data_aead_open_chunk(key, header, 1U, sealed, opened,
        &opened_bytes) == DATA_AEAD_OK);
    CHECK(opened_bytes == 1U && opened[0] == plain[0]);
    expect_zero(opened + 1U, sizeof(opened) - 1U);
    CHECK(data_aead_seal_chunk(key, header, 1U, nonce, plain, 2U, changed) ==
        DATA_AEAD_RANGE);
    expect_zero(changed, sizeof(changed));

    CHECK(data_aead_make_header(key, "HOME/NOTE.TXT", 4097U, other_id,
        other_header) == DATA_AEAD_OK);
    CHECK(data_aead_open_chunk(key, other_header, 1U, sealed, opened,
        &opened_bytes) == DATA_AEAD_AUTHENTICATION);
    CHECK(data_aead_rebind_header(key, "HOME/NOTE.TXT", "HOME/NEW.TXT",
        header, data_aead_physical_size(4097U)) == DATA_AEAD_OK);
    CHECK(data_aead_check_header(key, "HOME/NEW.TXT", header,
        data_aead_physical_size(4097U), &file_bytes) == DATA_AEAD_OK);
    CHECK(data_aead_check_header(key, "HOME/NOTE.TXT", header,
        data_aead_physical_size(4097U), &file_bytes) ==
        DATA_AEAD_AUTHENTICATION);
    CHECK(data_aead_open_chunk(key, header, 1U, sealed, opened,
        &opened_bytes) == DATA_AEAD_OK);

    CHECK(data_aead_make_header(key, "HOME/EMPTY", 0U, id, header) ==
        DATA_AEAD_OK);
    CHECK(data_aead_check_header(key, "HOME/EMPTY", header,
        DATA_AEAD_HEADER_BYTES, &file_bytes) == DATA_AEAD_OK);
    CHECK(file_bytes == 0U);
    CHECK(data_aead_open_chunk(key, header, 0U, sealed, opened,
        &opened_bytes) == DATA_AEAD_RANGE);

    puts("data AEAD parser, key/path binding, chunk and tamper controls passed");
    return 0;
}
