/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/account.h>
#include <openrfs/fat32_fs.h>
#include <openrfs/package_state.h>
#include <openrfs/random.h>

#define ACCOUNT_DIRECTORY "OPENRFS"
#define ACCOUNT_PATH "OPENRFS/LOGIN.DAT"
#define ACCOUNT_TEMP_PATH "OPENRFS/LOGIN.NEW"
#define ACCOUNT_RECORD_BYTES 124U
#define ACCOUNT_CHECKSUM_OFFSET 92U
#define ACCOUNT_SALT_OFFSET 12U
#define ACCOUNT_USERNAME_OFFSET 28U
#define ACCOUNT_DIGEST_OFFSET 60U
#define ACCOUNT_SALT_BYTES 16U
#define ACCOUNT_DIGEST_BYTES 32U
#define ACCOUNT_KDF_ROUNDS UINT32_C(32768)

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

static enum account_status read_record(uint8_t record[ACCOUNT_RECORD_BYTES])
{
    uint8_t extra = 0U;
    size_t completed = 0U;
    size_t read_bytes = 0U;
    openrfsfs_handle handle;
    enum openrfsfs_status status = openrfsfs_open(OPENRFSFS_VOLUME_DATA,
        ACCOUNT_PATH, OPENRFSFS_ACCESS_READ, &handle);

    if (status == OPENRFSFS_STATUS_NOT_FOUND) {
        return ACCOUNT_STATUS_NOT_CONFIGURED;
    }
    if (status != OPENRFSFS_STATUS_OK) {
        return status == OPENRFSFS_STATUS_NOT_MOUNTED ||
            status == OPENRFSFS_STATUS_ABSENT ?
            ACCOUNT_STATUS_STORAGE_UNAVAILABLE : ACCOUNT_STATUS_IO;
    }
    while (completed < ACCOUNT_RECORD_BYTES && status == OPENRFSFS_STATUS_OK) {
        status = openrfsfs_read(handle, record + completed,
            ACCOUNT_RECORD_BYTES - completed, &read_bytes);
        if (read_bytes == 0U) {
            break;
        }
        completed += read_bytes;
    }
    if (status == OPENRFSFS_STATUS_OK && completed == ACCOUNT_RECORD_BYTES) {
        status = openrfsfs_read(handle, &extra, 1U, &read_bytes);
    }
    if (openrfsfs_close(handle) != OPENRFSFS_STATUS_OK &&
            status == OPENRFSFS_STATUS_OK) {
        status = OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (status != OPENRFSFS_STATUS_OK) {
        return ACCOUNT_STATUS_IO;
    }
    if (completed != ACCOUNT_RECORD_BYTES || read_bytes != 0U) {
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
    return username_valid((const char *)(record + ACCOUNT_USERNAME_OFFSET), NULL) ?
        ACCOUNT_STATUS_OK : ACCOUNT_STATUS_STORAGE_CORRUPT;
}

static enum account_status load_record(uint8_t record[ACCOUNT_RECORD_BYTES])
{
    enum account_status status = read_record(record);

    return status == ACCOUNT_STATUS_OK ? validate_record(record) : status;
}

static enum account_status persist_record(const uint8_t record[ACCOUNT_RECORD_BYTES])
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
        status = openrfsfs_write(handle, record, ACCOUNT_RECORD_BYTES, &written);
    }
    if (opened && openrfsfs_close(handle) != OPENRFSFS_STATUS_OK &&
            status == OPENRFSFS_STATUS_OK) {
        status = OPENRFSFS_STATUS_STALE_HANDLE;
    }
    if (status == OPENRFSFS_STATUS_OK && written == ACCOUNT_RECORD_BYTES) {
        status = openrfsfs_sync(OPENRFSFS_VOLUME_DATA);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = openrfsfs_rename(OPENRFSFS_VOLUME_DATA, ACCOUNT_TEMP_PATH,
            ACCOUNT_PATH);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = openrfsfs_sync(OPENRFSFS_VOLUME_DATA);
    }
    if (status != OPENRFSFS_STATUS_OK || written != ACCOUNT_RECORD_BYTES) {
        (void)openrfsfs_unlink(OPENRFSFS_VOLUME_DATA, ACCOUNT_TEMP_PATH);
        return ACCOUNT_STATUS_IO;
    }
    return ACCOUNT_STATUS_OK;
}

enum account_status account_configured(bool *configured)
{
    uint8_t record[ACCOUNT_RECORD_BYTES];
    enum account_status status;

    if (configured == NULL) {
        return ACCOUNT_STATUS_NULL_ARGUMENT;
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
    uint8_t record[ACCOUNT_RECORD_BYTES] = { 0 };
    uint8_t existing[ACCOUNT_RECORD_BYTES];
    size_t username_bytes;
    enum account_status status;

    if (username == NULL || password == NULL) {
        return ACCOUNT_STATUS_NULL_ARGUMENT;
    }
    if (!username_valid(username, &username_bytes)) {
        return ACCOUNT_STATUS_INVALID_USERNAME;
    }
    if (!password_valid(password, password_bytes)) {
        return ACCOUNT_STATUS_INVALID_PASSWORD;
    }
    status = load_record(existing);
    zero_bytes(existing, sizeof(existing));
    if (status == ACCOUNT_STATUS_OK) {
        return ACCOUNT_STATUS_ALREADY_CONFIGURED;
    }
    if (status != ACCOUNT_STATUS_NOT_CONFIGURED) {
        return status;
    }
    copy_bytes(record, account_magic, sizeof(account_magic));
    record[4] = 1U;
    record[5] = (uint8_t)username_bytes;
    write_u32(record + 8U, ACCOUNT_KDF_ROUNDS);
    if (random_bytes(record + ACCOUNT_SALT_OFFSET, ACCOUNT_SALT_BYTES) !=
            RANDOM_STATUS_OK) {
        zero_bytes(record, sizeof(record));
        return ACCOUNT_STATUS_RANDOM_UNAVAILABLE;
    }
    copy_bytes(record + ACCOUNT_USERNAME_OFFSET, (const uint8_t *)username,
        username_bytes);
    status = derive_password(record + ACCOUNT_SALT_OFFSET,
        record + ACCOUNT_USERNAME_OFFSET, username_bytes, password,
        password_bytes, record + ACCOUNT_DIGEST_OFFSET);
    if (status == ACCOUNT_STATUS_OK &&
            package_state_sha256(record, ACCOUNT_CHECKSUM_OFFSET,
                record + ACCOUNT_CHECKSUM_OFFSET) != PACKAGE_STATE_STATUS_OK) {
        status = ACCOUNT_STATUS_IO;
    }
    if (status == ACCOUNT_STATUS_OK) {
        status = persist_record(record);
    }
    zero_bytes(record, sizeof(record));
    return status;
}

enum account_status account_authenticate(const char *username,
    const uint8_t *password, size_t password_bytes)
{
    uint8_t record[ACCOUNT_RECORD_BYTES];
    uint8_t digest[ACCOUNT_DIGEST_BYTES];
    size_t supplied_username_bytes;
    enum account_status status;

    if (username == NULL || password == NULL) {
        return ACCOUNT_STATUS_NULL_ARGUMENT;
    }
    if (!username_valid(username, &supplied_username_bytes) ||
            !password_valid(password, password_bytes)) {
        return ACCOUNT_STATUS_AUTHENTICATION_FAILED;
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
    zero_bytes(digest, sizeof(digest));
    zero_bytes(record, sizeof(record));
    return status;
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
        "invalid username or password"
    };

    _Static_assert(sizeof(messages) / sizeof(messages[0]) ==
        ACCOUNT_STATUS_COUNT, "account status table cardinality changed");
    return status < ACCOUNT_STATUS_COUNT ? messages[status] :
        "unknown account status";
}
