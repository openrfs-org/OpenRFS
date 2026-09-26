/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_ACCOUNT_H
#define OPENRFS_ACCOUNT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/fat32_fs.h>

#define ACCOUNT_USERNAME_BYTES 32U
#define ACCOUNT_PASSWORD_MIN_BYTES 8U
#define ACCOUNT_PASSWORD_MAX_BYTES 64U

enum account_status {
    ACCOUNT_STATUS_OK = 0,
    ACCOUNT_STATUS_NULL_ARGUMENT,
    ACCOUNT_STATUS_INVALID_USERNAME,
    ACCOUNT_STATUS_INVALID_PASSWORD,
    ACCOUNT_STATUS_ALREADY_CONFIGURED,
    ACCOUNT_STATUS_NOT_CONFIGURED,
    ACCOUNT_STATUS_STORAGE_UNAVAILABLE,
    ACCOUNT_STATUS_STORAGE_CORRUPT,
    ACCOUNT_STATUS_RANDOM_UNAVAILABLE,
    ACCOUNT_STATUS_IO,
    ACCOUNT_STATUS_AUTHENTICATION_FAILED,
    ACCOUNT_STATUS_CLOCK_UNAVAILABLE,
    ACCOUNT_STATUS_RATE_LIMITED,
    ACCOUNT_STATUS_KDF_UNAVAILABLE,
    ACCOUNT_STATUS_CLEANUP_PENDING,
    ACCOUNT_STATUS_UNENCRYPTED_DATA,
    ACCOUNT_STATUS_ENCRYPTED_DATA_UNAVAILABLE,
    ACCOUNT_STATUS_COUNT
};

enum account_status account_configured(bool *configured);
enum account_status account_create(
    const char *username,
    const uint8_t *password,
    size_t password_bytes
);
enum account_status account_authenticate(
    const char *username,
    const uint8_t *password,
    size_t password_bytes
);
enum account_status account_change_password(
    const char *username, const uint8_t *old_password,
    size_t old_password_bytes, const uint8_t *new_password,
    size_t new_password_bytes);
/* The caller durably verifies the encrypted tree before setting completion. */
enum account_status account_data_state_update(const char *username,
    const uint8_t *password, size_t password_bytes, uint8_t next_flags);
struct account_data_storage_hooks {
    enum openrfsfs_status (*preflight)(const uint8_t key[32]);
    enum openrfsfs_status (*migrate)(const uint8_t key[32]);
    enum openrfsfs_status (*activate)(const uint8_t key[32]);
    void (*deactivate)(void);
};
void account_data_storage_install(
    const struct account_data_storage_hooks *hooks);
/* Remove the only active credential after authenticating it. This revokes
 * access but does not erase existing Data blocks or provide rollback defense. */
enum account_status account_delete(const char *username,
    const uint8_t *password, size_t password_bytes);
bool account_data_key(uint8_t out[32]);
/* True only after successful credential authentication in this boot. */
bool account_session_active(void);
uint64_t account_session_generation(void);
void account_data_key_forget(void);
bool account_self_test(void);
const char *account_status_string(enum account_status status);

#endif
