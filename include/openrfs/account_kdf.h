/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_ACCOUNT_KDF_H
#define OPENRFS_ACCOUNT_KDF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ACCOUNT_KDF_V2_ALGORITHM UINT32_C(2)
#define ACCOUNT_KDF_V2_VERSION UINT8_C(0x13)
#define ACCOUNT_KDF_V2_MEMORY_KIB UINT32_C(65536)
#define ACCOUNT_KDF_V2_PASSES UINT32_C(3)
#define ACCOUNT_KDF_V2_LANES UINT32_C(4)
#define ACCOUNT_KDF_V2_SALT_BYTES 16U
#define ACCOUNT_KDF_V2_OUTPUT_BYTES 32U

enum account_kdf_status {
    ACCOUNT_KDF_STATUS_OK = 0,
    ACCOUNT_KDF_STATUS_BAD_ARGUMENT,
    ACCOUNT_KDF_STATUS_UNSUPPORTED_PARAMETERS,
    ACCOUNT_KDF_STATUS_INTERRUPTS_DISABLED,
    ACCOUNT_KDF_STATUS_BUSY,
    ACCOUNT_KDF_STATUS_RESOURCE_UNAVAILABLE,
    ACCOUNT_KDF_STATUS_MAPPING_FAILURE,
    ACCOUNT_KDF_STATUS_CLEANUP_FAILURE
};

bool account_kdf_v2_parameters_supported(uint32_t algorithm,
    uint8_t version, uint32_t memory_kib, uint32_t passes,
    uint32_t lanes);
enum account_kdf_status account_kdf_v2_derive(
    const uint8_t salt[ACCOUNT_KDF_V2_SALT_BYTES],
    const uint8_t *password, size_t password_bytes,
    uint8_t output[ACCOUNT_KDF_V2_OUTPUT_BYTES]
);

#endif
