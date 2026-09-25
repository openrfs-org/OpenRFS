/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openrfs/data_aead_slots.h>

#define FILE_CAPACITY (DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES)
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
        abort(); \
    } \
} while (0)

struct memory_file {
    uint8_t bytes[FILE_CAPACITY];
    size_t length;
    bool present;
};

struct memory_index {
    uint8_t bytes[DATA_AEAD_INDEX_BYTES];
    bool present;
};

struct memory_io {
    struct memory_file file[2];
    struct memory_file durable_file[2];
    struct memory_index index[2];
    struct memory_index durable_index[2];
    bool fail_file_sync;
    bool fail_index_write;
    bool fail_index_sync;
    uint8_t source[128];
    size_t source_length;
    bool source_present;
    bool durable_source_present;
    bool fail_shadow_write;
    bool fail_source_remove;
    bool fail_source_sync;
    unsigned shadow_slot;
    uint8_t random_counter;
};

static bool index_read(void *context, unsigned slot,
    uint8_t record[DATA_AEAD_INDEX_BYTES], bool *present)
{
    struct memory_io *io = context;
    if (slot > 1U) return false;
    *present = io->index[slot].present;
    if (*present) memcpy(record, io->index[slot].bytes,
        DATA_AEAD_INDEX_BYTES);
    return true;
}

static bool index_write(void *context, unsigned slot,
    const uint8_t record[DATA_AEAD_INDEX_BYTES])
{
    struct memory_io *io = context;
    if (slot > 1U) return false;
    io->index[slot].present = true;
    if (io->fail_index_write) {
        memset(io->index[slot].bytes, 0, DATA_AEAD_INDEX_BYTES);
        memcpy(io->index[slot].bytes, record, 20U);
        io->durable_index[slot] = io->index[slot];
        return false;
    }
    memcpy(io->index[slot].bytes, record, DATA_AEAD_INDEX_BYTES);
    return true;
}

static bool index_sync(void *context, unsigned slot)
{
    struct memory_io *io = context;
    if (slot > 1U) return false;
    if (io->fail_index_sync) {
        io->durable_index[slot].present = true;
        memset(io->durable_index[slot].bytes, 0,
            DATA_AEAD_INDEX_BYTES);
        memcpy(io->durable_index[slot].bytes, io->index[slot].bytes,
            40U);
        return false;
    }
    io->durable_index[slot] = io->index[slot];
    return true;
}

static bool file_size(void *context, unsigned slot, uint64_t *size,
    bool *present)
{
    struct memory_io *io = context;
    if (slot > 1U) return false;
    *present = io->file[slot].present;
    *size = io->file[slot].length;
    return true;
}

static bool file_read(void *context, unsigned slot, uint64_t offset,
    uint8_t *output, size_t bytes)
{
    struct memory_io *io = context;
    if (slot > 1U || offset > io->file[slot].length ||
            bytes > io->file[slot].length - (size_t)offset)
        return false;
    memcpy(output, io->file[slot].bytes + (size_t)offset, bytes);
    return true;
}

static bool file_sync(void *context, unsigned slot)
{
    struct memory_io *io = context;
    if (slot > 1U) return false;
    io->durable_file[slot] = io->file[slot];
    if (io->fail_file_sync) {
        io->durable_file[slot].length /= 2U;
        return false;
    }
    return true;
}

static void reboot(struct memory_io *io)
{
    memcpy(io->file, io->durable_file, sizeof(io->file));
    memcpy(io->index, io->durable_index, sizeof(io->index));
    io->fail_file_sync = false;
    io->fail_index_write = false;
    io->fail_index_sync = false;
    io->source_present = io->durable_source_present;
    io->fail_shadow_write = false;
    io->fail_source_remove = false;
    io->fail_source_sync = false;
}

static bool source_size(void *context, uint64_t *size, bool *present)
{
    struct memory_io *io = context;
    *size = io->source_length;
    *present = io->source_present;
    return true;
}

static bool source_remove(void *context)
{
    struct memory_io *io = context;
    if (io->fail_source_remove) return false;
    io->source_present = false;
    return true;
}

static bool source_sync(void *context)
{
    struct memory_io *io = context;
    if (io->fail_source_sync) return false;
    io->durable_source_present = io->source_present;
    return true;
}

static bool begin_shadow(void *context, uint64_t length)
{
    struct memory_io *io = context;
    if (io->shadow_slot > 1U || length > FILE_CAPACITY) return false;
    memset(&io->file[io->shadow_slot], 0,
        sizeof(io->file[io->shadow_slot]));
    io->file[io->shadow_slot].present = true;
    return true;
}

static bool read_old(void *context, uint64_t offset, uint8_t *to,
    size_t length)
{
    struct memory_io *io = context;
    if (!io->source_present || offset > io->source_length ||
            length > io->source_length - (size_t)offset)
        return false;
    memcpy(to, io->source + (size_t)offset, length);
    return true;
}

static bool write_shadow(void *context, uint64_t offset,
    const uint8_t *from, size_t length)
{
    struct memory_io *io = context;
    struct memory_file *file = &io->file[io->shadow_slot];
    if (io->fail_shadow_write || offset > FILE_CAPACITY ||
            length > FILE_CAPACITY - (size_t)offset)
        return false;
    memcpy(file->bytes + (size_t)offset, from, length);
    if (file->length < (size_t)offset + length)
        file->length = (size_t)offset + length;
    return true;
}

static bool random_bytes(void *context, uint8_t *to, size_t length)
{
    struct memory_io *io = context;
    for (size_t at = 0U; at < length; ++at)
        to[at] = ++io->random_counter;
    return true;
}

static void make_candidate(struct memory_io *io,
    const uint8_t key[DATA_AEAD_KEY_BYTES], unsigned slot,
    const uint8_t stable_id[DATA_AEAD_ID_BYTES], uint64_t generation,
    const uint8_t *message, size_t length)
{
    uint8_t revision_id[DATA_AEAD_ID_BYTES];
    uint8_t nonce[DATA_AEAD_NONCE_BYTES];
    uint8_t plain[DATA_AEAD_CHUNK_BYTES] = {0};
    struct memory_file *file = &io->file[slot];
    CHECK(slot < 2U && length > 0U && length <= sizeof(plain));
    for (size_t at = 0U; at < sizeof(revision_id); ++at)
        revision_id[at] = (uint8_t)(generation + at + 5U);
    for (size_t at = 0U; at < sizeof(nonce); ++at)
        nonce[at] = (uint8_t)(generation + at + 39U);
    memcpy(plain, message, length);
    CHECK(data_aead_make_header_v2(key, "HOME/NOTE.TXT", length,
        stable_id, revision_id, generation, file->bytes) == DATA_AEAD_OK);
    CHECK(data_aead_seal_chunk(key, file->bytes, 0U, nonce, plain,
        length, file->bytes + DATA_AEAD_HEADER_BYTES) == DATA_AEAD_OK);
    file->length = FILE_CAPACITY;
    file->present = true;
}

int main(void)
{
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t wrong_key[DATA_AEAD_KEY_BYTES];
    uint8_t stable_id[DATA_AEAD_ID_BYTES];
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    struct memory_io io = {0};
    struct data_aead_selection selected = {0};
    struct data_aead_slot_io callbacks = {
        &io, index_read, index_write, index_sync,
        file_size, file_read, file_sync,
        source_size, source_remove, source_sync
    };
    for (size_t at = 0U; at < sizeof(key); ++at) {
        key[at] = (uint8_t)(at + 1U);
        wrong_key[at] = (uint8_t)(at + 37U);
    }
    for (size_t at = 0U; at < sizeof(stable_id); ++at)
        stable_id[at] = (uint8_t)(at + 81U);
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_NOT_FOUND);
    const uint8_t first[] = "first-secret";
    const uint8_t second[] = "second-secret";
    make_candidate(&io, key, 0U, stable_id, 1U, first,
        sizeof(first) - 1U);
    CHECK(memcmp(io.file[0].bytes + DATA_AEAD_HEADER_BYTES +
        DATA_AEAD_NONCE_BYTES, first, sizeof(first) - 1U) != 0);
    CHECK(data_aead_publish(key, "HOME/NOTE.TXT", &callbacks, 0U,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK);
    CHECK(selected.generation == 1U && selected.file_slot == 0U &&
        selected.plaintext_bytes == sizeof(first) - 1U);
    reboot(&io);
    make_candidate(&io, key, 1U, stable_id, 2U, second,
        sizeof(second) - 1U);
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK &&
        selected.generation == 1U);

    io.fail_file_sync = true;
    CHECK(data_aead_publish(key, "HOME/NOTE.TXT", &callbacks, 1U,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_IO);
    reboot(&io);
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK &&
        selected.generation == 1U);

    make_candidate(&io, key, 1U, stable_id, 2U, second,
        sizeof(second) - 1U);
    io.fail_index_write = true;
    CHECK(data_aead_publish(key, "HOME/NOTE.TXT", &callbacks, 1U,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_IO);
    reboot(&io);
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK &&
        selected.generation == 1U);

    make_candidate(&io, key, 1U, stable_id, 2U, second,
        sizeof(second) - 1U);
    io.fail_index_sync = true;
    CHECK(data_aead_publish(key, "HOME/NOTE.TXT", &callbacks, 1U,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_IO);
    reboot(&io);
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK &&
        selected.generation == 1U);

    make_candidate(&io, key, 1U, stable_id, 2U, second,
        sizeof(second) - 1U);
    CHECK(data_aead_publish(key, "HOME/NOTE.TXT", &callbacks, 1U,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK);
    reboot(&io);
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK &&
        selected.generation == 2U && selected.file_slot == 1U);
    CHECK(data_aead_select(wrong_key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_AUTHENTICATION);
    CHECK(data_aead_select(key, "HOME/WRONG.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_AUTHENTICATION);
    io.index[1].bytes[40U] ^= 1U;
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_AUTHENTICATION);
    reboot(&io);
    io.file[1].bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_NONCE_BYTES] ^= 1U;
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_AUTHENTICATION);
    CHECK(selected.generation == 0U);
    reboot(&io);
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK &&
        selected.generation == 2U);
    uint8_t first_index[DATA_AEAD_INDEX_BYTES];
    memcpy(first_index, io.index[0].bytes, sizeof(first_index));
    const uint8_t third[] = "third-secret";
    make_candidate(&io, key, 0U, stable_id, 3U, third,
        sizeof(third) - 1U);
    CHECK(data_aead_publish(key, "HOME/NOTE.TXT", &callbacks, 0U,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK);
    reboot(&io);
    memcpy(io.index[1].bytes, first_index, sizeof(first_index));
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_CONFLICT);
    reboot(&io);
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &callbacks,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK &&
        selected.generation == 3U);

    struct memory_io migration = {0};
    const uint8_t legacy[] = "legacy-secret";
    memcpy(migration.source, legacy, sizeof(legacy) - 1U);
    migration.source_length = sizeof(legacy) - 1U;
    migration.source_present = true;
    migration.durable_source_present = true;
    struct data_aead_slot_io migration_slots = {
        &migration, index_read, index_write, index_sync,
        file_size, file_read, file_sync,
        source_size, source_remove, source_sync
    };
    struct data_aead_rewrite_io migration_rewrite = {
        .context = &migration,
        .begin_shadow = begin_shadow,
        .read_old = read_old,
        .write_shadow = write_shadow,
        .random = random_bytes,
    };
    migration.fail_shadow_write = true;
    CHECK(data_aead_migrate_one(key, "HOME/NOTE.TXT", &migration_slots,
        &migration_rewrite, workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_IO && migration.source_present);
    reboot(&migration);
    migration.fail_index_write = true;
    CHECK(data_aead_migrate_one(key, "HOME/NOTE.TXT", &migration_slots,
        &migration_rewrite, workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_IO && migration.source_present);
    reboot(&migration);
    migration.fail_source_remove = true;
    CHECK(data_aead_migrate_one(key, "HOME/NOTE.TXT", &migration_slots,
        &migration_rewrite, workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_IO && migration.source_present);
    reboot(&migration);
    CHECK(data_aead_migrate_one(wrong_key, "HOME/NOTE.TXT",
        &migration_slots, &migration_rewrite, workspace,
        sizeof(workspace), &selected) == DATA_AEAD_AUTHENTICATION &&
        migration.source_present);
    migration.file[0].bytes[DATA_AEAD_HEADER_BYTES +
        DATA_AEAD_NONCE_BYTES] ^= 1U;
    CHECK(data_aead_migrate_one(key, "HOME/NOTE.TXT", &migration_slots,
        &migration_rewrite, workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_AUTHENTICATION && migration.source_present);
    reboot(&migration);
    migration.fail_source_sync = true;
    CHECK(data_aead_migrate_one(key, "HOME/NOTE.TXT", &migration_slots,
        &migration_rewrite, workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_IO && !migration.source_present);
    reboot(&migration);
    CHECK(migration.source_present);
    CHECK(data_aead_migrate_one(key, "HOME/NOTE.TXT", &migration_slots,
        &migration_rewrite, workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_OK && !migration.source_present);
    reboot(&migration);
    CHECK(!migration.source_present);
    CHECK(data_aead_select(key, "HOME/NOTE.TXT", &migration_slots,
        workspace, sizeof(workspace), &selected) == DATA_AEAD_OK &&
        selected.generation == 1U &&
        selected.plaintext_bytes == sizeof(legacy) - 1U);
    migration.fail_source_sync = true;
    CHECK(data_aead_migrate_one(key, "HOME/NOTE.TXT", &migration_slots,
        &migration_rewrite, workspace, sizeof(workspace), &selected) ==
        DATA_AEAD_IO && selected.generation == 0U);
    reboot(&migration);
    CHECK(memcmp(migration.file[0].bytes + DATA_AEAD_HEADER_BYTES +
        DATA_AEAD_NONCE_BYTES, legacy, sizeof(legacy) - 1U) != 0);

    puts("Data AEAD dual-slot publication, conversion cuts, wrong key, path, tamper and stale-index refusal passed");
    return 0;
}
