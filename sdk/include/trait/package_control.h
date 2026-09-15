/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_USER_PACKAGE_CONTROL_H
#define TRAIT_USER_PACKAGE_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include <trait/abi.h>

struct trait_package_control_report {
    uint64_t repository_version;
    uint64_t generation;
    uint32_t plan_count;
    uint32_t attached_count;
    uint32_t result_flags;
};

struct trait_package_control_item {
    uint32_t index;
    uint32_t identifier_bytes;
    uint32_t version_bytes;
    uint32_t path_bytes;
    uint64_t package_bytes;
    uint8_t package_sha256[TRAIT_PACKAGE_UPLOAD_SHA256_BYTES];
    char identifier[TRAIT_PACKAGE_CONTROL_TEXT_BYTES];
    char version[TRAIT_PACKAGE_CONTROL_TEXT_BYTES];
    char download_path[TRAIT_PACKAGE_CONTROL_PATH_BYTES];
};

long trait_package_control_open_install(
    trait_handle_t repository_upload,
    const char *identifier,
    size_t identifier_bytes,
    struct trait_package_control_report *report
);

long trait_package_control_open_remove(
    const char *identifier,
    size_t identifier_bytes,
    struct trait_package_control_report *report
);

long trait_package_control_open_repair(
    trait_handle_t repository_upload,
    struct trait_package_control_report *report
);

long trait_package_control_item(
    trait_handle_t control,
    uint32_t index,
    struct trait_package_control_item *item
);

long trait_package_control_attach(
    trait_handle_t control,
    uint32_t index,
    trait_handle_t package_upload,
    struct trait_package_control_report *report
);

long trait_package_control_commit(
    trait_handle_t control,
    struct trait_package_control_report *report
);

long trait_package_control_close(trait_handle_t control);

#endif
