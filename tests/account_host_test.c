/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openrfs/account.h>
#include <openrfs/clock.h>
#include <openrfs/fat32_fs.h>
#include <openrfs/package_state.h>
#include <openrfs/random.h>

#define RECORD_BYTES 124U
#define CHECKSUM_OFFSET 92U
#define USERNAME_OFFSET 28U
#define ACCOUNT_PATH "OPENRFS/LOGIN.DAT"
#define TEMP_PATH "OPENRFS/LOGIN.NEW"

static uint8_t stored[RECORD_BYTES];
static uint8_t pending[RECORD_BYTES];
static size_t stored_bytes;
static size_t pending_bytes;
static size_t read_position;
static bool directory_present;
static bool stored_present;
static bool pending_present;
static bool test_clock_started;
static uint64_t test_now_ns;

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
    if (volume != OPENRFSFS_VOLUME_DATA ||
            strcmp(path, "OPENRFS") != 0 || !directory_present) {
        return OPENRFSFS_STATUS_NOT_FOUND;
    }
    memset(stat, 0, sizeof(*stat));
    stat->directory = true;
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
    if (volume != OPENRFSFS_VOLUME_DATA || strcmp(path, TEMP_PATH) != 0) {
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
    length = stored_bytes - read_position;
    if (length > capacity) {
        length = capacity;
    }
    memcpy(destination, stored + read_position, length);
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
    return volume == OPENRFSFS_VOLUME_DATA ? OPENRFSFS_STATUS_OK :
        OPENRFSFS_STATUS_INVALID_ARGUMENT;
}

enum openrfsfs_status openrfsfs_rename(enum openrfsfs_volume volume,
    const char *source, const char *destination)
{
    if (volume != OPENRFSFS_VOLUME_DATA ||
            strcmp(source, TEMP_PATH) != 0 ||
            strcmp(destination, ACCOUNT_PATH) != 0 || !pending_present) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    memcpy(stored, pending, pending_bytes);
    stored_bytes = pending_bytes;
    stored_present = true;
    pending_present = false;
    return OPENRFSFS_STATUS_OK;
}

int main(void)
{
    static const uint8_t password[] = "correct horse";
    static const uint8_t wrong[] = "correct house";
    uint8_t saved[RECORD_BYTES];
    bool configured = false;

    if (!check(account_self_test(), "record and backoff self-test") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_CLOCK_UNAVAILABLE,
            "login must refuse without a monotonic clock")) {
        return 1;
    }
    test_clock_started = true;
    if (!check(account_create("alice", password, sizeof(password) - 1U) ==
            ACCOUNT_STATUS_OK, "production account creation") ||
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
        !check(account_authenticate("alice", wrong,
            sizeof(wrong) - 1U) == ACCOUNT_STATUS_AUTHENTICATION_FAILED,
            "success must reset failure count") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_OK,
            "one later failure must not lock out")) {
        return 1;
    }
    memcpy(saved, stored, sizeof(saved));
    stored[USERNAME_OFFSET + 2U] = 0U;
    if (!check(package_state_sha256(stored, CHECKSUM_OFFSET,
            stored + CHECKSUM_OFFSET) == PACKAGE_STATE_STATUS_OK,
            "recompute tampered name checksum") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_STORAGE_CORRUPT,
            "production login must reject noncanonical name length")) {
        return 1;
    }
    memcpy(stored, saved, sizeof(saved));
    stored[USERNAME_OFFSET + 6U] = (uint8_t)'x';
    if (!check(package_state_sha256(stored, CHECKSUM_OFFSET,
            stored + CHECKSUM_OFFSET) == PACKAGE_STATE_STATUS_OK,
            "recompute tampered padding checksum") ||
        !check(account_authenticate("alice", password,
            sizeof(password) - 1U) == ACCOUNT_STATUS_STORAGE_CORRUPT,
            "production login must reject nonzero name padding")) {
        return 1;
    }
    puts("account creation, login, backoff, recovery and malformed-record refusal passed");
    return 0;
}
