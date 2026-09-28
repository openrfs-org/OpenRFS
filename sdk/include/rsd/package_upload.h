/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_USER_PACKAGE_UPLOAD_H
#define RSD_USER_PACKAGE_UPLOAD_H

#include <stddef.h>
#include <stdint.h>

#include <rsd/abi.h>

struct rsd_package_upload_report {
    uint64_t actual_bytes;
    uint8_t actual_sha256[RSD_PACKAGE_UPLOAD_SHA256_BYTES];
    uint32_t result_flags;
};

long rsd_package_upload_open(void);
long rsd_package_upload_write(
    rsd_handle_t upload,
    const void *bytes,
    size_t byte_count
);
long rsd_package_upload_seal(
    rsd_handle_t upload,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[RSD_PACKAGE_UPLOAD_SHA256_BYTES],
    struct rsd_package_upload_report *report
);
long rsd_package_upload_close(rsd_handle_t upload);

#endif
