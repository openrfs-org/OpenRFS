/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_USER_PACKAGE_UPLOAD_H
#define TRAIT_USER_PACKAGE_UPLOAD_H

#include <stddef.h>
#include <stdint.h>

#include <trait/abi.h>

struct trait_package_upload_report {
    uint64_t actual_bytes;
    uint8_t actual_sha256[TRAIT_PACKAGE_UPLOAD_SHA256_BYTES];
    uint32_t result_flags;
};

long trait_package_upload_open(void);
long trait_package_upload_write(
    trait_handle_t upload,
    const void *bytes,
    size_t byte_count
);
long trait_package_upload_seal(
    trait_handle_t upload,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[TRAIT_PACKAGE_UPLOAD_SHA256_BYTES],
    struct trait_package_upload_report *report
);
long trait_package_upload_close(trait_handle_t upload);

#endif
