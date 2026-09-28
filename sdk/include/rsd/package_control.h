/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_USER_PACKAGE_CONTROL_H
#define RSD_USER_PACKAGE_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include <rsd/abi.h>

struct rsd_package_control_report {
    uint64_t repository_version;
    uint64_t generation;
    uint32_t plan_count;
    uint32_t attached_count;
    uint32_t result_flags;
};

struct rsd_package_control_item {
    uint32_t index;
    uint32_t identifier_bytes;
    uint32_t version_bytes;
    uint32_t path_bytes;
    uint64_t package_bytes;
    uint8_t package_sha256[RSD_PACKAGE_UPLOAD_SHA256_BYTES];
    char identifier[RSD_PACKAGE_CONTROL_TEXT_BYTES];
    char version[RSD_PACKAGE_CONTROL_TEXT_BYTES];
    char download_path[RSD_PACKAGE_CONTROL_PATH_BYTES];
};

long rsd_package_control_open_install(
    rsd_handle_t repository_upload,
    const char *identifier,
    size_t identifier_bytes,
    struct rsd_package_control_report *report
);

long rsd_package_control_open_remove(
    const char *identifier,
    size_t identifier_bytes,
    struct rsd_package_control_report *report
);

long rsd_package_control_open_repair(
    rsd_handle_t repository_upload,
    struct rsd_package_control_report *report
);

long rsd_package_control_item(
    rsd_handle_t control,
    uint32_t index,
    struct rsd_package_control_item *item
);

long rsd_package_control_attach(
    rsd_handle_t control,
    uint32_t index,
    rsd_handle_t package_upload,
    struct rsd_package_control_report *report
);

long rsd_package_control_commit(
    rsd_handle_t control,
    struct rsd_package_control_report *report
);

long rsd_package_control_close(rsd_handle_t control);

#endif
