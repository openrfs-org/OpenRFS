/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_ABI_STORAGE_H
#define TRAIT_ABI_STORAGE_H

#include <trait/abi/base.h>

#define TRAIT_PATH_MAX 127U
#define TRAIT_DIRECTORY_NAME_MAX 12U

enum trait_volume {
    TRAIT_VOLUME_SYSTEM = 1,
    TRAIT_VOLUME_DATA = 2
};

enum trait_open_flags {
    TRAIT_OPEN_READ = UINT32_C(1) << 0,
    TRAIT_OPEN_WRITE = UINT32_C(1) << 1,
    TRAIT_OPEN_CREATE = UINT32_C(1) << 2,
    TRAIT_OPEN_TRUNCATE = UINT32_C(1) << 3
};

#define TRAIT_OPEN_FLAGS_V1 (TRAIT_OPEN_READ | TRAIT_OPEN_WRITE | \
    TRAIT_OPEN_CREATE | TRAIT_OPEN_TRUNCATE)

enum trait_seek_origin {
    TRAIT_SEEK_START = 0,
    TRAIT_SEEK_CURRENT = 1,
    TRAIT_SEEK_END = 2
};

struct trait_path {
    uint64_t address;
    uint32_t length;
    uint16_t volume;
    uint16_t reserved;
} __attribute__((packed));

struct trait_file_open_request {
    uint32_t size;
    uint32_t version;
    struct trait_path path;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct trait_io_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t handle;
    uint64_t buffer;
    uint64_t offset;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

struct trait_seek_request {
    uint32_t size;
    uint32_t version;
    trait_handle_t handle;
    int64_t offset;
    uint32_t origin;
    uint32_t reserved;
} __attribute__((packed));

struct trait_path_stat {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint32_t attributes;
    uint32_t reserved;
} __attribute__((packed));

enum trait_path_attributes {
    TRAIT_PATH_DIRECTORY = UINT32_C(1) << 0,
    TRAIT_PATH_READ_ONLY = UINT32_C(1) << 1
};

struct trait_directory_entry {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint32_t attributes;
    uint16_t name_length;
    uint16_t reserved;
    uint8_t name[TRAIT_DIRECTORY_NAME_MAX];
    uint32_t reserved_tail;
} __attribute__((packed));

struct trait_rename_request {
    uint32_t size;
    uint32_t version;
    struct trait_path source;
    struct trait_path destination;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct trait_volume_space {
    uint32_t size;
    uint32_t version;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t cluster_bytes;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct trait_path) == 16U,
    "Trait OS path ABI changed");
_Static_assert(sizeof(struct trait_file_open_request) == 32U,
    "Trait OS file-open ABI changed");
_Static_assert(sizeof(struct trait_io_request) == 40U,
    "Trait OS I/O ABI changed");
_Static_assert(sizeof(struct trait_seek_request) == 32U,
    "Trait OS seek ABI changed");
_Static_assert(sizeof(struct trait_path_stat) == 24U,
    "Trait OS stat ABI changed");
_Static_assert(sizeof(struct trait_directory_entry) == 40U,
    "Trait OS directory-entry ABI changed");
_Static_assert(sizeof(struct trait_rename_request) == 48U,
    "Trait OS rename ABI changed");
_Static_assert(sizeof(struct trait_volume_space) == 32U,
    "Trait OS volume-space ABI changed");

#endif
