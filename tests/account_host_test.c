/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openrfs/account.h>
#include <openrfs/account_kdf.h>
#include <openrfs/account_v2.h>
#include <openrfs/clock.h>
#include <openrfs/fat32_fs.h>
#include <openrfs/package_state.h>
#include <openrfs/random.h>

#include "../vendor/monocypher/src/monocypher.h"

#define RECORD_BYTES 124U
#define CHECKSUM_OFFSET 92U
#define USERNAME_OFFSET 28U
#define ACCOUNT_PATH "OPENRFS/LOGIN.DAT"
#define TEMP_PATH "OPENRFS/LOGIN.NEW"
#define V2_A_PATH "OPENRFS/LOGIN.V2A"
#define V2_B_PATH "OPENRFS/LOGIN.V2B"

static uint8_t stored[RECORD_BYTES];
static uint8_t v2_a[ACCOUNT_V2_RECORD_BYTES];
static uint8_t v2_b[ACCOUNT_V2_RECORD_BYTES];
static uint8_t pending[ACCOUNT_V2_RECORD_BYTES];
static size_t stored_bytes;
static size_t v2_a_bytes;
static size_t v2_b_bytes;
static size_t pending_bytes;
static size_t read_position;
static bool directory_present;
static bool stored_present;
static bool v2_a_present;
static bool v2_b_present;
static bool pending_present;
static bool test_clock_started;
static uint64_t test_now_ns;
static bool refuse_kdf;
static bool fail_sync_once;
static bool fail_unlink_v1_once;
static bool fail_unlink_v2_once;
static const uint8_t *read_source;
static size_t read_source_bytes;
static bool ordinary_file_present;
static struct {
    char path[OPENRFSFS_MAX_PATH];
    unsigned int cursor;
    bool used;
} directory_slots[OPENRFSFS_MAX_DEPTH];

static bool check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "account host test: %s\n", message);
    }
    return condition;
}

bool clock_is_started(void)
{
    return test_clock_started;
}

uint64_t clock_monotonic_ns(void)
{
    return test_now_ns;
}

bool account_kdf_v2_parameters_supported(uint32_t algorithm, uint8_t version,
    uint32_t memory_kib, uint32_t passes, uint32_t lanes)
{
    return algorithm == ACCOUNT_KDF_V2_ALGORITHM &&
        version == ACCOUNT_KDF_V2_VERSION &&
        memory_kib == ACCOUNT_KDF_V2_MEMORY_KIB &&
        passes == ACCOUNT_KDF_V2_PASSES && lanes == ACCOUNT_KDF_V2_LANES;
}

/* The production KDF has a separate published-vector and resource gate. */
enum account_kdf_status account_kdf_v2_derive(const uint8_t salt[16],
    const uint8_t *password, size_t password_bytes, uint8_t output[32])
{
    uint8_t input[80] = {0};
    if (refuse_kdf) {
        return ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE;
    }
    memcpy(input, salt, 16U);
    memcpy(input + 16U, password, password_bytes);
    crypto_blake2b(output, 32U, input, sizeof(input));
    crypto_wipe(input, sizeof(input));
    return ACCOUNT_KDF_STATUS_OK;
}

enum random_status random_bytes(void *destination, size_t length)
{
    if (destination == NULL) {
        return RANDOM_STATUS_NULL_ARGUMENT;
    }
    for (size_t index = 0U; index < length; ++index) {
        ((uint8_t *)destination)[index] = (uint8_t)(index + 1U);
    }
    return RANDOM_STATUS_OK;
}

enum openrfsfs_status openrfsfs_stat_path(enum openrfsfs_volume volume,
    const char *path, struct openrfsfs_stat *stat)
{
    if (volume != OPENRFSFS_VOLUME_DATA) {
        return OPENRFSFS_STATUS_NOT_FOUND;
    }
    memset(stat, 0, sizeof(*stat));
    if (strcmp(path, "OPENRFS") == 0 && directory_present) {
        stat->directory = true;
        return OPENRFSFS_STATUS_OK;
    }
    if (strcmp(path, ACCOUNT_PATH) == 0 && stored_present) {
        stat->size = stored_bytes;
        return OPENRFSFS_STATUS_OK;
    }
    if (strcmp(path, V2_A_PATH) == 0 && v2_a_present) {
        stat->size = v2_a_bytes;
        return OPENRFSFS_STATUS_OK;
    }
    if (strcmp(path, V2_B_PATH) == 0 && v2_b_present) {
        stat->size = v2_b_bytes;
        return OPENRFSFS_STATUS_OK;
    }
    return OPENRFSFS_STATUS_NOT_FOUND;
}

enum openrfsfs_status openrfsfs_directory_open(
    enum openrfsfs_volume volume, const char *path,
    openrfsfs_directory_handle *handle)
{
    if (volume != OPENRFSFS_VOLUME_DATA || handle == NULL ||
            (strcmp(path, ".") != 0 && strcmp(path, "OPENRFS") != 0 &&
             strcmp(path, "data") != 0)) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    for (size_t at = 0U; at < OPENRFSFS_MAX_DEPTH; ++at) {
        if (!directory_slots[at].used) {
            directory_slots[at].used = true;
            directory_slots[at].cursor = 0U;
            strcpy(directory_slots[at].path, path);
            *handle = at + 1U;
            return OPENRFSFS_STATUS_OK;
        }
    }
    return OPENRFSFS_STATUS_NO_HANDLES;
}

enum openrfsfs_status openrfsfs_directory_read(
    openrfsfs_directory_handle handle, struct openrfsfs_list_entry *entry,
    bool *present)
{
    if (handle == 0U || handle > OPENRFSFS_MAX_DEPTH || entry == NULL ||
            present == NULL || !directory_slots[handle - 1U].used) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    const size_t slot = (size_t)handle - 1U;
    const unsigned int cursor = directory_slots[slot].cursor++;
    const char *name = NULL;

    memset(entry, 0, sizeof(*entry));
    *present = false;
    if (strcmp(directory_slots[slot].path, ".") == 0) {
        if (cursor == 0U) name = "OPENRFS";
        if (cursor == 1U && ordinary_file_present) name = "data";
        entry->directory = true;
    } else if (strcmp(directory_slots[slot].path, "OPENRFS") == 0) {
        if (cursor == 0U && v2_a_present) name = "LOGIN.V2A";
        if (cursor == 0U && v2_b_present) name = "LOGIN.V2B";
        if (cursor == 0U && stored_present) name = "LOGIN.DAT";
    } else if (strcmp(directory_slots[slot].path, "data") == 0 &&
            cursor == 0U && ordinary_file_present) {
        name = "note.txt";
        entry->size = 5U;
    }
    if (name != NULL) {
        strcpy(entry->name, name);
        *present = true;
    }
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status openrfsfs_directory_close(
    openrfsfs_directory_handle handle)
{
    if (handle == 0U || handle > OPENRFSFS_MAX_DEPTH ||
            !directory_slots[handle - 1U].used) {
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    directory_slots[handle - 1U].used = false;
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status openrfsfs_mkdir(enum openrfsfs_volume volume,
    const char *path)
{
    if (volume != OPENRFSFS_VOLUME_DATA || strcmp(path, "OPENRFS") != 0) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    directory_present = true;
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status openrfsfs_unlink(enum openrfsfs_volume volume,
    const char *path)
{
    if (volume != OPENRFSFS_VOLUME_DATA) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (strcmp(path, ACCOUNT_PATH) == 0) {
        if (!stored_present) return OPENRFSFS_STATUS_NOT_FOUND;
        if (fail_unlink_v1_once) {
            fail_unlink_v1_once = false;
            return OPENRFSFS_STATUS_IO;
        }
        stored_present = false;
        return OPENRFSFS_STATUS_OK;
    }
    if (strcmp(path, V2_A_PATH) == 0) {
        if (!v2_a_present) return OPENRFSFS_STATUS_NOT_FOUND;
        if (fail_unlink_v2_once) {
            fail_unlink_v2_once = false;
            return OPENRFSFS_STATUS_IO;
        }
        v2_a_present = false;
        return OPENRFSFS_STATUS_OK;
    }
    if (strcmp(path, V2_B_PATH) == 0) {
        if (!v2_b_present) return OPENRFSFS_STATUS_NOT_FOUND;
        if (fail_unlink_v2_once) {
            fail_unlink_v2_once = false;
            return OPENRFSFS_STATUS_IO;
        }
        v2_b_present = false;
        return OPENRFSFS_STATUS_OK;
    }
    if (strcmp(path, TEMP_PATH) != 0) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (!pending_present) {
        return OPENRFSFS_STATUS_NOT_FOUND;
    }
    pending_present = false;
    pending_bytes = 0U;
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status openrfsfs_create(enum openrfsfs_volume volume,
    const char *path)
{
    if (volume != OPENRFSFS_VOLUME_DATA || strcmp(path, TEMP_PATH) != 0) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    pending_present = true;
    pending_bytes = 0U;
    memset(pending, 0, sizeof(pending));
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status openrfsfs_open(enum openrfsfs_volume volume,
    const char *path, enum openrfsfs_access access, openrfsfs_handle *handle)
{
    if (volume != OPENRFSFS_VOLUME_DATA || handle == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (strcmp(path, ACCOUNT_PATH) == 0 &&
            access == OPENRFSFS_ACCESS_READ) {
        if (!stored_present) {
            return OPENRFSFS_STATUS_NOT_FOUND;
        }
        read_position = 0U;
        read_source = stored;
        read_source_bytes = stored_bytes;
        *handle = 1U;
        return OPENRFSFS_STATUS_OK;
    }
    if (strcmp(path, V2_A_PATH) == 0 &&
            access == OPENRFSFS_ACCESS_READ && v2_a_present) {
        read_position = 0U;
        read_source = v2_a;
        read_source_bytes = v2_a_bytes;
        *handle = 1U;
        return OPENRFSFS_STATUS_OK;
    }
    if (strcmp(path, V2_B_PATH) == 0 &&
            access == OPENRFSFS_ACCESS_READ && v2_b_present) {
        read_position = 0U;
        read_source = v2_b;
        read_source_bytes = v2_b_bytes;
        *handle = 1U;
        return OPENRFSFS_STATUS_OK;
    }
    if (strcmp(path, TEMP_PATH) == 0 &&
            access == OPENRFSFS_ACCESS_WRITE && pending_present) {
        *handle = 2U;
        return OPENRFSFS_STATUS_OK;
    }
    return OPENRFSFS_STATUS_NOT_FOUND;
}

enum openrfsfs_status openrfsfs_read(openrfsfs_handle handle,
    uint8_t *destination, size_t capacity, size_t *read_bytes)
{
    size_t length;

    if (handle != 1U || destination == NULL || read_bytes == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    length = read_source_bytes - read_position;
    if (length > capacity) {
        length = capacity;
    }
    memcpy(destination, read_source + read_position, length);
    read_position += length;
    *read_bytes = length;
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status openrfsfs_write(openrfsfs_handle handle,
    const uint8_t *source, size_t source_bytes, size_t *written_bytes)
{
    if (handle != 2U || source == NULL || written_bytes == NULL ||
            source_bytes > sizeof(pending) - pending_bytes) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    memcpy(pending + pending_bytes, source, source_bytes);
    pending_bytes += source_bytes;
    *written_bytes = source_bytes;
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status openrfsfs_close(openrfsfs_handle handle)
{
    return handle == 1U || handle == 2U ? OPENRFSFS_STATUS_OK :
        OPENRFSFS_STATUS_STALE_HANDLE;
}

enum openrfsfs_status openrfsfs_sync(enum openrfsfs_volume volume)
{
    if (volume != OPENRFSFS_VOLUME_DATA) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (fail_sync_once) {
        fail_sync_once = false;
        return OPENRFSFS_STATUS_IO;
    }
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status openrfsfs_rename(enum openrfsfs_volume volume,
    const char *source, const char *destination)
{
    if (volume != OPENRFSFS_VOLUME_DATA ||
            strcmp(source, TEMP_PATH) != 0 ||
            !pending_present) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (strcmp(destination, ACCOUNT_PATH) == 0) {
        memcpy(stored, pending, pending_bytes);
        stored_bytes = pending_bytes;
        stored_present = true;
    } else if (strcmp(destination, V2_A_PATH) == 0) {
        memcpy(v2_a, pending, pending_bytes);
        v2_a_bytes = pending_bytes;
        v2_a_present = true;
    } else if (strcmp(destination, V2_B_PATH) == 0) {
        memcpy(v2_b, pending, pending_bytes);
        v2_b_bytes = pending_bytes;
        v2_b_present = true;
    } else {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    pending_present = false;
    return OPENRFSFS_STATUS_OK;
}

static void build_legacy_record(const uint8_t *password,
    size_t password_bytes)
{
    static const uint8_t domain[] = "OpenRFS account password v1";
    uint8_t digest[32] = {0};
    uint8_t next[32];
    memset(stored, 0, sizeof(stored));
    memcpy(stored, "OGA1", 4U);
    stored[4] = 1U;
    stored[5] = 5U;
    stored[8] = 0U; stored[9] = 0x80U;
    for (size_t index = 0U; index < 16U; ++index) {
        stored[12U + index] = (uint8_t)(index + 1U);
    }
    memcpy(stored + USERNAME_OFFSET, "alice", 5U);
    for (uint32_t round = 0U; round < UINT32_C(32768); ++round) {
        struct package_state_sha256_context ctx;
        assert(package_state_sha256_initialize(&ctx) == PACKAGE_STATE_STATUS_OK);
        assert(package_state_sha256_update(&ctx, domain,
            sizeof(domain) - 1U) == PACKAGE_STATE_STATUS_OK);
        assert(package_state_sha256_update(&ctx, stored + 12U, 16U) ==
            PACKAGE_STATE_STATUS_OK);
        assert(package_state_sha256_update(&ctx, stored + USERNAME_OFFSET,
            5U) == PACKAGE_STATE_STATUS_OK);
        if (round != 0U) {
            assert(package_state_sha256_update(&ctx, digest, 32U) ==
                PACKAGE_STATE_STATUS_OK);
        }
        assert(package_state_sha256_update(&ctx, password,
            password_bytes) == PACKAGE_STATE_STATUS_OK);
        assert(package_state_sha256_finish(&ctx, next) == PACKAGE_STATE_STATUS_OK);
        memcpy(digest, next, sizeof(digest));
    }
    memcpy(stored + 60U, digest, sizeof(digest));
    assert(package_state_sha256(stored, CHECKSUM_OFFSET,
        stored + CHECKSUM_OFFSET) == PACKAGE_STATE_STATUS_OK);
    stored_bytes = sizeof(stored);
    stored_present = true;
    crypto_wipe(digest, sizeof(digest));
    crypto_wipe(next, sizeof(next));
}

int main(void)
{
    static const uint8_t password[] = "correct horse";
    static const uint8_t wrong[] = "correct house";
    static const uint8_t new_password[] = "another horse";
    uint8_t saved[ACCOUNT_V2_RECORD_BYTES];
    uint8_t saved_v1[RECORD_BYTES];
    uint8_t key[ACCOUNT_V2_KEY_BYTES];
    uint8_t after_change[ACCOUNT_V2_KEY_BYTES];
    bool configured = false;

    if (!check(account_self_test(), "record and backoff self-test") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_CLOCK_UNAVAILABLE,
            "login must refuse without a monotonic clock")) {
        return 1;
    }
    test_clock_started = true;
    refuse_kdf = true;
    if (!check(account_create("alice", password, sizeof(password) - 1U) ==
            ACCOUNT_STATUS_KDF_UNAVAILABLE, "KDF refusal must stop creation")) {
        return 1;
    }
    refuse_kdf = false;
    if (!check(account_create("alice", password, sizeof(password) - 1U) ==
            ACCOUNT_STATUS_OK, "production v2 account creation") ||
        !check(v2_a_present && !stored_present, "new account must write v2") ||
        !check(account_configured(&configured) == ACCOUNT_STATUS_OK &&
            configured, "created record must be readable")) {
        return 1;
    }
    for (size_t index = 0U; index < 3U; ++index) {
        if (!check(account_authenticate("alice", wrong,
                sizeof(wrong) - 1U) == ACCOUNT_STATUS_AUTHENTICATION_FAILED,
                "wrong password must fail")) {
            return 1;
        }
    }
    if (!check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_RATE_LIMITED,
            "correct password must not bypass active backoff")) {
        return 1;
    }
    test_now_ns = UINT64_C(1000000000);
    if (!check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_OK,
            "login must recover at deadline") ||
        !check(account_data_key(key), "login must unlock Data key") ||
        !check(account_authenticate("alice", wrong,
            sizeof(wrong) - 1U) == ACCOUNT_STATUS_AUTHENTICATION_FAILED,
            "success must reset failure count") ||
        !check(!account_data_key(key), "failed login must forget Data key") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_OK,
            "one later failure must not lock out")) {
        return 1;
    }
    assert(account_data_key(key));
    if (!check(account_change_password("alice", password,
            sizeof(password) - 1U, new_password, 4U) ==
                ACCOUNT_STATUS_INVALID_PASSWORD,
            "invalid replacement password must fail") ||
        !check(!account_session_active(),
            "invalid replacement password must revoke the Data session") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_OK,
            "old password must still unlock after invalid change")) {
        return 1;
    }
    fail_sync_once = true;
    if (!check(account_change_password("alice", password,
            sizeof(password) - 1U, new_password,
            sizeof(new_password) - 1U) == ACCOUNT_STATUS_IO,
            "interrupted password change must refuse") ||
        !check(!account_data_key(key),
            "interrupted change must revoke the active Data session") ||
        !check(v2_a_present && !v2_b_present,
            "old generation must survive interrupted change") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_OK,
            "old password must unlock after interruption")) {
        return 1;
    }
    if (!check(account_change_password("alice", password,
            sizeof(password) - 1U, new_password,
            sizeof(new_password) - 1U) == ACCOUNT_STATUS_OK,
            "password change must commit") ||
        !check(v2_b_present && !v2_a_present,
            "new generation must retire old slot") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_AUTHENTICATION_FAILED,
            "old password must be refused online") ||
        !check(account_authenticate("alice", new_password,
            sizeof(new_password) - 1U) == ACCOUNT_STATUS_OK,
            "new password must unlock") ||
        !check(account_data_key(after_change) &&
            memcmp(key, after_change, sizeof(key)) == 0,
            "password change must preserve Data key")) {
        return 1;
    }
    /* A torn inactive slot must not strand an intact credential.
     * Successful login retires the damaged peer before exposing its key. */
    memcpy(v2_a, v2_b, sizeof(v2_a));
    v2_a_bytes = sizeof(v2_a);
    v2_a_present = true;
    v2_a[6] = 1U;
    if (!check(account_authenticate("alice", new_password,
            sizeof(new_password) - 1U) == ACCOUNT_STATUS_OK,
            "valid v2 slot must survive malformed inactive slot") ||
        !check(v2_b_present && !v2_a_present,
            "malformed inactive slot must be retired")) {
        return 1;
    }
    memcpy(v2_a, v2_b, sizeof(v2_a));
    v2_a_bytes = sizeof(v2_a) - 1U;
    v2_a_present = true;
    if (!check(account_authenticate("alice", new_password,
            sizeof(new_password) - 1U) == ACCOUNT_STATUS_OK,
            "valid v2 slot must survive short inactive slot") ||
        !check(v2_b_present && !v2_a_present,
            "short inactive slot must be retired")) {
        return 1;
    }
    fail_unlink_v2_once = true;
    if (!check(account_change_password("alice", new_password,
            sizeof(new_password) - 1U, password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_CLEANUP_PENDING,
            "committed change must report pending retirement") ||
        !check(!account_data_key(key),
            "pending retirement must revoke the active Data session") ||
        !check(v2_a_present && v2_b_present,
            "both generations survive interrupted retirement")) {
        return 1;
    }
    fail_unlink_v2_once = true;
    if (!check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_IO,
            "login cleanup failure must report storage error") ||
        !check(!account_data_key(key),
            "login cleanup failure must not expose the Data key") ||
        !check(v2_a_present && v2_b_present,
            "both generations must remain for retry") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_OK,
            "new password login must retire stale generation") ||
        !check(v2_a_present && !v2_b_present,
            "later login must retire inactive slot")) {
        return 1;
    }
    memcpy(saved, v2_a, sizeof(saved));
    v2_a[53] = 0U;
    assert(package_state_sha256(v2_a, 188U, v2_a + 188U) ==
        PACKAGE_STATE_STATUS_OK);
    if (!check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_STORAGE_CORRUPT,
            "production login must reject noncanonical v2 name")) {
        return 1;
    }
    memcpy(v2_a, saved, sizeof(saved));
    v2_a[140] ^= 1U;
    assert(package_state_sha256(v2_a, 188U, v2_a + 188U) ==
        PACKAGE_STATE_STATUS_OK);
    if (!check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_STORAGE_CORRUPT,
            "production login must reject altered wrapped key")) {
        return 1;
    }
    memcpy(v2_a, saved, sizeof(saved));

    v2_a_present = false;
    build_legacy_record(password, sizeof(password) - 1U);
    memcpy(saved_v1, stored, sizeof(saved_v1));
    stored[USERNAME_OFFSET + 2U] = 0U;
    assert(package_state_sha256(stored, CHECKSUM_OFFSET,
        stored + CHECKSUM_OFFSET) == PACKAGE_STATE_STATUS_OK);
    if (!check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_STORAGE_CORRUPT,
            "v1 must reject noncanonical name length")) {
        return 1;
    }
    memcpy(stored, saved_v1, sizeof(saved_v1));
    stored[USERNAME_OFFSET + 6U] = 'x';
    assert(package_state_sha256(stored, CHECKSUM_OFFSET,
        stored + CHECKSUM_OFFSET) == PACKAGE_STATE_STATUS_OK);
    if (!check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_STORAGE_CORRUPT,
            "v1 must reject nonzero name padding")) {
        return 1;
    }
    memcpy(stored, saved_v1, sizeof(saved_v1));
    fail_sync_once = true;
    if (!check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_IO,
            "interrupted migration must not authenticate") ||
        !check(stored_present && !v2_a_present,
            "interrupted migration must preserve v1")) {
        return 1;
    }
    fail_unlink_v1_once = true;
    if (!check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_IO,
            "v1 retirement failure must refuse login") ||
        !check(v2_a_present && stored_present,
            "v2 and v1 remain for retirement retry") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_OK,
            "v2 login must finish v1 retirement") ||
        !check(v2_a_present && !stored_present,
            "durable v2 must retire v1") ||
        !check(account_data_key(key), "migrated login must unlock Data key")) {
        return 1;
    }
    if (!check(account_delete("alice", wrong,
            sizeof(wrong) - 1U) == ACCOUNT_STATUS_AUTHENTICATION_FAILED,
            "wrong password must not delete the account") ||
        !check(v2_a_present && !account_session_active(),
            "failed deletion must retain record and revoke session")) {
        return 1;
    }
    ordinary_file_present = true;
    if (!check(account_delete("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_UNENCRYPTED_DATA,
            "unencrypted Data file must prevent account deletion") ||
        !check(v2_a_present && !account_session_active(),
            "refused deletion must preserve credential and revoke session")) {
        return 1;
    }
    ordinary_file_present = false;
    memcpy(pending, v2_a, sizeof(v2_a));
    pending_bytes = sizeof(v2_a);
    pending_present = true;
    fail_unlink_v2_once = true;
    if (!check(account_delete("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_IO,
            "failed unlink must refuse deletion") ||
        !check(v2_a_present && !pending_present &&
                !account_session_active(),
            "failed unlink must retire staging and revoke session") ||
        !check(account_delete("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_OK,
            "correct password must delete the account") ||
        !check(!stored_present && !v2_a_present && !v2_b_present &&
                !account_session_active(),
            "deleted account must leave no active credential or session")) {
        return 1;
    }
    configured = true;
    if (!check(account_configured(&configured) == ACCOUNT_STATUS_OK &&
            !configured, "deleted account must report unconfigured") ||
        !check(account_create("alice", new_password,
            sizeof(new_password) - 1U) == ACCOUNT_STATUS_OK,
            "new account may be created after deletion") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_AUTHENTICATION_FAILED,
            "old password must not unlock the replacement account") ||
        !check(account_authenticate("alice", new_password,
            sizeof(new_password) - 1U) == ACCOUNT_STATUS_OK,
            "replacement account must authenticate")) {
        return 1;
    }
    puts("account v2 creation, login, password rotation/deletion, backoff and interrupted v1 migration passed");
    return 0;
}
