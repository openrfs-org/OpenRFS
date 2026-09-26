/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openrfs/data_aead_manifest.h>

static unsigned next_nonce = 1U;

struct segment_blob {
    uint8_t bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES];
};

static bool read_segment(void *context, uint64_t offset, uint8_t *to,
    size_t bytes);

struct manifest_slots {
    uint8_t records[2][DATA_AEAD_MANIFEST_BYTES];
    uint8_t temp[DATA_AEAD_MANIFEST_BYTES];
    const struct segment_blob *segment;
    bool present[2];
    bool temp_present;
    bool fail_read;
    unsigned fault;
    unsigned slot_sync_calls;
};

static bool read_manifest_slot(void *context, unsigned slot,
    uint8_t record[DATA_AEAD_MANIFEST_BYTES], bool *present)
{
    const struct manifest_slots *slots = context;
    if (slot > 1U || slots->fail_read) return false;
    *present = slots->present[slot];
    if (*present) memcpy(record, slots->records[slot],
        DATA_AEAD_MANIFEST_BYTES);
    return true;
}

static bool remove_manifest_slot(void *context, unsigned slot)
{
    struct manifest_slots *slots = context;
    if (slot > 1U) return false;
    slots->present[slot] = false;
    return slots->fault != 3U;
}

static bool sync_manifest_slot(void *context, unsigned slot)
{
    struct manifest_slots *slots = context;
    if (slot > 1U) return false;
    ++slots->slot_sync_calls;
    return slots->fault != (slots->slot_sync_calls == 1U ? 4U : 6U);
}

static bool write_manifest_temp(void *context,
    const uint8_t record[DATA_AEAD_MANIFEST_BYTES])
{
    struct manifest_slots *slots = context;
    memcpy(slots->temp, record, DATA_AEAD_MANIFEST_BYTES);
    slots->temp_present = true;
    return slots->fault != 1U;
}

static bool sync_manifest_temp(void *context)
{
    const struct manifest_slots *slots = context;
    return slots->temp_present && slots->fault != 2U;
}

static bool read_manifest_temp(void *context,
    uint8_t record[DATA_AEAD_MANIFEST_BYTES])
{
    const struct manifest_slots *slots = context;
    if (!slots->temp_present) return false;
    memcpy(record, slots->temp, DATA_AEAD_MANIFEST_BYTES);
    return true;
}

static bool publish_manifest_temp(void *context, unsigned slot)
{
    struct manifest_slots *slots = context;
    if (slot > 1U || !slots->temp_present || slots->present[slot])
        return false;
    memcpy(slots->records[slot], slots->temp, DATA_AEAD_MANIFEST_BYTES);
    slots->present[slot] = true;
    slots->temp_present = false;
    return slots->fault != 5U;
}

static bool sync_segment(void *context,
    const uint8_t revision_id[DATA_AEAD_ID_BYTES])
{
    const struct manifest_slots *slots = context;
    return slots->segment != NULL &&
        memcmp(slots->segment->bytes + 20U, revision_id,
            DATA_AEAD_ID_BYTES) == 0;
}

static bool segment_size(void *context,
    const uint8_t revision_id[DATA_AEAD_ID_BYTES], uint64_t *size)
{
    const struct manifest_slots *slots = context;
    if (!sync_segment(context, revision_id)) return false;
    *size = sizeof(slots->segment->bytes);
    return true;
}

static bool read_publication_segment(void *context,
    const uint8_t revision_id[DATA_AEAD_ID_BYTES], uint64_t offset,
    uint8_t *to, size_t bytes)
{
    const struct manifest_slots *slots = context;
    return sync_segment(context, revision_id) &&
        read_segment((void *)slots->segment, offset, to, bytes);
}

static bool read_segment(void *context, uint64_t offset, uint8_t *to,
    size_t bytes)
{
    const struct segment_blob *blob = context;
    if (offset > sizeof(blob->bytes) ||
            bytes > sizeof(blob->bytes) - (size_t)offset) return false;
    memcpy(to, blob->bytes + (size_t)offset, bytes);
    return true;
}

static bool random_bytes(void *context, uint8_t *to, size_t bytes)
{
    (void)context;
    for (size_t at = 0U; at < bytes; ++at)
        to[at] = (uint8_t)(next_nonce + at);
    ++next_nonce;
    return true;
}

static bool no_random(void *context, uint8_t *to, size_t bytes)
{
    (void)context;
    (void)to;
    (void)bytes;
    return false;
}

static bool check(bool condition, const char *message)
{
    if (!condition) fprintf(stderr, "Data manifest: %s\n", message);
    return condition;
}

static bool is_zero(const void *memory, size_t bytes)
{
    const uint8_t *from = memory;
    for (size_t at = 0U; at < bytes; ++at)
        if (from[at] != 0U) return false;
    return true;
}

int main(void)
{
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t wrong_key[DATA_AEAD_KEY_BYTES];
    uint8_t migration_stable[DATA_AEAD_ID_BYTES];
    uint8_t migration_revision[DATA_AEAD_ID_BYTES];
    uint8_t retry_stable[DATA_AEAD_ID_BYTES];
    uint8_t retry_revision[DATA_AEAD_ID_BYTES];
    uint8_t record[DATA_AEAD_MANIFEST_BYTES];
    uint8_t second[DATA_AEAD_MANIFEST_BYTES];
    uint8_t damaged[DATA_AEAD_MANIFEST_BYTES];
    uint8_t segment_header[DATA_AEAD_HEADER_BYTES];
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    char binding[DATA_AEAD_PATH_MAX + 1U];
    char physical[DATA_AEAD_PATH_MAX + 1U];
    char other_physical[DATA_AEAD_PATH_MAX + 1U];
    char manifest_path[DATA_AEAD_PATH_MAX + 1U];
    char peer_path[DATA_AEAD_PATH_MAX + 1U];
    char temp_path[DATA_AEAD_PATH_MAX + 1U];
    struct data_aead_manifest original = {0};
    struct data_aead_manifest opened;
    for (size_t at = 0U; at < sizeof(key); ++at) {
        key[at] = (uint8_t)(at + 1U);
        wrong_key[at] = (uint8_t)(at + 2U);
    }
    if (!check(data_aead_migration_ids(key, "HOME/BIG.BIN", 0U,
            migration_stable, migration_revision) == DATA_AEAD_OK,
            "migration ids derive") ||
        !check(data_aead_migration_ids(key, "HOME/BIG.BIN", 0U,
            retry_stable, retry_revision) == DATA_AEAD_OK &&
            memcmp(migration_stable, retry_stable,
                DATA_AEAD_ID_BYTES) == 0 &&
            memcmp(migration_revision, retry_revision,
                DATA_AEAD_ID_BYTES) == 0,
            "migration retry finds the same staging identity") ||
        !check(data_aead_migration_ids(key, "HOME/BIG.BIN", 1U,
            retry_stable, retry_revision) == DATA_AEAD_OK &&
            memcmp(migration_stable, retry_stable,
                DATA_AEAD_ID_BYTES) == 0 &&
            memcmp(migration_revision, retry_revision,
                DATA_AEAD_ID_BYTES) != 0,
            "segments share a file identity but not a revision") ||
        !check(data_aead_migration_ids(key, "HOME/OTHER.BIN", 0U,
            retry_stable, retry_revision) == DATA_AEAD_OK &&
            memcmp(migration_stable, retry_stable,
                DATA_AEAD_ID_BYTES) != 0,
            "migration path changes file identity") ||
        !check(data_aead_migration_ids(wrong_key, "HOME/BIG.BIN", 0U,
            retry_stable, retry_revision) == DATA_AEAD_OK &&
            memcmp(migration_stable, retry_stable,
                DATA_AEAD_ID_BYTES) != 0,
            "migration key changes file identity") ||
        !check(data_aead_migration_ids(key, "HOME/BIG.BIN",
            DATA_AEAD_SEGMENTS_MAX, retry_stable, retry_revision) ==
            DATA_AEAD_ARGUMENT &&
            is_zero(retry_stable, sizeof(retry_stable)) &&
            is_zero(retry_revision, sizeof(retry_revision)),
            "invalid migration segment clears output")) return 1;
    for (size_t at = 0U; at < DATA_AEAD_ID_BYTES; ++at)
        original.stable_id[at] = (uint8_t)(0xa0U + at);
    original.segment_count = DATA_AEAD_SEGMENTS_MAX;
    original.generation = 5U;
    original.plaintext_bytes = DATA_AEAD_SEGMENT_BYTES *
        DATA_AEAD_SEGMENTS_MAX;
    for (unsigned at = 0U; at < DATA_AEAD_SEGMENTS_MAX; ++at) {
        original.segments[at].revision_id[0] = (uint8_t)(at + 1U);
        original.segments[at].plaintext_bytes = DATA_AEAD_SEGMENT_BYTES;
        original.segments[at].generation = at + 1U;
    }
    if (!check(data_aead_manifest_seal(key, "HOME/BIG.BIN", &original,
            random_bytes, NULL, record) == DATA_AEAD_OK,
            "64 MiB manifest seals") ||
        !check(data_aead_manifest_open(key, "HOME/BIG.BIN", record,
            &opened) == DATA_AEAD_OK,
            "64 MiB manifest opens") ||
        !check(opened.plaintext_bytes == original.plaintext_bytes &&
            opened.segment_count == DATA_AEAD_SEGMENTS_MAX &&
            opened.generation == original.generation &&
            memcmp(opened.stable_id, original.stable_id,
                DATA_AEAD_ID_BYTES) == 0 &&
            opened.segments[7].generation == 8U,
            "maximum layout round trips") ||
        !check(data_aead_manifest_seal(key, "HOME/BIG.BIN", &original,
            random_bytes, NULL, second) == DATA_AEAD_OK &&
            memcmp(record + 8U, second + 8U, DATA_AEAD_NONCE_BYTES) != 0 &&
            memcmp(record, second, sizeof(record)) != 0,
            "each publication uses a new nonce") ||
        !check(data_aead_segment_binding(original.stable_id, 7U,
            binding) == DATA_AEAD_OK &&
            strcmp(binding, "SEG/A0A1A2A3/A4A5A6A7/A8A9AAAB/ACADAEAF/S7") == 0,
            "segment binding is stable and FAT32 compatible") ||
        !check(data_aead_segment_storage_path(key,
            original.segments[7].revision_id, physical) == DATA_AEAD_OK &&
            strlen(physical) == 52U && physical[8] == '/' &&
            physical[17] == '/' && physical[26] == '/' &&
            physical[35] == '/' && physical[44] == '/' &&
            strcmp(physical + 45U, "SEG.DAT") == 0,
            "physical path uses bounded 8.3 components") ||
        !check(data_aead_segment_storage_path(wrong_key,
            original.segments[7].revision_id,
            other_physical) == DATA_AEAD_OK &&
            strcmp(physical, other_physical) != 0 &&
            data_aead_segment_storage_path(key,
                original.segments[6].revision_id,
                other_physical) == DATA_AEAD_OK &&
            strcmp(physical, other_physical) != 0,
            "physical path depends on key and revision") ||
        !check(data_aead_manifest_storage_path(key, "HOME/BIG.BIN", 0U,
            manifest_path) == DATA_AEAD_OK &&
            data_aead_manifest_storage_path(key, "HOME/BIG.BIN", 1U,
                peer_path) == DATA_AEAD_OK &&
            data_aead_manifest_storage_path(key, "HOME/BIG.BIN", 2U,
                temp_path) == DATA_AEAD_OK &&
            strncmp(manifest_path, physical, 8U) == 0 &&
            strcmp(strrchr(manifest_path, '/'), "/M0.DAT") == 0 &&
            strcmp(strrchr(peer_path, '/'), "/M1.DAT") == 0 &&
            strcmp(strrchr(temp_path, '/'), "/TMP.DAT") == 0 &&
            strncmp(manifest_path, peer_path,
                strlen(manifest_path) - 6U) == 0,
            "manifest slots share a keyed 8.3 parent") ||
        !check(data_aead_make_header_v2(key, binding,
            DATA_AEAD_SEGMENT_BYTES, original.stable_id,
            original.segments[7].revision_id, 8U,
            segment_header) == DATA_AEAD_OK,
            "segment header seals") ||
        !check(data_aead_manifest_check_segment(key, &original, 7U,
            segment_header,
            data_aead_physical_size(DATA_AEAD_SEGMENT_BYTES)) ==
            DATA_AEAD_OK,
            "manifest authenticates selected segment header"))
        return 1;
    segment_header[20] ^= 1U;
    if (!check(data_aead_manifest_check_segment(key, &original, 7U,
            segment_header,
            data_aead_physical_size(DATA_AEAD_SEGMENT_BYTES)) ==
            DATA_AEAD_AUTHENTICATION,
            "changed revision refuses")) return 1;
    uint8_t other_revision[DATA_AEAD_ID_BYTES] = {0};
    other_revision[0] = 99U;
    if (!check(data_aead_make_header_v2(key, binding,
            DATA_AEAD_SEGMENT_BYTES, original.stable_id,
            other_revision, 8U, segment_header) == DATA_AEAD_OK &&
            data_aead_manifest_check_segment(key, &original, 7U,
                segment_header,
                data_aead_physical_size(DATA_AEAD_SEGMENT_BYTES)) ==
                DATA_AEAD_AUTHENTICATION,
            "different authentic revision refuses") ||
        !check(data_aead_manifest_check_segment(key, &original, 6U,
            segment_header,
            data_aead_physical_size(DATA_AEAD_SEGMENT_BYTES)) ==
            DATA_AEAD_AUTHENTICATION,
            "segment index is bound") ||
        !check(data_aead_manifest_check_segment(key, &original, 7U,
            segment_header, DATA_AEAD_HEADER_BYTES) == DATA_AEAD_FORMAT,
            "physical length is bound")) return 1;

    struct data_aead_manifest small = original;
    struct segment_blob blob = {0};
    uint8_t plain[DATA_AEAD_CHUNK_BYTES] = {0};
    uint8_t nonce[DATA_AEAD_NONCE_BYTES] = {0};
    small.segment_count = 1U;
    small.plaintext_bytes = 5U;
    small.segments[0].plaintext_bytes = 5U;
    memcpy(plain, "hello", 5U);
    nonce[0] = 1U;
    if (!check(data_aead_segment_binding(small.stable_id, 0U,
            binding) == DATA_AEAD_OK &&
            data_aead_make_header_v2(key, binding, 5U, small.stable_id,
                small.segments[0].revision_id,
                small.segments[0].generation,
                blob.bytes) == DATA_AEAD_OK &&
            data_aead_seal_chunk(key, blob.bytes, 0U, nonce,
                plain, 5U, blob.bytes + DATA_AEAD_HEADER_BYTES) ==
                DATA_AEAD_OK,
            "small segment seals") ||
        !check(data_aead_manifest_verify_segment(key, &small, 0U,
            sizeof(blob.bytes), read_segment, &blob, workspace,
            sizeof(workspace)) == DATA_AEAD_OK,
            "manifest verifies complete segment content")) return 1;
    blob.bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES - 1U]
        ^= 1U;
    if (!check(data_aead_manifest_verify_segment(key, &small, 0U,
            sizeof(blob.bytes), read_segment, &blob, workspace,
            sizeof(workspace)) == DATA_AEAD_AUTHENTICATION,
            "tampered segment content refuses")) return 1;

    struct manifest_slots slots = {0};
    struct data_aead_manifest selected;
    unsigned selected_slot = 99U;
    struct data_aead_manifest_slot_io slot_io = {
        &slots, read_manifest_slot
    };
    if (!check(data_aead_manifest_select(key, "HOME/BIG.BIN", &slot_io,
            &selected, &selected_slot) == DATA_AEAD_NOT_FOUND &&
            is_zero(&selected, sizeof(selected)),
            "absent slots are not a plaintext file")) return 1;
    slots.present[0] = true;
    memcpy(slots.records[0], record, sizeof(record));
    original.generation++;
    if (!check(data_aead_manifest_seal(key, "HOME/BIG.BIN", &original,
            random_bytes, NULL, second) == DATA_AEAD_OK,
            "next manifest generation seals")) return 1;
    original.generation--;
    slots.present[1] = true;
    memcpy(slots.records[1], second, sizeof(second));
    if (!check(data_aead_manifest_select(key, "HOME/BIG.BIN", &slot_io,
            &selected, &selected_slot) == DATA_AEAD_OK &&
            selected_slot == 1U && selected.generation == 6U,
            "newest adjacent authenticated slot wins")) return 1;
    slots.records[1][32] ^= 1U;
    if (!check(data_aead_manifest_select(key, "HOME/BIG.BIN", &slot_io,
            &selected, &selected_slot) == DATA_AEAD_AUTHENTICATION &&
            is_zero(&selected, sizeof(selected)),
            "shaped bad MAC refuses rollback to older slot")) return 1;
    memcpy(slots.records[1], second, sizeof(second));
    slots.records[1][4] = 2U;
    if (!check(data_aead_manifest_select(key, "HOME/BIG.BIN", &slot_io,
            &selected, &selected_slot) == DATA_AEAD_OK &&
            selected_slot == 0U && selected.generation == 5U,
            "torn inactive shape keeps old slot")) return 1;
    memcpy(slots.records[1], record, sizeof(record));
    if (!check(data_aead_manifest_select(key, "HOME/BIG.BIN", &slot_io,
            &selected, &selected_slot) == DATA_AEAD_CONFLICT,
            "equal generations refuse")) return 1;
    original.generation++;
    original.stable_id[0] ^= 1U;
    if (!check(data_aead_manifest_seal(key, "HOME/BIG.BIN", &original,
            random_bytes, NULL, slots.records[1]) == DATA_AEAD_OK &&
            data_aead_manifest_select(key, "HOME/BIG.BIN", &slot_io,
                &selected, &selected_slot) == DATA_AEAD_CONFLICT,
            "identity changes across generations refuse")) return 1;
    original.stable_id[0] ^= 1U;
    original.generation--;
    slots.fail_read = true;
    if (!check(data_aead_manifest_select(key, "HOME/BIG.BIN", &slot_io,
            &selected, &selected_slot) == DATA_AEAD_IO,
            "read error refuses")) return 1;
    slots.fail_read = false;

    blob.bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES - 1U]
        ^= 1U;
    struct data_aead_manifest base = small;
    struct data_aead_manifest next = small;
    struct data_aead_manifest published;
    uint8_t base_record[DATA_AEAD_MANIFEST_BYTES];
    base.generation = 1U;
    next.generation = 2U;
    struct data_aead_manifest_publish_io publish_io = {
        .context = &slots,
        .slot_read = read_manifest_slot,
        .slot_remove = remove_manifest_slot,
        .slot_sync = sync_manifest_slot,
        .temp_write = write_manifest_temp,
        .temp_sync = sync_manifest_temp,
        .temp_read = read_manifest_temp,
        .temp_publish = publish_manifest_temp,
        .segment_sync = sync_segment,
        .segment_size = segment_size,
        .segment_read = read_publication_segment,
        .random = random_bytes,
    };
    if (!check(data_aead_manifest_seal(key, "HOME/SMALL.BIN", &base,
            random_bytes, NULL, base_record) == DATA_AEAD_OK,
            "published old generation fixture")) return 1;
    for (unsigned cut = 1U; cut <= 6U; ++cut) {
        memset(&slots, 0, sizeof(slots));
        slots.segment = &blob;
        slots.present[0] = true;
        memcpy(slots.records[0], base_record, sizeof(base_record));
        slots.fault = cut;
        if (!check(data_aead_manifest_publish(key, "HOME/SMALL.BIN",
                &next, &publish_io, workspace, sizeof(workspace),
                &published, &selected_slot) == DATA_AEAD_IO,
                "cut must report I/O failure") ||
            !check(data_aead_manifest_select(key, "HOME/SMALL.BIN",
                &slot_io, &selected, &selected_slot) == DATA_AEAD_OK &&
                (selected.generation == 1U || selected.generation == 2U),
                "cut leaves a complete generation")) return 1;
        slots.fault = 0U;
        slots.slot_sync_calls = 0U;
        if (!check(data_aead_manifest_publish(key, "HOME/SMALL.BIN",
                &next, &publish_io, workspace, sizeof(workspace),
                &published, &selected_slot) == DATA_AEAD_OK &&
                published.generation == 2U && selected_slot == 1U,
                "retry publishes exact candidate")) return 1;
    }
    memset(&slots, 0, sizeof(slots));
    slots.segment = &blob;
    slots.present[0] = true;
    memcpy(slots.records[0], base_record, sizeof(base_record));
    blob.bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES - 1U]
        ^= 1U;
    if (!check(data_aead_manifest_publish(key, "HOME/SMALL.BIN",
            &next, &publish_io, workspace, sizeof(workspace),
            &published, &selected_slot) == DATA_AEAD_AUTHENTICATION &&
            !slots.present[1] && !slots.temp_present,
            "tampered content cannot publish a manifest")) return 1;
    blob.bytes[DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES - 1U]
        ^= 1U;

    memset(&opened, 0xa5, sizeof(opened));
    if (!check(data_aead_manifest_open(wrong_key, "HOME/BIG.BIN",
            record, &opened) == DATA_AEAD_AUTHENTICATION &&
            is_zero(&opened, sizeof(opened)),
            "wrong key refuses without output") ||
        !check(data_aead_manifest_open(key, "HOME/OTHER.BIN", record,
            &opened) == DATA_AEAD_AUTHENTICATION &&
            is_zero(&opened, sizeof(opened)),
            "path is authenticated")) return 1;

    memcpy(damaged, record, sizeof(damaged));
    damaged[32] ^= 1U;
    if (!check(data_aead_manifest_open(key, "HOME/BIG.BIN", damaged,
            &opened) == DATA_AEAD_AUTHENTICATION,
            "tag tampering refuses")) return 1;
    memcpy(damaged, record, sizeof(damaged));
    damaged[70] ^= 1U;
    if (!check(data_aead_manifest_open(key, "HOME/BIG.BIN", damaged,
            &opened) == DATA_AEAD_AUTHENTICATION,
            "ciphertext tampering refuses")) return 1;
    memcpy(damaged, record, sizeof(damaged));
    damaged[4] = 2U;
    if (!check(data_aead_manifest_open(key, "HOME/BIG.BIN", damaged,
            &opened) == DATA_AEAD_FORMAT,
            "unknown format refuses")) return 1;

    original.segments[7].revision_id[0] =
        original.segments[0].revision_id[0];
    if (!check(data_aead_manifest_seal(key, "HOME/BIG.BIN", &original,
            random_bytes, NULL, damaged) == DATA_AEAD_ARGUMENT &&
            is_zero(damaged, sizeof(damaged)),
            "duplicate segment revision refuses")) return 1;
    original.segments[7].revision_id[0] = 8U;
    original.segments[3].plaintext_bytes--;
    if (!check(data_aead_manifest_seal(key, "HOME/BIG.BIN", &original,
            random_bytes, NULL, damaged) == DATA_AEAD_ARGUMENT,
            "nonfinal short segment refuses")) return 1;
    original.segments[3].plaintext_bytes++;
    original.plaintext_bytes--;
    if (!check(data_aead_manifest_seal(key, "HOME/BIG.BIN", &original,
            random_bytes, NULL, damaged) == DATA_AEAD_ARGUMENT,
            "partial logical size refuses")) return 1;
    original.plaintext_bytes++;
    if (!check(data_aead_manifest_seal(key, "HOME/BIG.BIN", &original,
            no_random, NULL, damaged) == DATA_AEAD_ENTROPY &&
            is_zero(damaged, sizeof(damaged)),
            "entropy failure leaves no record")) return 1;

    original.segment_count = 0U;
    original.plaintext_bytes = 0U;
    if (!check(data_aead_manifest_seal(key, "EMPTY", &original,
            random_bytes, NULL, record) == DATA_AEAD_OK &&
            data_aead_manifest_open(key, "EMPTY", record,
                &opened) == DATA_AEAD_OK &&
            opened.segment_count == 0U && opened.plaintext_bytes == 0U,
            "empty file round trips")) return 1;
    puts("Data AEAD encrypted segment manifest, 64 MiB, path, tamper and entropy controls passed");
    return 0;
}
