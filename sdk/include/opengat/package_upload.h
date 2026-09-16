/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_USER_PACKAGE_UPLOAD_H
#define OPENGAT_USER_PACKAGE_UPLOAD_H

#include <stddef.h>
#include <stdint.h>

#include <opengat/abi.h>

struct opengat_package_upload_report {
    uint64_t actual_bytes;
    uint8_t actual_sha256[OPENGAT_PACKAGE_UPLOAD_SHA256_BYTES];
    uint32_t result_flags;
};

long opengat_package_upload_open(void);
long opengat_package_upload_write(
    opengat_handle_t upload,
    const void *bytes,
    size_t byte_count
);
long opengat_package_upload_seal(
    opengat_handle_t upload,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[OPENGAT_PACKAGE_UPLOAD_SHA256_BYTES],
    struct opengat_package_upload_report *report
);
long opengat_package_upload_close(opengat_handle_t upload);

#endif
