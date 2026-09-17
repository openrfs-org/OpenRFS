/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_USER_PACKAGE_CONTROL_H
#define OPENRFS_USER_PACKAGE_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include <openrfs/abi.h>

struct openrfs_package_control_report {
    uint64_t repository_version;
    uint64_t generation;
    uint32_t plan_count;
    uint32_t attached_count;
    uint32_t result_flags;
};

struct openrfs_package_control_item {
    uint32_t index;
    uint32_t identifier_bytes;
    uint32_t version_bytes;
    uint32_t path_bytes;
    uint64_t package_bytes;
    uint8_t package_sha256[OPENRFS_PACKAGE_UPLOAD_SHA256_BYTES];
    char identifier[OPENRFS_PACKAGE_CONTROL_TEXT_BYTES];
    char version[OPENRFS_PACKAGE_CONTROL_TEXT_BYTES];
    char download_path[OPENRFS_PACKAGE_CONTROL_PATH_BYTES];
};

long openrfs_package_control_open_install(
    openrfs_handle_t repository_upload,
    const char *identifier,
    size_t identifier_bytes,
    struct openrfs_package_control_report *report
);

long openrfs_package_control_open_remove(
    const char *identifier,
    size_t identifier_bytes,
    struct openrfs_package_control_report *report
);

long openrfs_package_control_open_repair(
    openrfs_handle_t repository_upload,
    struct openrfs_package_control_report *report
);

long openrfs_package_control_item(
    openrfs_handle_t control,
    uint32_t index,
    struct openrfs_package_control_item *item
);

long openrfs_package_control_attach(
    openrfs_handle_t control,
    uint32_t index,
    openrfs_handle_t package_upload,
    struct openrfs_package_control_report *report
);

long openrfs_package_control_commit(
    openrfs_handle_t control,
    struct openrfs_package_control_report *report
);

long openrfs_package_control_close(openrfs_handle_t control);

#endif
