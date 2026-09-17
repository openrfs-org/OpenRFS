/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_USER_PACKAGE_UPLOAD_H
#define OPENRFS_USER_PACKAGE_UPLOAD_H

#include <stddef.h>
#include <stdint.h>

#include <openrfs/abi.h>

struct openrfs_package_upload_report {
    uint64_t actual_bytes;
    uint8_t actual_sha256[OPENRFS_PACKAGE_UPLOAD_SHA256_BYTES];
    uint32_t result_flags;
};

long openrfs_package_upload_open(void);
long openrfs_package_upload_write(
    openrfs_handle_t upload,
    const void *bytes,
    size_t byte_count
);
long openrfs_package_upload_seal(
    openrfs_handle_t upload,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[OPENRFS_PACKAGE_UPLOAD_SHA256_BYTES],
    struct openrfs_package_upload_report *report
);
long openrfs_package_upload_close(openrfs_handle_t upload);

#endif
