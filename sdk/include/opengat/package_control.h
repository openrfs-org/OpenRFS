/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_USER_PACKAGE_CONTROL_H
#define OPENGAT_USER_PACKAGE_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include <opengat/abi.h>

struct opengat_package_control_report {
    uint64_t repository_version;
    uint64_t generation;
    uint32_t plan_count;
    uint32_t attached_count;
    uint32_t result_flags;
};

struct opengat_package_control_item {
    uint32_t index;
    uint32_t identifier_bytes;
    uint32_t version_bytes;
    uint32_t path_bytes;
    uint64_t package_bytes;
    uint8_t package_sha256[OPENGAT_PACKAGE_UPLOAD_SHA256_BYTES];
    char identifier[OPENGAT_PACKAGE_CONTROL_TEXT_BYTES];
    char version[OPENGAT_PACKAGE_CONTROL_TEXT_BYTES];
    char download_path[OPENGAT_PACKAGE_CONTROL_PATH_BYTES];
};

long opengat_package_control_open_install(
    opengat_handle_t repository_upload,
    const char *identifier,
    size_t identifier_bytes,
    struct opengat_package_control_report *report
);

long opengat_package_control_open_remove(
    const char *identifier,
    size_t identifier_bytes,
    struct opengat_package_control_report *report
);

long opengat_package_control_open_repair(
    opengat_handle_t repository_upload,
    struct opengat_package_control_report *report
);

long opengat_package_control_item(
    opengat_handle_t control,
    uint32_t index,
    struct opengat_package_control_item *item
);

long opengat_package_control_attach(
    opengat_handle_t control,
    uint32_t index,
    opengat_handle_t package_upload,
    struct opengat_package_control_report *report
);

long opengat_package_control_commit(
    opengat_handle_t control,
    struct opengat_package_control_report *report
);

long opengat_package_control_close(opengat_handle_t control);

#endif
