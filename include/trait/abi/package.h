/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_ABI_PACKAGE_H
#define TRAIT_ABI_PACKAGE_H

#include <trait/abi/base.h>

#define TRAIT_PACKAGE_UPLOAD_SHA256_BYTES UINT32_C(32)
#define TRAIT_PACKAGE_UPLOAD_WRITE_MAX UINT32_C(4096)
#define TRAIT_PACKAGE_UPLOAD_MAX_BYTES UINT32_C(16777216)

#define TRAIT_PACKAGE_UPLOAD_SEALED UINT32_C(1)
#define TRAIT_PACKAGE_UPLOAD_DURABLE UINT32_C(2)

#define TRAIT_PACKAGE_CONTROL_PLAN_MAX UINT32_C(8)
#define TRAIT_PACKAGE_CONTROL_TEXT_BYTES UINT32_C(128)
#define TRAIT_PACKAGE_CONTROL_PATH_BYTES UINT32_C(256)

#define TRAIT_PACKAGE_CONTROL_PREPARED UINT32_C(1)
#define TRAIT_PACKAGE_CONTROL_COMMITTED UINT32_C(2)

#define TRAIT_PACKAGE_CONTROL_OPEN_INSTALL UINT32_C(0)
#define TRAIT_PACKAGE_CONTROL_OPEN_REMOVE UINT32_C(1)
#define TRAIT_PACKAGE_CONTROL_OPEN_REPAIR UINT32_C(2)

struct trait_package_upload_write_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t handle;
    uint64_t buffer;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

struct trait_package_upload_seal_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t handle;
    uint64_t expected_bytes;
    uint8_t expected_sha256[TRAIT_PACKAGE_UPLOAD_SHA256_BYTES];
    uint64_t actual_bytes;
    uint8_t actual_sha256[TRAIT_PACKAGE_UPLOAD_SHA256_BYTES];
    uint32_t result_flags;
    uint32_t reserved;
} __attribute__((packed));

struct trait_package_control_open_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t repository_upload;
    uint64_t identifier;
    uint32_t identifier_bytes;
    uint32_t flags;
    uint64_t repository_version;
    uint64_t generation;
    uint32_t plan_count;
    uint32_t result_flags;
} __attribute__((packed));

struct trait_package_control_item_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t control;
    uint32_t index;
    uint32_t flags;
    uint64_t package_bytes;
    uint8_t package_sha256[TRAIT_PACKAGE_UPLOAD_SHA256_BYTES];
    uint32_t identifier_bytes;
    uint32_t version_bytes;
    uint32_t path_bytes;
    uint32_t reserved;
    char identifier[TRAIT_PACKAGE_CONTROL_TEXT_BYTES];
    char package_version[TRAIT_PACKAGE_CONTROL_TEXT_BYTES];
    char download_path[TRAIT_PACKAGE_CONTROL_PATH_BYTES];
} __attribute__((packed));

struct trait_package_control_attach_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t control;
    uint32_t index;
    uint32_t flags;
    trait_handle_t package_upload;
    uint32_t attached_count;
    uint32_t result_flags;
} __attribute__((packed));

struct trait_package_control_commit_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t control;
    uint32_t flags;
    uint32_t reserved;
    uint64_t generation;
    uint32_t plan_count;
    uint32_t attached_count;
    uint32_t result_flags;
    uint32_t result_reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct trait_package_upload_write_request) == 32U,
    "Trait OS package-upload write ABI changed");
_Static_assert(sizeof(struct trait_package_upload_seal_request) == 104U,
    "Trait OS package-upload seal ABI changed");
_Static_assert(sizeof(struct trait_package_control_open_request) == 56U,
    "Trait OS package-control open ABI changed");
_Static_assert(sizeof(struct trait_package_control_item_request) == 592U,
    "Trait OS package-control item ABI changed");
_Static_assert(sizeof(struct trait_package_control_attach_request) == 40U,
    "Trait OS package-control attach ABI changed");
_Static_assert(sizeof(struct trait_package_control_commit_request) == 48U,
    "Trait OS package-control commit ABI changed");

#endif
