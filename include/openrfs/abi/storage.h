/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_ABI_STORAGE_H
#define OPENRFS_ABI_STORAGE_H

#include <openrfs/abi/base.h>

#define OPENRFS_PATH_MAX 127U
#define OPENRFS_DIRECTORY_NAME_MAX 12U

enum openrfs_volume {
    OPENRFS_VOLUME_SYSTEM = 1,
    OPENRFS_VOLUME_DATA = 2
};

enum openrfs_open_flags {
    OPENRFS_OPEN_READ = UINT32_C(1) << 0,
    OPENRFS_OPEN_WRITE = UINT32_C(1) << 1,
    OPENRFS_OPEN_CREATE = UINT32_C(1) << 2,
    OPENRFS_OPEN_TRUNCATE = UINT32_C(1) << 3
};

#define OPENRFS_OPEN_FLAGS_V1 (OPENRFS_OPEN_READ | OPENRFS_OPEN_WRITE | \
    OPENRFS_OPEN_CREATE | OPENRFS_OPEN_TRUNCATE)

enum openrfs_seek_origin {
    OPENRFS_SEEK_START = 0,
    OPENRFS_SEEK_CURRENT = 1,
    OPENRFS_SEEK_END = 2
};

struct openrfs_path {
    uint64_t address;
    uint32_t length;
    uint16_t volume;
    uint16_t reserved;
} __attribute__((packed));

struct openrfs_file_open_request {
    uint32_t size;
    uint32_t version;
    struct openrfs_path path;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct openrfs_io_request {
    uint32_t size;
    uint32_t version;
    openrfs_handle_t handle;
    uint64_t buffer;
    uint64_t offset;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

struct openrfs_seek_request {
    uint32_t size;
    uint32_t version;
    openrfs_handle_t handle;
    int64_t offset;
    uint32_t origin;
    uint32_t reserved;
} __attribute__((packed));

struct openrfs_path_stat {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint32_t attributes;
    uint32_t reserved;
} __attribute__((packed));

enum openrfs_path_attributes {
    OPENRFS_PATH_DIRECTORY = UINT32_C(1) << 0,
    OPENRFS_PATH_READ_ONLY = UINT32_C(1) << 1
};

struct openrfs_directory_entry {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint32_t attributes;
    uint16_t name_length;
    uint16_t reserved;
    uint8_t name[OPENRFS_DIRECTORY_NAME_MAX];
    uint32_t reserved_tail;
} __attribute__((packed));

struct openrfs_rename_request {
    uint32_t size;
    uint32_t version;
    struct openrfs_path source;
    struct openrfs_path destination;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct openrfs_volume_space {
    uint32_t size;
    uint32_t version;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t cluster_bytes;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct openrfs_path) == 16U,
    "OpenRFS path ABI changed");
_Static_assert(sizeof(struct openrfs_file_open_request) == 32U,
    "OpenRFS file-open ABI changed");
_Static_assert(sizeof(struct openrfs_io_request) == 40U,
    "OpenRFS I/O ABI changed");
_Static_assert(sizeof(struct openrfs_seek_request) == 32U,
    "OpenRFS seek ABI changed");
_Static_assert(sizeof(struct openrfs_path_stat) == 24U,
    "OpenRFS stat ABI changed");
_Static_assert(sizeof(struct openrfs_directory_entry) == 40U,
    "OpenRFS directory-entry ABI changed");
_Static_assert(sizeof(struct openrfs_rename_request) == 48U,
    "OpenRFS rename ABI changed");
_Static_assert(sizeof(struct openrfs_volume_space) == 32U,
    "OpenRFS volume-space ABI changed");

#endif
