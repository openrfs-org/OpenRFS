/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openrfs/data_aead_rewrite.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #expression); \
        abort(); \
    } \
} while (0)

#define FILE_CAPACITY 30000U

struct memory_file {
    uint8_t bytes[FILE_CAPACITY];
    size_t length;
};

struct memory_io {
    struct memory_file *old;
    struct memory_file *shadow;
    size_t write_limit;
    unsigned int random_calls;
    unsigned int fail_random_at;
};

static struct memory_file old_file;
static struct memory_file shadow_file;
static struct memory_file saved_file;
static uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
static uint8_t expected[8193U];

static bool begin_shadow(void *context, uint64_t physical_bytes)
{
    struct memory_io *io = context;
    if (physical_bytes > FILE_CAPACITY) return false;
    memset(io->shadow, 0, sizeof(*io->shadow));
    return true;
}

static bool read_old(void *context, uint64_t offset, uint8_t *to,
    size_t bytes)
{
    const struct memory_io *io = context;
    if (offset > io->old->length || bytes > io->old->length - (size_t)offset)
        return false;
    memcpy(to, io->old->bytes + (size_t)offset, bytes);
    return true;
}

static bool read_shadow(void *context, uint64_t offset, uint8_t *to,
    size_t bytes)
{
    const struct memory_io *io = context;
    if (offset > io->shadow->length ||
            bytes > io->shadow->length - (size_t)offset)
        return false;
    memcpy(to, io->shadow->bytes + (size_t)offset, bytes);
    return true;
}

static bool write_shadow(void *context, uint64_t offset,
    const uint8_t *from, size_t bytes)
{
    struct memory_io *io = context;
    if (offset > io->write_limit || bytes > io->write_limit - (size_t)offset)
        return false;
    memcpy(io->shadow->bytes + (size_t)offset, from, bytes);
    if (io->shadow->length < (size_t)offset + bytes)
        io->shadow->length = (size_t)offset + bytes;
    return true;
}

static bool random_bytes(void *context, uint8_t *to, size_t bytes)
{
    struct memory_io *io = context;
    ++io->random_calls;
    if (io->random_calls == io->fail_random_at) return false;
    for (size_t index = 0U; index < bytes; ++index)
        to[index] = (uint8_t)(io->random_calls + index);
    return true;
}

static void expect_wiped(void)
{
    for (size_t index = 0U; index < sizeof(workspace); ++index)
        CHECK(workspace[index] == 0U);
}

static void check_plaintext(const uint8_t key[32],
    const struct memory_file *file, const uint8_t *want, size_t want_bytes)
{
    uint8_t header[DATA_AEAD_HEADER_BYTES];
    uint8_t plain[DATA_AEAD_CHUNK_BYTES];
    uint64_t decoded = 0U;
    memcpy(header, file->bytes, sizeof(header));
    CHECK(file->length == data_aead_physical_size(want_bytes));
    CHECK(data_aead_check_header(key, "HOME/NOTE.TXT", header,
        file->length, &decoded) == DATA_AEAD_OK);
    CHECK(decoded == want_bytes);
    const uint64_t count = (want_bytes + DATA_AEAD_CHUNK_BYTES - 1U) /
        DATA_AEAD_CHUNK_BYTES;
    for (uint64_t index = 0U; index < count; ++index) {
        size_t opened = 0U;
        const uint64_t offset = DATA_AEAD_HEADER_BYTES +
            index * DATA_AEAD_SEALED_CHUNK_BYTES;
        CHECK(data_aead_open_chunk(key, header, index, file->bytes + offset,
            plain, &opened) == DATA_AEAD_OK);
        const size_t start = (size_t)index * DATA_AEAD_CHUNK_BYTES;
        const size_t remaining = want_bytes - start;
        const size_t length = remaining < DATA_AEAD_CHUNK_BYTES ? remaining :
            DATA_AEAD_CHUNK_BYTES;
        CHECK(opened == length);
        CHECK(memcmp(plain, want + start, length) == 0);
    }
}

static void publish_shadow(struct memory_io *io)
{
    *io->old = *io->shadow;
    memset(io->shadow, 0, sizeof(*io->shadow));
}

int main(void)
{
    uint8_t key[32];
    uint8_t wrong_key[32];
    uint8_t first_id[DATA_AEAD_ID_BYTES];
    uint8_t stable_id[DATA_AEAD_ID_BYTES];
    uint8_t rewritten_id[DATA_AEAD_ID_BYTES];
    uint64_t produced = 0U;
    struct memory_io io = {&old_file, &shadow_file, FILE_CAPACITY, 0U, 0U};
    const struct data_aead_rewrite_io callbacks = {
        &io, begin_shadow, read_old, write_shadow, random_bytes
    };
    for (size_t index = 0U; index < sizeof(key); ++index) {
        key[index] = (uint8_t)(index + 7U);
        wrong_key[index] = (uint8_t)(index + 37U);
    }

    /* Even an empty file has a keyed, path-bound header on disk. */
    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", 0U, 0U,
        0U, NULL, 0U, &callbacks, workspace, sizeof(workspace),
        &produced) == DATA_AEAD_OK);
    CHECK(produced == DATA_AEAD_HEADER_BYTES);
    check_plaintext(key, &shadow_file, expected, 0U);
    expect_wiped();
    publish_shadow(&io);

    /* A sparse write crosses a chunk boundary; untouched gaps are
     * authenticated zeros, and the physical file is a complete shadow. */
    const uint8_t patch[] = {'A', 'B', 'C', 'D', 'E'};
    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", old_file.length,
        5000U,
        4094U, patch, sizeof(patch), &callbacks, workspace,
        sizeof(workspace), &produced) == DATA_AEAD_OK);
    CHECK(produced == shadow_file.length);
    memcpy(expected + 4094U, patch, sizeof(patch));
    check_plaintext(key, &shadow_file, expected, 5000U);
    uint64_t verified = 0U;
    CHECK(data_aead_verify_shadow(key, "HOME/NOTE.TXT", produced,
        read_shadow, &io, workspace, sizeof(workspace), &verified) ==
        DATA_AEAD_OK && verified == 5000U);
    expect_wiped();
    uint8_t range[16];
    size_t range_bytes = 0U;
    memset(range, 0xa5, sizeof(range));
    CHECK(data_aead_read_range(key, "HOME/NOTE.TXT", produced, 4092U,
        range, sizeof(range), read_shadow, &io, workspace,
        sizeof(workspace), &range_bytes) == DATA_AEAD_OK &&
        range_bytes == sizeof(range) &&
        memcmp(range, expected + 4092U, sizeof(range)) == 0);
    CHECK(data_aead_read_range(key, "HOME/NOTE.TXT", produced, 4998U,
        range, sizeof(range), read_shadow, &io, workspace,
        sizeof(workspace), &range_bytes) == DATA_AEAD_OK &&
        range_bytes == 2U && memcmp(range, expected + 4998U, 2U) == 0);
    CHECK(data_aead_read_range(key, "HOME/NOTE.TXT", produced, 5000U,
        range, sizeof(range), read_shadow, &io, workspace,
        sizeof(workspace), &range_bytes) == DATA_AEAD_OK &&
        range_bytes == 0U);
    CHECK(data_aead_read_range(key, "HOME/WRONG.TXT", produced, 0U,
        range, sizeof(range), read_shadow, &io, workspace,
        sizeof(workspace), &range_bytes) == DATA_AEAD_AUTHENTICATION &&
        range_bytes == 0U);
    shadow_file.bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_NONCE_BYTES] ^= 1U;
    CHECK(data_aead_verify_shadow(key, "HOME/NOTE.TXT", produced,
        read_shadow, &io, workspace, sizeof(workspace), &verified) ==
        DATA_AEAD_AUTHENTICATION && verified == 0U);
    expect_wiped();
    shadow_file.bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_NONCE_BYTES] ^= 1U;
    const size_t second_chunk = DATA_AEAD_HEADER_BYTES +
        DATA_AEAD_SEALED_CHUNK_BYTES + DATA_AEAD_NONCE_BYTES;
    shadow_file.bytes[second_chunk] ^= 1U;
    memset(range, 0xa5, sizeof(range));
    CHECK(data_aead_read_range(key, "HOME/NOTE.TXT", produced, 4092U,
        range, sizeof(range), read_shadow, &io, workspace,
        sizeof(workspace), &range_bytes) == DATA_AEAD_AUTHENTICATION &&
        range_bytes == 0U);
    for (size_t index = 0U; index < 4U; ++index) CHECK(range[index] == 0U);
    shadow_file.bytes[second_chunk] ^= 1U;
    CHECK(data_aead_verify_shadow(wrong_key, "HOME/NOTE.TXT", produced,
        read_shadow, &io, workspace, sizeof(workspace), &verified) ==
        DATA_AEAD_AUTHENTICATION && verified == 0U);
    CHECK(data_aead_verify_shadow(key, "HOME/OTHER.TXT", produced,
        read_shadow, &io, workspace, sizeof(workspace), &verified) ==
        DATA_AEAD_AUTHENTICATION && verified == 0U);
    --shadow_file.length;
    CHECK(data_aead_verify_shadow(key, "HOME/NOTE.TXT", produced,
        read_shadow, &io, workspace, sizeof(workspace), &verified) ==
        DATA_AEAD_IO && verified == 0U);
    ++shadow_file.length;
    expect_wiped();
    memcpy(first_id, shadow_file.bytes + 20U, sizeof(first_id));
    CHECK(data_aead_file_identity(key, "HOME/NOTE.TXT",
        shadow_file.bytes, shadow_file.length, stable_id) == DATA_AEAD_OK);
    uint64_t generation = 0U;
    CHECK(data_aead_generation(key, "HOME/NOTE.TXT",
        shadow_file.bytes, shadow_file.length, &generation) == DATA_AEAD_OK &&
        generation == 2U);
    expect_wiped();
    publish_shadow(&io);

    const uint8_t changed[] = {'X', 'Y', 'Z'};
    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", old_file.length,
        5000U, 100U, changed, sizeof(changed), &callbacks, workspace,
        sizeof(workspace), &produced) == DATA_AEAD_OK);
    CHECK(memcmp(first_id, shadow_file.bytes + 20U, sizeof(first_id)) != 0);
    CHECK(data_aead_file_identity(key, "HOME/NOTE.TXT",
        shadow_file.bytes, shadow_file.length, rewritten_id) == DATA_AEAD_OK);
    CHECK(memcmp(stable_id, rewritten_id, sizeof(stable_id)) == 0);
    CHECK(data_aead_generation(key, "HOME/NOTE.TXT",
        shadow_file.bytes, shadow_file.length, &generation) == DATA_AEAD_OK &&
        generation == 3U);
    memcpy(expected + 100U, changed, sizeof(changed));
    check_plaintext(key, &shadow_file, expected, 5000U);
    expect_wiped();
    publish_shadow(&io);

    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", old_file.length,
        100U, 0U, NULL, 0U, &callbacks, workspace, sizeof(workspace),
        &produced) == DATA_AEAD_OK);
    check_plaintext(key, &shadow_file, expected, 100U);
    expect_wiped();
    publish_shadow(&io);

    /* Extension of a sealed short file yields zero-filled sparse space. */
    memset(expected + 100U, 0, sizeof(expected) - 100U);
    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", old_file.length,
        sizeof(expected), 0U, NULL, 0U, &callbacks, workspace,
        sizeof(workspace), &produced) == DATA_AEAD_OK);
    check_plaintext(key, &shadow_file, expected, sizeof(expected));
    expect_wiped();
    publish_shadow(&io);

    saved_file = old_file;
    old_file.bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_NONCE_BYTES + 1U] ^= 1U;
    shadow_file.length = 0U;
    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", old_file.length,
        sizeof(expected), 0U, NULL, 0U, &callbacks, workspace,
        sizeof(workspace), &produced) == DATA_AEAD_AUTHENTICATION);
    CHECK(produced == 0U);
    CHECK(old_file.bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_NONCE_BYTES + 1U]
        != saved_file.bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_NONCE_BYTES + 1U]);
    expect_wiped();
    old_file = saved_file;

    /* Truncation must still authenticate a damaged chunk that would be
     * discarded from the new logical length. */
    const size_t last_chunk = DATA_AEAD_HEADER_BYTES +
        2U * DATA_AEAD_SEALED_CHUNK_BYTES + DATA_AEAD_NONCE_BYTES + 1U;
    old_file.bytes[last_chunk] ^= 1U;
    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", old_file.length,
        100U, 0U, NULL, 0U, &callbacks, workspace, sizeof(workspace),
        &produced) == DATA_AEAD_AUTHENTICATION);
    CHECK(produced == 0U);
    expect_wiped();
    old_file = saved_file;

    shadow_file.length = 0U;
    io.write_limit = DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES;
    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", old_file.length,
        sizeof(expected), 0U, NULL, 0U, &callbacks, workspace,
        sizeof(workspace), &produced) == DATA_AEAD_IO);
    CHECK(produced == 0U && memcmp(&old_file, &saved_file,
        sizeof(old_file)) == 0);
    expect_wiped();
    io.write_limit = FILE_CAPACITY;

    shadow_file.length = 0U;
    CHECK(data_aead_rewrite_shadow(wrong_key, "HOME/NOTE.TXT",
        old_file.length, sizeof(expected), 0U, NULL, 0U, &callbacks,
        workspace, sizeof(workspace), &produced) ==
        DATA_AEAD_AUTHENTICATION);
    CHECK(shadow_file.length == 0U && produced == 0U);

    shadow_file.length = 0U;
    io.fail_random_at = io.random_calls + 1U;
    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", old_file.length,
        sizeof(expected), 0U, NULL, 0U, &callbacks, workspace,
        sizeof(workspace), &produced) == DATA_AEAD_ENTROPY);
    CHECK(shadow_file.length == 0U && produced == 0U);
    expect_wiped();
    io.fail_random_at = 0U;

    CHECK(data_aead_rewrite_shadow(key, "HOME/../NOTE", old_file.length,
        sizeof(expected), 0U, NULL, 0U, &callbacks, workspace,
        sizeof(workspace), &produced) == DATA_AEAD_ARGUMENT);
    memset(workspace, 0xa5, sizeof(workspace));
    CHECK(data_aead_rewrite_shadow(key, "HOME/NOTE.TXT", old_file.length,
        DATA_AEAD_PHYSICAL_MAX, 0U, NULL, 0U, &callbacks, workspace,
        sizeof(workspace), &produced) == DATA_AEAD_RANGE);
    expect_wiped();

    /* A rename verifies the source's path binding and produces a distinct
     * revision that authenticates only at the destination path. */
    CHECK(data_aead_rewrite_shadow_paths(key, "HOME/NOTE.TXT",
        "HOME/MOVED.TXT", old_file.length, sizeof(expected), 0U, NULL,
        0U, &callbacks, workspace, sizeof(workspace), &produced) ==
        DATA_AEAD_OK);
    CHECK(data_aead_verify_shadow(key, "HOME/MOVED.TXT", produced,
        read_shadow, &io, workspace, sizeof(workspace), &verified) ==
        DATA_AEAD_OK && verified == sizeof(expected));
    CHECK(data_aead_verify_shadow(key, "HOME/NOTE.TXT", produced,
        read_shadow, &io, workspace, sizeof(workspace), &verified) ==
        DATA_AEAD_AUTHENTICATION && verified == 0U);
    CHECK(data_aead_rewrite_shadow_paths(key, "HOME/WRONG.TXT",
        "HOME/MOVED.TXT", old_file.length, sizeof(expected), 0U, NULL,
        0U, &callbacks, workspace, sizeof(workspace), &produced) ==
        DATA_AEAD_AUTHENTICATION && produced == 0U);
    expect_wiped();

    /* Legacy plaintext is read into a distinct sealed candidate. The source
     * remains byte-for-byte intact until a higher layer durably publishes. */
    memset(&old_file, 0, sizeof(old_file));
    memcpy(old_file.bytes, expected, sizeof(expected));
    old_file.length = sizeof(expected);
    saved_file = old_file;
    CHECK(data_aead_migrate_plain_shadow(key, "HOME/NOTE.TXT",
        old_file.length, &callbacks, workspace, sizeof(workspace),
        &produced) == DATA_AEAD_OK);
    CHECK(memcmp(&old_file, &saved_file, sizeof(old_file)) == 0);
    check_plaintext(key, &shadow_file, expected, sizeof(expected));
    CHECK(data_aead_verify_shadow(key, "HOME/NOTE.TXT", produced,
        read_shadow, &io, workspace, sizeof(workspace), &verified) ==
        DATA_AEAD_OK && verified == sizeof(expected));
    expect_wiped();
    uint8_t supplied_stable[DATA_AEAD_ID_BYTES] = {1U};
    uint8_t supplied_revision[DATA_AEAD_ID_BYTES] = {2U};
    CHECK(data_aead_migrate_plain_shadow_identified(key,
        "HOME/NOTE.TXT", old_file.length, supplied_stable,
        supplied_revision, 7U, &callbacks, workspace,
        sizeof(workspace), &produced) == DATA_AEAD_OK);
    CHECK(data_aead_file_identity(key, "HOME/NOTE.TXT",
        shadow_file.bytes, produced, stable_id) == DATA_AEAD_OK &&
        memcmp(stable_id, supplied_stable, sizeof(stable_id)) == 0);
    CHECK(data_aead_generation(key, "HOME/NOTE.TXT",
        shadow_file.bytes, produced, &generation) == DATA_AEAD_OK &&
        generation == 7U &&
        memcmp(shadow_file.bytes + 20U, supplied_revision,
            sizeof(supplied_revision)) == 0);
    check_plaintext(key, &shadow_file, expected, sizeof(expected));
    expect_wiped();
    --old_file.length;
    CHECK(data_aead_migrate_plain_shadow(key, "HOME/NOTE.TXT",
        sizeof(expected), &callbacks, workspace, sizeof(workspace),
        &produced) == DATA_AEAD_IO && produced == 0U);
    old_file.length = sizeof(expected);
    expect_wiped();
    io.write_limit = DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES;
    CHECK(data_aead_migrate_plain_shadow(key, "HOME/NOTE.TXT",
        old_file.length, &callbacks, workspace, sizeof(workspace),
        &produced) == DATA_AEAD_IO && produced == 0U);
    io.write_limit = FILE_CAPACITY;
    CHECK(memcmp(&old_file, &saved_file, sizeof(old_file)) == 0);
    expect_wiped();
    io.fail_random_at = io.random_calls + 1U;
    CHECK(data_aead_migrate_plain_shadow(key, "HOME/NOTE.TXT",
        old_file.length, &callbacks, workspace, sizeof(workspace),
        &produced) == DATA_AEAD_ENTROPY && produced == 0U);
    io.fail_random_at = 0U;
    expect_wiped();

    puts("Data AEAD shadow rewrite/readback/range/rename/plaintext migration, partial/sparse/truncate, tamper, disk-full and entropy controls passed");
    return 0;
}
