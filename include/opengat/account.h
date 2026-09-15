/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_ACCOUNT_H
#define OPENGAT_ACCOUNT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
bool account_self_test(void);
const char *account_status_string(enum account_status status);

#endif
