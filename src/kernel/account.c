/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/account.h>
#include <openrfs/account_v2.h>
#include <openrfs/clock.h>
#include <openrfs/fat32_fs.h>
#include <openrfs/package_state.h>
#include <openrfs/random.h>

#include "../../vendor/monocypher/src/monocypher.h"

#define ACCOUNT_DIRECTORY "OPENRFS"
#define ACCOUNT_PATH "OPENRFS/LOGIN.DAT"
#define ACCOUNT_TEMP_PATH "OPENRFS/LOGIN.NEW"
#define ACCOUNT_V2_A_PATH "OPENRFS/LOGIN.V2A"
#define ACCOUNT_V2_B_PATH "OPENRFS/LOGIN.V2B"
#define ACCOUNT_RECORD_BYTES 124U
#define ACCOUNT_CHECKSUM_OFFSET 92U
#define ACCOUNT_SALT_OFFSET 12U
#define ACCOUNT_USERNAME_OFFSET 28U
#define ACCOUNT_DIGEST_OFFSET 60U
#define ACCOUNT_SALT_BYTES 16U
#define ACCOUNT_DIGEST_BYTES 32U
#define ACCOUNT_KDF_ROUNDS UINT32_C(32768)
#define ACCOUNT_BACKOFF_INITIAL_NS UINT64_C(1000000000)
#define ACCOUNT_BACKOFF_MAX_NS UINT64_C(60000000000)
#define ACCOUNT_BACKOFF_FIRST_FAILURE 3U
#define ACCOUNT_BACKOFF_SATURATED_FAILURES 9U
#define ACCOUNT_DELETE_MAX_ENTRIES 1024U

struct account_throttle {
    uint32_t failures;
    uint64_t retry_after_ns;
};

static struct account_throttle login_throttle;
static uint8_t active_data_key[ACCOUNT_V2_KEY_BYTES];
static bool active_data_key_present;
static uint64_t active_data_epoch = 1U;
static struct account_data_storage_hooks data_storage_hooks;

void account_data_storage_install(
    const struct account_data_storage_hooks *hooks)
{
    if (hooks == NULL || hooks->preflight == NULL ||
            hooks->migrate == NULL || hooks->activate == NULL ||
            hooks->deactivate == NULL)
        return;
    if (data_storage_hooks.activate == NULL)
        data_storage_hooks = *hooks;
}

bool account_session_active(void)
{
    return __atomic_load_n(&active_data_key_present, __ATOMIC_ACQUIRE);
}

uint64_t account_session_generation(void)
{
    return __atomic_load_n(&active_data_epoch, __ATOMIC_ACQUIRE);
}

bool account_data_key(uint8_t out[ACCOUNT_V2_KEY_BYTES])
{
    const uint64_t generation = account_session_generation();
    if (out == NULL || !account_session_active()) {
        return false;
    }
    for (size_t index = 0U; index < sizeof(active_data_key); ++index) {
        out[index] = __atomic_load_n(&active_data_key[index],
            __ATOMIC_RELAXED);
    }
    if (!account_session_active() ||
            generation != account_session_generation()) {
        crypto_wipe(out, ACCOUNT_V2_KEY_BYTES);
        return false;
    }
    return true;
}

void account_data_key_forget(void)
{
    __atomic_store_n(&active_data_key_present, false, __ATOMIC_RELEASE);
    for (size_t index = 0U; index < sizeof(active_data_key); ++index)
        __atomic_store_n(&active_data_key[index], 0U, __ATOMIC_RELAXED);
    __atomic_add_fetch(&active_data_epoch, 1U, __ATOMIC_ACQ_REL);
    if (data_storage_hooks.deactivate != NULL)
        data_storage_hooks.deactivate();
}

static const uint8_t account_magic[4] = { 'O', 'G', 'A', '1' };
static const uint8_t account_domain[] = "OpenRFS account password v1";

static void copy_bytes(uint8_t *destination, const uint8_t *source, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static void zero_bytes(void *memory, size_t length)
{
    volatile uint8_t *bytes = memory;

    while (length != 0U) {
        *bytes++ = 0U;
        --length;
    }
}

static bool equal_bytes(const uint8_t *left, const uint8_t *right, size_t length)
{
    uint8_t difference = 0U;

    for (size_t index = 0U; index < length; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0U;
}

static bool throttle_allows(const struct account_throttle *throttle,
    uint64_t now)
{
    return now >= throttle->retry_after_ns;
}

static void throttle_failed(struct account_throttle *throttle, uint64_t now)
{
    uint64_t delay;

    if (throttle->failures < ACCOUNT_BACKOFF_SATURATED_FAILURES) {
        ++throttle->failures;
    }
    if (throttle->failures < ACCOUNT_BACKOFF_FIRST_FAILURE) {
        return;
    }
    delay = ACCOUNT_BACKOFF_INITIAL_NS <<
        (throttle->failures - ACCOUNT_BACKOFF_FIRST_FAILURE);
    if (delay > ACCOUNT_BACKOFF_MAX_NS) {
        delay = ACCOUNT_BACKOFF_MAX_NS;
    }
    throttle->retry_after_ns = UINT64_MAX - now < delay ? UINT64_MAX :
        now + delay;
}

static void throttle_succeeded(struct account_throttle *throttle)
{
    throttle->failures = 0U;
    throttle->retry_after_ns = 0U;
}

static size_t text_length(const char *text, size_t capacity)
{
    size_t length = 0U;

    if (text == NULL) {
        return capacity + 1U;
    }
    while (length <= capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool text_equal(const char *left, const char *right)
{
    size_t at = 0U;

    while (left[at] != '\0' && right[at] != '\0') {
        if (left[at] != right[at]) {
            return false;
        }
        ++at;
    }
    return left[at] == right[at];
}

static bool username_valid(const char *username, size_t *length_out)
{
    const size_t length = text_length(username, ACCOUNT_USERNAME_BYTES - 1U);

    if (length == 0U || length >= ACCOUNT_USERNAME_BYTES) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const char character = username[index];
        const bool alphanumeric =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9');

        if (!alphanumeric && character != '-' && character != '_') {
            return false;
        }
        if (index == 0U && !alphanumeric) {
            return false;
        }
    }
    if (length_out != NULL) {
        *length_out = length;
    }
    return true;
}

static bool password_valid(const uint8_t *password, size_t length)
{
    if (password == NULL || length < ACCOUNT_PASSWORD_MIN_BYTES ||
            length > ACCOUNT_PASSWORD_MAX_BYTES) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        if (password[index] < UINT8_C(0x20) || password[index] > UINT8_C(0x7e)) {
            return false;
        }
    }
    return true;
}

static void write_u32(uint8_t *destination, uint32_t value)
{
    for (size_t index = 0U; index < 4U; ++index) {
        destination[index] = (uint8_t)(value >> (index * 8U));
    }
}

static uint32_t read_u32(const uint8_t *source)
{
    uint32_t value = 0U;

    for (size_t index = 0U; index < 4U; ++index) {
        value |= (uint32_t)source[index] << (index * 8U);
    }
    return value;
}

static enum account_status digest_parts(
    const uint8_t *salt,
    const uint8_t *username,
    size_t username_bytes,
    const uint8_t *password,
    size_t password_bytes,
    const uint8_t *previous,
    uint8_t digest[ACCOUNT_DIGEST_BYTES]
)
{
    struct package_state_sha256_context context;
    enum package_state_status status = package_state_sha256_initialize(&context);

    if (status == PACKAGE_STATE_STATUS_OK) {
        status = package_state_sha256_update(&context, account_domain,
            sizeof(account_domain) - 1U);
    }
    if (status == PACKAGE_STATE_STATUS_OK) {
        status = package_state_sha256_update(&context, salt, ACCOUNT_SALT_BYTES);
    }
    if (status == PACKAGE_STATE_STATUS_OK) {
        status = package_state_sha256_update(&context, username, username_bytes);
    }
    if (status == PACKAGE_STATE_STATUS_OK && previous != NULL) {
        status = package_state_sha256_update(&context, previous,
            ACCOUNT_DIGEST_BYTES);
    }
    if (status == PACKAGE_STATE_STATUS_OK) {
        status = package_state_sha256_update(&context, password, password_bytes);
    }
    if (status == PACKAGE_STATE_STATUS_OK) {
        status = package_state_sha256_finish(&context, digest);
    }
    zero_bytes(&context, sizeof(context));
    return status == PACKAGE_STATE_STATUS_OK ? ACCOUNT_STATUS_OK :
        ACCOUNT_STATUS_IO;
}

static enum account_status derive_password(
    const uint8_t salt[ACCOUNT_SALT_BYTES],
    const uint8_t *username,
    size_t username_bytes,
    const uint8_t *password,
    size_t password_bytes,
    uint8_t digest[ACCOUNT_DIGEST_BYTES]
)
{
    uint8_t next[ACCOUNT_DIGEST_BYTES];
    enum account_status status = digest_parts(salt, username, username_bytes,
        password, password_bytes, NULL, digest);

    for (uint32_t round = 1U;
         status == ACCOUNT_STATUS_OK && round < ACCOUNT_KDF_ROUNDS; ++round) {
        status = digest_parts(salt, username, username_bytes, password,
            password_bytes, digest, next);
        if (status == ACCOUNT_STATUS_OK) {
            copy_bytes(digest, next, sizeof(next));
        }
    }
    zero_bytes(next, sizeof(next));
    return status;
}

static enum account_status read_record(const char *path, uint8_t *record,
    size_t record_bytes)
{
    uint8_t extra = 0U;
    size_t completed = 0U;
    size_t read_bytes = 0U;
    openrfsfs_handle handle;
    enum openrfsfs_status status = openrfsfs_open(OPENRFSFS_VOLUME_DATA,
        path, OPENRFSFS_ACCESS_READ, &handle);

    if (status == OPENRFSFS_STATUS_NOT_FOUND) {
        return ACCOUNT_STATUS_NOT_CONFIGURED;
    }
    if (status != OPENRFSFS_STATUS_OK) {
        return status == OPENRFSFS_STATUS_NOT_MOUNTED ||
            status == OPENRFSFS_STATUS_ABSENT ?
            ACCOUNT_STATUS_STORAGE_UNAVAILABLE : ACCOUNT_STATUS_IO;
    }
    while (completed < record_bytes && status == OPENRFSFS_STATUS_OK) {
        status = openrfsfs_read(handle, record + completed,
            record_bytes - completed, &read_bytes);
        if (read_bytes == 0U) {
            break;
        }
        completed += read_bytes;
    }
    if (status == OPENRFSFS_STATUS_OK && completed == record_bytes) {
        status = openrfsfs_read(handle, &extra, 1U, &read_bytes);
    }
    if (openrfsfs_close(handle) != OPENRFSFS_STATUS_OK &&
            status == OPENRFSFS_STATUS_OK) {
        status = OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (status != OPENRFSFS_STATUS_OK) {
        return ACCOUNT_STATUS_IO;
    }
    if (completed != record_bytes || read_bytes != 0U) {
        return ACCOUNT_STATUS_STORAGE_CORRUPT;
    }
    return ACCOUNT_STATUS_OK;
}

static enum account_status validate_record(uint8_t record[ACCOUNT_RECORD_BYTES])
{
    uint8_t checksum[ACCOUNT_DIGEST_BYTES];
    const uint8_t username_bytes = record[5];

    if (!equal_bytes(record, account_magic, sizeof(account_magic)) ||
            record[4] != 1U || username_bytes == 0U ||
            username_bytes >= ACCOUNT_USERNAME_BYTES || record[6] != 0U ||
            record[7] != 0U || read_u32(record + 8U) != ACCOUNT_KDF_ROUNDS ||
            record[ACCOUNT_USERNAME_OFFSET + username_bytes] != 0U) {
        return ACCOUNT_STATUS_STORAGE_CORRUPT;
    }
    if (package_state_sha256(record, ACCOUNT_CHECKSUM_OFFSET, checksum) !=
            PACKAGE_STATE_STATUS_OK ||
            !equal_bytes(checksum, record + ACCOUNT_CHECKSUM_OFFSET,
                sizeof(checksum))) {
        zero_bytes(checksum, sizeof(checksum));
        return ACCOUNT_STATUS_STORAGE_CORRUPT;
    }
    zero_bytes(checksum, sizeof(checksum));
    size_t actual_username_bytes = 0U;
    if (!username_valid((const char *)(record + ACCOUNT_USERNAME_OFFSET),
            &actual_username_bytes) || actual_username_bytes != username_bytes) {
        return ACCOUNT_STATUS_STORAGE_CORRUPT;
    }
    for (size_t index = (size_t)username_bytes + 1U;
         index < ACCOUNT_USERNAME_BYTES; ++index) {
        if (record[ACCOUNT_USERNAME_OFFSET + index] != 0U) {
            return ACCOUNT_STATUS_STORAGE_CORRUPT;
        }
    }
    return ACCOUNT_STATUS_OK;
}

static enum account_status load_record(uint8_t record[ACCOUNT_RECORD_BYTES])
{
    enum account_status status = read_record(ACCOUNT_PATH, record,
        ACCOUNT_RECORD_BYTES);

    return status == ACCOUNT_STATUS_OK ? validate_record(record) : status;
}

static enum account_status persist_record(const uint8_t *record,
    size_t record_bytes, const char *destination)
{
    struct openrfsfs_stat stat;
    openrfsfs_handle handle;
    size_t written = 0U;
    bool opened = false;
    enum openrfsfs_status status = openrfsfs_stat_path(OPENRFSFS_VOLUME_DATA,
        ACCOUNT_DIRECTORY, &stat);

    if (status == OPENRFSFS_STATUS_NOT_FOUND) {
        status = openrfsfs_mkdir(OPENRFSFS_VOLUME_DATA, ACCOUNT_DIRECTORY);
    } else if (status == OPENRFSFS_STATUS_OK && !stat.directory) {
        return ACCOUNT_STATUS_STORAGE_CORRUPT;
    }
    if (status != OPENRFSFS_STATUS_OK) {
        return status == OPENRFSFS_STATUS_NOT_MOUNTED ||
            status == OPENRFSFS_STATUS_ABSENT ||
            status == OPENRFSFS_STATUS_READ_ONLY ?
            ACCOUNT_STATUS_STORAGE_UNAVAILABLE : ACCOUNT_STATUS_IO;
    }
    status = openrfsfs_unlink(OPENRFSFS_VOLUME_DATA, ACCOUNT_TEMP_PATH);
    if (status != OPENRFSFS_STATUS_OK && status != OPENRFSFS_STATUS_NOT_FOUND) {
        return ACCOUNT_STATUS_IO;
    }
    status = openrfsfs_create(OPENRFSFS_VOLUME_DATA, ACCOUNT_TEMP_PATH);
    if (status == OPENRFSFS_STATUS_OK) {
        status = openrfsfs_open(OPENRFSFS_VOLUME_DATA, ACCOUNT_TEMP_PATH,
            OPENRFSFS_ACCESS_WRITE, &handle);
        opened = status == OPENRFSFS_STATUS_OK;
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = openrfsfs_write(handle, record, record_bytes, &written);
    }
    if (opened && openrfsfs_close(handle) != OPENRFSFS_STATUS_OK &&
            status == OPENRFSFS_STATUS_OK) {
        status = OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (status == OPENRFSFS_STATUS_OK && written == record_bytes) {
        status = openrfsfs_sync(OPENRFSFS_VOLUME_DATA);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = openrfsfs_rename(OPENRFSFS_VOLUME_DATA, ACCOUNT_TEMP_PATH,
            destination);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = openrfsfs_sync(OPENRFSFS_VOLUME_DATA);
    }
    if (status != OPENRFSFS_STATUS_OK || written != record_bytes) {
        (void)openrfsfs_unlink(OPENRFSFS_VOLUME_DATA, ACCOUNT_TEMP_PATH);
        return ACCOUNT_STATUS_IO;
    }
    return ACCOUNT_STATUS_OK;
}

static enum account_status load_v2_slot(const char *path,
    uint8_t record[ACCOUNT_V2_RECORD_BYTES])
{
    enum account_status status = read_record(path, record,
        ACCOUNT_V2_RECORD_BYTES);

    if (status == ACCOUNT_STATUS_OK &&
            account_v2_validate(record) != ACCOUNT_V2_OK) {
        status = ACCOUNT_STATUS_STORAGE_CORRUPT;
    }
    return status;
}

static enum account_status load_active_v2(
    uint8_t record[ACCOUNT_V2_RECORD_BYTES], const char **slot)
{
    uint8_t second[ACCOUNT_V2_RECORD_BYTES];
    enum account_status first_status = load_v2_slot(ACCOUNT_V2_A_PATH, record);
    enum account_status second_status = load_v2_slot(ACCOUNT_V2_B_PATH,
        second);

    /* A cut while publishing the inactive slot can leave that directory
     * entry present but malformed. Keep the intact slot available, then
     * retire the damaged peer only after authenticating the intact wrap.
     * Read I/O failures are not evidence of a torn record and still refuse. */
    if (first_status == ACCOUNT_STATUS_STORAGE_CORRUPT &&
            second_status == ACCOUNT_STATUS_OK) {
        first_status = ACCOUNT_STATUS_NOT_CONFIGURED;
    }
    if (second_status == ACCOUNT_STATUS_STORAGE_CORRUPT &&
            first_status == ACCOUNT_STATUS_OK) {
        second_status = ACCOUNT_STATUS_NOT_CONFIGURED;
    }
    if (first_status != ACCOUNT_STATUS_OK &&
            first_status != ACCOUNT_STATUS_NOT_CONFIGURED) {
        zero_bytes(second, sizeof(second));
        return first_status;
    }
    if (second_status != ACCOUNT_STATUS_OK &&
            second_status != ACCOUNT_STATUS_NOT_CONFIGURED) {
        zero_bytes(second, sizeof(second));
        return second_status;
    }
    if (first_status == ACCOUNT_STATUS_NOT_CONFIGURED &&
            second_status == ACCOUNT_STATUS_NOT_CONFIGURED) {
        zero_bytes(second, sizeof(second));
        return ACCOUNT_STATUS_NOT_CONFIGURED;
    }
    if (second_status == ACCOUNT_STATUS_OK &&
            (first_status == ACCOUNT_STATUS_NOT_CONFIGURED ||
             account_v2_generation(second) > account_v2_generation(record))) {
        copy_bytes(record, second, sizeof(second));
        if (slot != NULL) {
            *slot = ACCOUNT_V2_B_PATH;
        }
    } else if (first_status == ACCOUNT_STATUS_OK) {
        if (second_status == ACCOUNT_STATUS_OK &&
                account_v2_generation(second) ==
                    account_v2_generation(record)) {
            zero_bytes(second, sizeof(second));
            return ACCOUNT_STATUS_STORAGE_CORRUPT;
        }
        if (slot != NULL) {
            *slot = ACCOUNT_V2_A_PATH;
        }
    }
    zero_bytes(second, sizeof(second));
    return ACCOUNT_STATUS_OK;
}

static enum account_status v2_create(const char *username,
    const uint8_t *password, size_t password_bytes)
{
    uint8_t salt[16];
    uint8_t nonce[ACCOUNT_V2_NONCE_BYTES];
    uint8_t data_key[ACCOUNT_V2_KEY_BYTES];
    uint8_t record[ACCOUNT_V2_RECORD_BYTES];
    enum account_status result = ACCOUNT_STATUS_RANDOM_UNAVAILABLE;

    if (random_bytes(salt, sizeof(salt)) != RANDOM_STATUS_OK ||
            random_bytes(nonce, sizeof(nonce)) != RANDOM_STATUS_OK ||
            random_bytes(data_key, sizeof(data_key)) != RANDOM_STATUS_OK) {
        goto done;
    }
    const enum account_v2_status status = account_v2_seal(username,
        password, password_bytes, 1U, salt, nonce, data_key, record);
    if (status == ACCOUNT_V2_OK) {
        result = persist_record(record, sizeof(record), ACCOUNT_V2_A_PATH);
    } else {
        result = status == ACCOUNT_V2_KDF_UNAVAILABLE ?
            ACCOUNT_STATUS_KDF_UNAVAILABLE : ACCOUNT_STATUS_IO;
    }
done:
    zero_bytes(salt, sizeof(salt));
    zero_bytes(nonce, sizeof(nonce));
    zero_bytes(data_key, sizeof(data_key));
    zero_bytes(record, sizeof(record));
    return result;
}

static enum account_status prepare_encrypted_data(
    const char *username, const uint8_t *password,
    size_t password_bytes, uint8_t flags,
    const uint8_t data_key[ACCOUNT_V2_KEY_BYTES])
{
    if (data_storage_hooks.activate == NULL)
        return flags == 0U ? ACCOUNT_STATUS_OK :
            ACCOUNT_STATUS_ENCRYPTED_DATA_UNAVAILABLE;
    enum account_status result = ACCOUNT_STATUS_OK;
    if (flags == 0U) {
        const enum openrfsfs_status ready =
            data_storage_hooks.preflight(data_key);
        if (ready != OPENRFSFS_STATUS_OK)
            return ready == OPENRFSFS_STATUS_CORRUPT ?
                ACCOUNT_STATUS_STORAGE_CORRUPT : ACCOUNT_STATUS_IO;
        result = account_data_state_update(username, password,
            password_bytes, ACCOUNT_V2_FLAG_DATA_MIGRATING);
        if (result != ACCOUNT_STATUS_OK) return result;
        flags = ACCOUNT_V2_FLAG_DATA_MIGRATING;
    }
    if (flags == ACCOUNT_V2_FLAG_DATA_MIGRATING) {
        if (data_storage_hooks.migrate(data_key) !=
                OPENRFSFS_STATUS_OK)
            return ACCOUNT_STATUS_ENCRYPTED_DATA_UNAVAILABLE;
        result = account_data_state_update(username, password,
            password_bytes, ACCOUNT_V2_FLAG_DATA_ENCRYPTED);
        if (result != ACCOUNT_STATUS_OK) return result;
    } else if (flags != ACCOUNT_V2_FLAG_DATA_ENCRYPTED)
        return ACCOUNT_STATUS_STORAGE_CORRUPT;
    uint8_t final_record[ACCOUNT_V2_RECORD_BYTES];
    uint8_t checked_key[ACCOUNT_V2_KEY_BYTES];
    uint8_t final_flags = 0U;
    result = load_active_v2(final_record, NULL);
    if (result == ACCOUNT_STATUS_OK &&
            (account_v2_record_flags(final_record,
                &final_flags) != ACCOUNT_V2_OK ||
             final_flags != ACCOUNT_V2_FLAG_DATA_ENCRYPTED ||
             account_v2_open(final_record, username, password,
                password_bytes, checked_key) != ACCOUNT_V2_OK ||
             !equal_bytes(checked_key, data_key,
                 ACCOUNT_V2_KEY_BYTES)))
        result = ACCOUNT_STATUS_STORAGE_CORRUPT;
    zero_bytes(final_record, sizeof(final_record));
    zero_bytes(checked_key, sizeof(checked_key));
    if (result != ACCOUNT_STATUS_OK) return result;
    return data_storage_hooks.activate(data_key) ==
        OPENRFSFS_STATUS_OK ? ACCOUNT_STATUS_OK :
        ACCOUNT_STATUS_ENCRYPTED_DATA_UNAVAILABLE;
}

static enum account_status v2_authenticate(
    const uint8_t record[ACCOUNT_V2_RECORD_BYTES], const char *username,
    const uint8_t *password, size_t password_bytes)
{
    uint8_t data_key[ACCOUNT_V2_KEY_BYTES];
    account_data_key_forget();
    const enum account_v2_status status = account_v2_open(record, username,
        password, password_bytes, data_key);

    if (status == ACCOUNT_V2_OK) {
        const enum account_status prepared = prepare_encrypted_data(
            username, password, password_bytes, record[6],
            data_key);
        if (prepared != ACCOUNT_STATUS_OK) {
            zero_bytes(data_key, sizeof(data_key));
            return prepared;
        }
    }

    if (status == ACCOUNT_V2_OK) {
        for (size_t index = 0U; index < sizeof(active_data_key); ++index)
            __atomic_store_n(&active_data_key[index], data_key[index],
                __ATOMIC_RELAXED);
        __atomic_store_n(&active_data_key_present, true, __ATOMIC_RELEASE);
    }
    zero_bytes(data_key, sizeof(data_key));
    if (status == ACCOUNT_V2_OK) {
        return ACCOUNT_STATUS_OK;
    }
    if (status == ACCOUNT_V2_AUTHENTICATION_FAILED) {
        return ACCOUNT_STATUS_AUTHENTICATION_FAILED;
    }
    return status == ACCOUNT_V2_KDF_UNAVAILABLE ?
        ACCOUNT_STATUS_KDF_UNAVAILABLE : ACCOUNT_STATUS_STORAGE_CORRUPT;
}

static enum account_status retire_legacy_record(void)
{
    struct openrfsfs_stat stat;
    const enum openrfsfs_status present = openrfsfs_stat_path(
        OPENRFSFS_VOLUME_DATA, ACCOUNT_PATH, &stat);
    if (present == OPENRFSFS_STATUS_NOT_FOUND) {
        return ACCOUNT_STATUS_OK;
    }
    if (present != OPENRFSFS_STATUS_OK || stat.directory) {
        return ACCOUNT_STATUS_STORAGE_CORRUPT;
    }
    const enum openrfsfs_status removed = openrfsfs_unlink(
        OPENRFSFS_VOLUME_DATA, ACCOUNT_PATH);

    if (removed == OPENRFSFS_STATUS_NOT_FOUND) {
        return ACCOUNT_STATUS_OK;
    }
    if (removed != OPENRFSFS_STATUS_OK ||
            openrfsfs_sync(OPENRFSFS_VOLUME_DATA) != OPENRFSFS_STATUS_OK) {
        return ACCOUNT_STATUS_IO;
    }
    return ACCOUNT_STATUS_OK;
}

static enum account_status retire_previous_slot(const char *active_slot)
{
    struct openrfsfs_stat stat;
    const char *previous =
        active_slot[sizeof(ACCOUNT_V2_A_PATH) - 2U] == 'A' ?
        ACCOUNT_V2_B_PATH : ACCOUNT_V2_A_PATH;
    const enum openrfsfs_status present = openrfsfs_stat_path(
        OPENRFSFS_VOLUME_DATA, previous, &stat);

    if (present == OPENRFSFS_STATUS_NOT_FOUND) {
        return ACCOUNT_STATUS_OK;
    }
    if (present != OPENRFSFS_STATUS_OK || stat.directory) {
        return ACCOUNT_STATUS_STORAGE_CORRUPT;
    }
    const enum openrfsfs_status removed = openrfsfs_unlink(
        OPENRFSFS_VOLUME_DATA, previous);
    if (removed == OPENRFSFS_STATUS_NOT_FOUND) {
        return ACCOUNT_STATUS_OK;
    }
    if (removed != OPENRFSFS_STATUS_OK ||
            openrfsfs_sync(OPENRFSFS_VOLUME_DATA) != OPENRFSFS_STATUS_OK) {
        return ACCOUNT_STATUS_CLEANUP_PENDING;
    }
    return ACCOUNT_STATUS_OK;
}

static enum account_status deletion_data_census(void)
{
    char paths[OPENRFSFS_MAX_DEPTH][OPENRFSFS_MAX_PATH] = {{0}};
    openrfsfs_directory_handle handles[OPENRFSFS_MAX_DEPTH] = {0};
    struct openrfsfs_list_entry entry;
    size_t depth = 0U;
    size_t visited = 0U;
    enum account_status result = ACCOUNT_STATUS_OK;

    paths[0][0] = '.';
    if (openrfsfs_directory_open(OPENRFSFS_VOLUME_DATA, paths[0],
            &handles[0]) != OPENRFSFS_STATUS_OK) {
        return ACCOUNT_STATUS_IO;
    }
    for (;;) {
        bool present = false;
        enum openrfsfs_status status = openrfsfs_directory_read(
            handles[depth], &entry, &present);
        if (status != OPENRFSFS_STATUS_OK) {
            result = ACCOUNT_STATUS_IO;
            break;
        }
        if (!present) {
            if (openrfsfs_directory_close(handles[depth]) !=
                    OPENRFSFS_STATUS_OK) {
                result = ACCOUNT_STATUS_IO;
                handles[depth] = 0U;
                break;
            }
            handles[depth] = 0U;
            if (depth == 0U) {
                break;
            }
            --depth;
            continue;
        }
        if (++visited > ACCOUNT_DELETE_MAX_ENTRIES) {
            result = ACCOUNT_STATUS_UNENCRYPTED_DATA;
            break;
        }
        const size_t name_bytes = text_length(entry.name,
            OPENRFSFS_MAX_COMPONENT_BYTES - 1U);
        if (name_bytes == 0U || name_bytes >= OPENRFSFS_MAX_COMPONENT_BYTES ||
                (entry.name[0] == '.' &&
                    (entry.name[1] == '\0' ||
                     (entry.name[1] == '.' && entry.name[2] == '\0')))) {
            result = ACCOUNT_STATUS_STORAGE_CORRUPT;
            break;
        }
        for (size_t at = 0U; at < name_bytes; ++at) {
            if (entry.name[at] == '/' || entry.name[at] == '\\' ||
                    (uint8_t)entry.name[at] < 0x20U) {
                result = ACCOUNT_STATUS_STORAGE_CORRUPT;
                break;
            }
        }
        if (result != ACCOUNT_STATUS_OK) {
            break;
        }
        const size_t parent_bytes = paths[depth][0] == '.' &&
            paths[depth][1] == '\0' ? 0U : text_length(paths[depth],
                OPENRFSFS_MAX_PATH - 1U);
        const size_t child_bytes = parent_bytes +
            (parent_bytes != 0U ? 1U : 0U) + name_bytes;
        if (child_bytes >= OPENRFSFS_MAX_PATH) {
            result = ACCOUNT_STATUS_UNENCRYPTED_DATA;
            break;
        }
        char child[OPENRFSFS_MAX_PATH] = {0};
        if (parent_bytes != 0U) {
            copy_bytes((uint8_t *)child, (const uint8_t *)paths[depth],
                parent_bytes);
            child[parent_bytes] = '/';
        }
        copy_bytes((uint8_t *)child + parent_bytes +
            (parent_bytes != 0U ? 1U : 0U),
            (const uint8_t *)entry.name, name_bytes);
        if (!entry.directory) {
            if (!text_equal(child, ACCOUNT_PATH) &&
                    !text_equal(child, ACCOUNT_TEMP_PATH) &&
                    !text_equal(child, ACCOUNT_V2_A_PATH) &&
                    !text_equal(child, ACCOUNT_V2_B_PATH)) {
                result = ACCOUNT_STATUS_UNENCRYPTED_DATA;
                break;
            }
            continue;
        }
        if (depth + 1U >= OPENRFSFS_MAX_DEPTH) {
            result = ACCOUNT_STATUS_UNENCRYPTED_DATA;
            break;
        }
        ++depth;
        copy_bytes((uint8_t *)paths[depth], (const uint8_t *)child,
            child_bytes + 1U);
        status = openrfsfs_directory_open(OPENRFSFS_VOLUME_DATA,
            paths[depth], &handles[depth]);
        if (status != OPENRFSFS_STATUS_OK) {
            result = ACCOUNT_STATUS_IO;
            --depth;
            break;
        }
    }
    while (handles[depth] != 0U || depth != 0U) {
        if (handles[depth] != 0U &&
                openrfsfs_directory_close(handles[depth]) !=
                    OPENRFSFS_STATUS_OK) {
            result = ACCOUNT_STATUS_IO;
        }
        handles[depth] = 0U;
        if (depth == 0U) {
            break;
        }
        --depth;
    }
    zero_bytes(&entry, sizeof(entry));
    zero_bytes(paths, sizeof(paths));
    return result;
}

enum account_status account_configured(bool *configured)
{
    uint8_t record[ACCOUNT_RECORD_BYTES];
    uint8_t v2_record[ACCOUNT_V2_RECORD_BYTES];
    enum account_status status;

    if (configured == NULL) {
        return ACCOUNT_STATUS_NULL_ARGUMENT;
    }
    status = load_active_v2(v2_record, NULL);
    zero_bytes(v2_record, sizeof(v2_record));
    if (status == ACCOUNT_STATUS_OK) {
        *configured = true;
        return ACCOUNT_STATUS_OK;
    }
    if (status != ACCOUNT_STATUS_NOT_CONFIGURED) {
        *configured = false;
        return status;
    }
    status = load_record(record);
    zero_bytes(record, sizeof(record));
    if (status == ACCOUNT_STATUS_NOT_CONFIGURED) {
        *configured = false;
        return ACCOUNT_STATUS_OK;
    }
    if (status != ACCOUNT_STATUS_OK) {
        *configured = false;
        return status;
    }
    *configured = true;
    return ACCOUNT_STATUS_OK;
}

enum account_status account_create(const char *username,
    const uint8_t *password, size_t password_bytes)
{
    bool configured = false;
    enum account_status status;

    if (username == NULL || password == NULL) {
        return ACCOUNT_STATUS_NULL_ARGUMENT;
    }
    if (!username_valid(username, NULL)) {
        return ACCOUNT_STATUS_INVALID_USERNAME;
    }
    if (!password_valid(password, password_bytes)) {
        return ACCOUNT_STATUS_INVALID_PASSWORD;
    }
    status = account_configured(&configured);
    if (status != ACCOUNT_STATUS_OK) {
        return status;
    }
    if (configured) {
        return ACCOUNT_STATUS_ALREADY_CONFIGURED;
    }
    return v2_create(username, password, password_bytes);
}

enum account_status account_authenticate(const char *username,
    const uint8_t *password, size_t password_bytes)
{
    uint8_t record[ACCOUNT_RECORD_BYTES];
    uint8_t v2_record[ACCOUNT_V2_RECORD_BYTES];
    const char *active_slot = NULL;
    uint8_t digest[ACCOUNT_DIGEST_BYTES];
    size_t supplied_username_bytes;
    uint64_t now;
    enum account_status status;

    account_data_key_forget();
    if (username == NULL || password == NULL) {
        return ACCOUNT_STATUS_NULL_ARGUMENT;
    }
    if (!clock_is_started()) {
        return ACCOUNT_STATUS_CLOCK_UNAVAILABLE;
    }
    now = clock_monotonic_ns();
    if (!throttle_allows(&login_throttle, now)) {
        return ACCOUNT_STATUS_RATE_LIMITED;
    }
    if (!username_valid(username, &supplied_username_bytes) ||
            !password_valid(password, password_bytes)) {
        throttle_failed(&login_throttle, now);
        return ACCOUNT_STATUS_AUTHENTICATION_FAILED;
    }
    status = load_active_v2(v2_record, &active_slot);
    if (status == ACCOUNT_STATUS_OK) {
        status = v2_authenticate(v2_record, username, password,
            password_bytes);
        if (status == ACCOUNT_STATUS_OK) {
            status = retire_legacy_record();
            if (status == ACCOUNT_STATUS_OK) {
                uint8_t selected[ACCOUNT_V2_RECORD_BYTES];
                status = load_active_v2(selected, &active_slot);
                zero_bytes(selected, sizeof(selected));
                if (status == ACCOUNT_STATUS_OK)
                    status = retire_previous_slot(active_slot);
                if (status == ACCOUNT_STATUS_CLEANUP_PENDING) {
                    status = ACCOUNT_STATUS_IO;
                }
            }
            if (status != ACCOUNT_STATUS_OK) {
                account_data_key_forget();
            }
        }
        zero_bytes(v2_record, sizeof(v2_record));
        goto finish;
    }
    zero_bytes(v2_record, sizeof(v2_record));
    if (status != ACCOUNT_STATUS_NOT_CONFIGURED) {
        return status;
    }
    status = load_record(record);
    if (status != ACCOUNT_STATUS_OK) {
        zero_bytes(record, sizeof(record));
        return status;
    }
    status = derive_password(record + ACCOUNT_SALT_OFFSET,
        record + ACCOUNT_USERNAME_OFFSET, record[5], password, password_bytes,
        digest);
    if (status == ACCOUNT_STATUS_OK &&
            (supplied_username_bytes != record[5] ||
             !equal_bytes((const uint8_t *)username,
                record + ACCOUNT_USERNAME_OFFSET, record[5]) ||
             !equal_bytes(digest, record + ACCOUNT_DIGEST_OFFSET,
                sizeof(digest)))) {
        status = ACCOUNT_STATUS_AUTHENTICATION_FAILED;
    }
    if (status == ACCOUNT_STATUS_OK) {
        status = v2_create(username, password, password_bytes);
        if (status == ACCOUNT_STATUS_OK) {
            status = load_active_v2(v2_record, NULL);
            if (status == ACCOUNT_STATUS_OK) {
                status = v2_authenticate(v2_record, username, password,
                    password_bytes);
            }
            zero_bytes(v2_record, sizeof(v2_record));
        }
        if (status == ACCOUNT_STATUS_OK) {
            status = retire_legacy_record();
            if (status != ACCOUNT_STATUS_OK) {
                account_data_key_forget();
            }
        }
    }
    zero_bytes(digest, sizeof(digest));
    zero_bytes(record, sizeof(record));
finish:
    if (status == ACCOUNT_STATUS_AUTHENTICATION_FAILED) {
        throttle_failed(&login_throttle, clock_monotonic_ns());
    } else if (status == ACCOUNT_STATUS_OK) {
        throttle_succeeded(&login_throttle);
    }
    return status;
}

enum account_status account_data_state_update(const char *username,
    const uint8_t *password, size_t password_bytes, uint8_t next_flags)
{
    uint8_t current[ACCOUNT_V2_RECORD_BYTES] = {0};
    uint8_t replacement[ACCOUNT_V2_RECORD_BYTES] = {0};
    uint8_t data_key[ACCOUNT_V2_KEY_BYTES] = {0};
    uint8_t verified_key[ACCOUNT_V2_KEY_BYTES] = {0};
    uint8_t salt[16] = {0};
    uint8_t nonce[ACCOUNT_V2_NONCE_BYTES] = {0};
    uint8_t flags = 0U;
    const char *active_slot = NULL;
    const char *inactive_slot;
    enum account_status result;

    account_data_key_forget();
    if (username == NULL || password == NULL ||
            (next_flags != ACCOUNT_V2_FLAG_DATA_MIGRATING &&
             next_flags != ACCOUNT_V2_FLAG_DATA_ENCRYPTED)) {
        result = ACCOUNT_STATUS_NULL_ARGUMENT;
        goto done;
    }
    result = load_active_v2(current, &active_slot);
    if (result != ACCOUNT_STATUS_OK || active_slot == NULL ||
            account_v2_generation(current) == UINT64_MAX ||
            account_v2_record_flags(current, &flags) != ACCOUNT_V2_OK) {
        result = ACCOUNT_STATUS_STORAGE_CORRUPT;
        goto done;
    }
    const enum account_v2_status opened = account_v2_open(current, username,
        password, password_bytes, data_key);
    if (opened != ACCOUNT_V2_OK) {
        result = opened == ACCOUNT_V2_AUTHENTICATION_FAILED ?
            ACCOUNT_STATUS_AUTHENTICATION_FAILED :
            opened == ACCOUNT_V2_KDF_UNAVAILABLE ?
            ACCOUNT_STATUS_KDF_UNAVAILABLE : ACCOUNT_STATUS_STORAGE_CORRUPT;
        goto done;
    }
    if (flags == next_flags) {
        result = retire_previous_slot(active_slot);
        goto done;
    }
    if (!((flags == 0U &&
            next_flags == ACCOUNT_V2_FLAG_DATA_MIGRATING) ||
          (flags == ACCOUNT_V2_FLAG_DATA_MIGRATING &&
            next_flags == ACCOUNT_V2_FLAG_DATA_ENCRYPTED))) {
        result = ACCOUNT_STATUS_STORAGE_CORRUPT;
        goto done;
    }
    inactive_slot = active_slot[sizeof(ACCOUNT_V2_A_PATH) - 2U] == 'A' ?
        ACCOUNT_V2_B_PATH : ACCOUNT_V2_A_PATH;
    const enum openrfsfs_status removed = openrfsfs_unlink(
        OPENRFSFS_VOLUME_DATA, inactive_slot);
    if ((removed != OPENRFSFS_STATUS_OK &&
            removed != OPENRFSFS_STATUS_NOT_FOUND) ||
            (removed == OPENRFSFS_STATUS_OK &&
             openrfsfs_sync(OPENRFSFS_VOLUME_DATA) != OPENRFSFS_STATUS_OK)) {
        result = ACCOUNT_STATUS_IO;
        goto done;
    }
    if (random_bytes(salt, sizeof(salt)) != RANDOM_STATUS_OK ||
            random_bytes(nonce, sizeof(nonce)) != RANDOM_STATUS_OK) {
        result = ACCOUNT_STATUS_RANDOM_UNAVAILABLE;
        goto done;
    }
    const enum account_v2_status sealed = account_v2_seal_flags(username,
        password, password_bytes, account_v2_generation(current) + 1U,
        next_flags, salt, nonce, data_key, replacement);
    if (sealed != ACCOUNT_V2_OK) {
        result = sealed == ACCOUNT_V2_KDF_UNAVAILABLE ?
            ACCOUNT_STATUS_KDF_UNAVAILABLE : ACCOUNT_STATUS_IO;
        goto done;
    }
    result = persist_record(replacement, sizeof(replacement), inactive_slot);
    if (result != ACCOUNT_STATUS_OK) goto done;
    result = load_active_v2(current, NULL);
    if (result != ACCOUNT_STATUS_OK ||
            account_v2_generation(current) !=
                account_v2_generation(replacement) ||
            account_v2_record_flags(current, &flags) != ACCOUNT_V2_OK ||
            flags != next_flags ||
            account_v2_open(current, username, password, password_bytes,
                verified_key) != ACCOUNT_V2_OK ||
            !equal_bytes(data_key, verified_key, sizeof(data_key))) {
        result = ACCOUNT_STATUS_STORAGE_CORRUPT;
        goto done;
    }
    result = retire_previous_slot(inactive_slot);
done:
    zero_bytes(current, sizeof(current));
    zero_bytes(replacement, sizeof(replacement));
    zero_bytes(data_key, sizeof(data_key));
    zero_bytes(verified_key, sizeof(verified_key));
    zero_bytes(salt, sizeof(salt));
    zero_bytes(nonce, sizeof(nonce));
    account_data_key_forget();
    return result;
}

enum account_status account_change_password(const char *username,
    const uint8_t *old_password, size_t old_password_bytes,
    const uint8_t *new_password, size_t new_password_bytes)
{
    uint8_t current[ACCOUNT_V2_RECORD_BYTES];
    uint8_t replacement[ACCOUNT_V2_RECORD_BYTES];
    uint8_t salt[16];
    uint8_t nonce[ACCOUNT_V2_NONCE_BYTES];
    uint8_t authenticated_key[ACCOUNT_V2_KEY_BYTES];
    uint8_t verified_key[ACCOUNT_V2_KEY_BYTES] = {0};
    uint8_t record_flags = 0U;
    bool protected_state = false;
    const char *active_slot = NULL;
    const char *inactive_slot;
    enum account_status result;

    if (username == NULL || old_password == NULL || new_password == NULL) {
        account_data_key_forget();
        return ACCOUNT_STATUS_NULL_ARGUMENT;
    }
    if (!password_valid(new_password, new_password_bytes)) {
        account_data_key_forget();
        return ACCOUNT_STATUS_INVALID_PASSWORD;
    }
    result = account_authenticate(username, old_password, old_password_bytes);
    protected_state = result == ACCOUNT_STATUS_ENCRYPTED_DATA_UNAVAILABLE;
    if (result != ACCOUNT_STATUS_OK && !protected_state) {
        return result;
    }
    result = load_active_v2(current, &active_slot);
    if (result != ACCOUNT_STATUS_OK ||
            (!protected_state && !account_session_active()) ||
            account_v2_generation(current) == UINT64_MAX ||
            account_v2_record_flags(current, &record_flags) !=
                ACCOUNT_V2_OK ||
            (protected_state && record_flags == 0U)) {
        result = ACCOUNT_STATUS_STORAGE_CORRUPT;
        goto done;
    }
    if (protected_state) {
        const enum account_v2_status opened = account_v2_open(current,
            username, old_password, old_password_bytes, authenticated_key);
        if (opened != ACCOUNT_V2_OK) {
            result = opened == ACCOUNT_V2_AUTHENTICATION_FAILED ?
                ACCOUNT_STATUS_AUTHENTICATION_FAILED :
                opened == ACCOUNT_V2_KDF_UNAVAILABLE ?
                ACCOUNT_STATUS_KDF_UNAVAILABLE : ACCOUNT_STATUS_STORAGE_CORRUPT;
            goto done;
        }
    } else {
        if (!account_data_key(authenticated_key)) {
            result = ACCOUNT_STATUS_STORAGE_CORRUPT;
            goto done;
        }
        /* Authenticate the second read before copying its state into a new
         * generation. The checksum is not authority. */
        result = v2_authenticate(current, username, old_password,
            old_password_bytes);
        if (result != ACCOUNT_STATUS_OK) goto done;
        if (!account_data_key(verified_key) ||
                !equal_bytes(authenticated_key, verified_key,
                    sizeof(authenticated_key))) {
            result = ACCOUNT_STATUS_STORAGE_CORRUPT;
            goto done;
        }
    }
    inactive_slot = active_slot[sizeof(ACCOUNT_V2_A_PATH) - 2U] == 'A' ?
        ACCOUNT_V2_B_PATH : ACCOUNT_V2_A_PATH;
    if (random_bytes(salt, sizeof(salt)) != RANDOM_STATUS_OK ||
            random_bytes(nonce, sizeof(nonce)) != RANDOM_STATUS_OK) {
        result = ACCOUNT_STATUS_RANDOM_UNAVAILABLE;
        goto done;
    }
    const enum account_v2_status sealed = account_v2_seal_flags(username,
        new_password, new_password_bytes, account_v2_generation(current) + 1U,
        record_flags, salt, nonce, authenticated_key, replacement);
    if (sealed != ACCOUNT_V2_OK) {
        result = sealed == ACCOUNT_V2_KDF_UNAVAILABLE ?
            ACCOUNT_STATUS_KDF_UNAVAILABLE : ACCOUNT_STATUS_IO;
        goto done;
    }
    /* The selected slot remains intact through every operation on the
     * inactive slot. A cut before its rename keeps the old password valid;
     * a cut after it selects the new generation. */
    const enum openrfsfs_status removed = openrfsfs_unlink(
        OPENRFSFS_VOLUME_DATA, inactive_slot);
    if (removed != OPENRFSFS_STATUS_OK &&
            removed != OPENRFSFS_STATUS_NOT_FOUND) {
        result = ACCOUNT_STATUS_IO;
        goto done;
    }
    if (removed == OPENRFSFS_STATUS_OK &&
            openrfsfs_sync(OPENRFSFS_VOLUME_DATA) != OPENRFSFS_STATUS_OK) {
        result = ACCOUNT_STATUS_IO;
        goto done;
    }
    result = persist_record(replacement, sizeof(replacement), inactive_slot);
    if (result != ACCOUNT_STATUS_OK) {
        goto done;
    }
    result = load_active_v2(current, NULL);
    if (result != ACCOUNT_STATUS_OK ||
            account_v2_generation(current) !=
                account_v2_generation(replacement) ||
            account_v2_record_flags(current, &record_flags) !=
                ACCOUNT_V2_OK ||
            record_flags != replacement[6]) {
        result = ACCOUNT_STATUS_STORAGE_CORRUPT;
        goto done;
    }
    if (protected_state) {
        const enum account_v2_status opened = account_v2_open(current,
            username, new_password, new_password_bytes, verified_key);
        result = opened == ACCOUNT_V2_OK ? ACCOUNT_STATUS_OK :
            ACCOUNT_STATUS_STORAGE_CORRUPT;
    } else {
        result = v2_authenticate(current, username, new_password,
            new_password_bytes);
        if (result == ACCOUNT_STATUS_OK &&
                !account_data_key(verified_key))
            result = ACCOUNT_STATUS_STORAGE_CORRUPT;
    }
    if (result != ACCOUNT_STATUS_OK ||
            !equal_bytes(authenticated_key, verified_key,
                sizeof(authenticated_key))) {
        result = ACCOUNT_STATUS_STORAGE_CORRUPT;
        goto done;
    }
    if (openrfsfs_unlink(OPENRFSFS_VOLUME_DATA, active_slot) !=
            OPENRFSFS_STATUS_OK ||
            openrfsfs_sync(OPENRFSFS_VOLUME_DATA) != OPENRFSFS_STATUS_OK) {
        result = ACCOUNT_STATUS_CLEANUP_PENDING;
    }
done:
    zero_bytes(current, sizeof(current));
    zero_bytes(replacement, sizeof(replacement));
    zero_bytes(salt, sizeof(salt));
    zero_bytes(nonce, sizeof(nonce));
    zero_bytes(authenticated_key, sizeof(authenticated_key));
    zero_bytes(verified_key, sizeof(verified_key));
    if (result != ACCOUNT_STATUS_OK) {
        account_data_key_forget();
    }
    return result;
}

enum account_status account_delete(const char *username,
    const uint8_t *password, size_t password_bytes)
{
    uint8_t record[ACCOUNT_V2_RECORD_BYTES];
    const char *active_slot = NULL;
    enum account_status result;

    if (username == NULL || password == NULL) {
        account_data_key_forget();
        return ACCOUNT_STATUS_NULL_ARGUMENT;
    }
    /* Authentication also retires any legacy or previous v2 slot before the
     * last credential is removed. Every failure leaves the session locked. */
    result = account_authenticate(username, password, password_bytes);
    if (result != ACCOUNT_STATUS_OK) {
        return result;
    }
    result = deletion_data_census();
    if (result != ACCOUNT_STATUS_OK) {
        goto done;
    }
    result = load_active_v2(record, &active_slot);
    if (result != ACCOUNT_STATUS_OK || !account_session_active() ||
            active_slot == NULL) {
        result = ACCOUNT_STATUS_STORAGE_CORRUPT;
        goto done;
    }
    const enum openrfsfs_status removed_temp = openrfsfs_unlink(
        OPENRFSFS_VOLUME_DATA, ACCOUNT_TEMP_PATH);
    if ((removed_temp != OPENRFSFS_STATUS_OK &&
            removed_temp != OPENRFSFS_STATUS_NOT_FOUND) ||
            (removed_temp == OPENRFSFS_STATUS_OK &&
             openrfsfs_sync(OPENRFSFS_VOLUME_DATA) != OPENRFSFS_STATUS_OK)) {
        result = ACCOUNT_STATUS_IO;
        goto done;
    }
    if (openrfsfs_unlink(OPENRFSFS_VOLUME_DATA, active_slot) !=
            OPENRFSFS_STATUS_OK ||
            openrfsfs_sync(OPENRFSFS_VOLUME_DATA) != OPENRFSFS_STATUS_OK) {
        result = ACCOUNT_STATUS_IO;
        goto done;
    }
    result = ACCOUNT_STATUS_OK;
done:
    zero_bytes(record, sizeof(record));
    account_data_key_forget();
    return result;
}

bool account_self_test(void)
{
    static const uint8_t salt[ACCOUNT_SALT_BYTES] = {
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU
    };
    static const uint8_t first_password[] = "correct horse";
    static const uint8_t second_password[] = "correct house";
    uint8_t first[ACCOUNT_DIGEST_BYTES];
    uint8_t repeat[ACCOUNT_DIGEST_BYTES];
    uint8_t second[ACCOUNT_DIGEST_BYTES];
    uint8_t record[ACCOUNT_RECORD_BYTES] = {0};
    struct account_throttle throttle = {0};
    size_t username_bytes;
    bool passed = username_valid("alice_1", &username_bytes) &&
        username_bytes == 7U && !username_valid("-alice", NULL) &&
        !username_valid("bad/name", NULL) &&
        password_valid(first_password, sizeof(first_password) - 1U) &&
        !password_valid((const uint8_t *)"short", 5U) &&
        derive_password(salt, (const uint8_t *)"alice_1", 7U, first_password,
            sizeof(first_password) - 1U, first) == ACCOUNT_STATUS_OK &&
        derive_password(salt, (const uint8_t *)"alice_1", 7U, first_password,
            sizeof(first_password) - 1U, repeat) == ACCOUNT_STATUS_OK &&
        derive_password(salt, (const uint8_t *)"alice_1", 7U, second_password,
            sizeof(second_password) - 1U, second) == ACCOUNT_STATUS_OK &&
        equal_bytes(first, repeat, sizeof(first)) &&
        !equal_bytes(first, second, sizeof(first));

    copy_bytes(record, account_magic, sizeof(account_magic));
    record[4] = 1U;
    record[5] = 5U;
    write_u32(record + 8U, ACCOUNT_KDF_ROUNDS);
    copy_bytes(record + ACCOUNT_USERNAME_OFFSET, (const uint8_t *)"alice", 5U);
    passed = passed &&
        package_state_sha256(record, ACCOUNT_CHECKSUM_OFFSET,
            record + ACCOUNT_CHECKSUM_OFFSET) == PACKAGE_STATE_STATUS_OK &&
        validate_record(record) == ACCOUNT_STATUS_OK;

    record[ACCOUNT_USERNAME_OFFSET + 2U] = 0U;
    passed = passed &&
        package_state_sha256(record, ACCOUNT_CHECKSUM_OFFSET,
            record + ACCOUNT_CHECKSUM_OFFSET) == PACKAGE_STATE_STATUS_OK &&
        validate_record(record) == ACCOUNT_STATUS_STORAGE_CORRUPT;

    throttle_failed(&throttle, 0U);
    throttle_failed(&throttle, 0U);
    passed = passed && throttle_allows(&throttle, 0U);
    throttle_failed(&throttle, 0U);
    passed = passed && !throttle_allows(&throttle,
        ACCOUNT_BACKOFF_INITIAL_NS - 1U) &&
        throttle_allows(&throttle, ACCOUNT_BACKOFF_INITIAL_NS);
    throttle_failed(&throttle, ACCOUNT_BACKOFF_INITIAL_NS);
    passed = passed && throttle.retry_after_ns ==
        3U * ACCOUNT_BACKOFF_INITIAL_NS;
    for (size_t index = 0U; index < 5U; ++index) {
        const uint64_t attempted_at = throttle.retry_after_ns;

        throttle_failed(&throttle, attempted_at);
        if (index == 4U) {
            passed = passed && throttle.retry_after_ns - attempted_at ==
                ACCOUNT_BACKOFF_MAX_NS;
        }
    }
    passed = passed && throttle.failures ==
        ACCOUNT_BACKOFF_SATURATED_FAILURES;
    throttle_failed(&throttle, UINT64_MAX - 1U);
    passed = passed && throttle.retry_after_ns == UINT64_MAX &&
        !throttle_allows(&throttle, UINT64_MAX - 1U);
    throttle_succeeded(&throttle);
    passed = passed && throttle_allows(&throttle, 0U) &&
        throttle.failures == 0U;

    record[ACCOUNT_USERNAME_OFFSET + 2U] = (uint8_t)'i';
    record[ACCOUNT_USERNAME_OFFSET + 6U] = (uint8_t)'x';
    passed = passed &&
        package_state_sha256(record, ACCOUNT_CHECKSUM_OFFSET,
            record + ACCOUNT_CHECKSUM_OFFSET) == PACKAGE_STATE_STATUS_OK &&
        validate_record(record) == ACCOUNT_STATUS_STORAGE_CORRUPT;

    zero_bytes(record, sizeof(record));
    zero_bytes(first, sizeof(first));
    zero_bytes(repeat, sizeof(repeat));
    zero_bytes(second, sizeof(second));
    return passed;
}

const char *account_status_string(enum account_status status)
{
    static const char *const messages[] = {
        "ok",
        "null account argument",
        "username must start with a letter or number and use only letters, numbers, '-' or '_'",
        "password must contain 8-64 printable characters",
        "an OpenRFS account already exists",
        "no OpenRFS account exists",
        "the writable data volume is unavailable",
        "the OpenRFS account record is corrupt",
        "the account salt source is unavailable",
        "account storage failed",
        "invalid username or password",
        "monotonic clock is unavailable for account login",
        "account login is temporarily rate limited",
        "the bounded account KDF is unavailable",
        "password changed, but old credential cleanup is pending; use the new password",
        "Data still contains files; account deletion refused",
        "encrypted Data account requires the protected VFS path"
    };

    _Static_assert(sizeof(messages) / sizeof(messages[0]) ==
        ACCOUNT_STATUS_COUNT, "account status table cardinality changed");
    return status < ACCOUNT_STATUS_COUNT ? messages[status] :
        "unknown account status";
}
