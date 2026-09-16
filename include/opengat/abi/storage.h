/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_ABI_STORAGE_H
#define OPENGAT_ABI_STORAGE_H

#include <opengat/abi/base.h>

#define OPENGAT_PATH_MAX 127U
#define OPENGAT_DIRECTORY_NAME_MAX 12U

enum opengat_volume {
    OPENGAT_VOLUME_SYSTEM = 1,
    OPENGAT_VOLUME_DATA = 2
};

enum opengat_open_flags {
    OPENGAT_OPEN_READ = UINT32_C(1) << 0,
    OPENGAT_OPEN_WRITE = UINT32_C(1) << 1,
    OPENGAT_OPEN_CREATE = UINT32_C(1) << 2,
    OPENGAT_OPEN_TRUNCATE = UINT32_C(1) << 3
};

#define OPENGAT_OPEN_FLAGS_V1 (OPENGAT_OPEN_READ | OPENGAT_OPEN_WRITE | \
    OPENGAT_OPEN_CREATE | OPENGAT_OPEN_TRUNCATE)

enum opengat_seek_origin {
    OPENGAT_SEEK_START = 0,
    OPENGAT_SEEK_CURRENT = 1,
    OPENGAT_SEEK_END = 2
};

struct opengat_path {
    uint64_t address;
    uint32_t length;
    uint16_t volume;
    uint16_t reserved;
} __attribute__((packed));

struct opengat_file_open_request {
    uint32_t size;
    uint32_t version;
    struct opengat_path path;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct opengat_io_request {
    uint32_t size;
    uint32_t version;
    opengat_handle_t handle;
    uint64_t buffer;
    uint64_t offset;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

struct opengat_seek_request {
    uint32_t size;
    uint32_t version;
    opengat_handle_t handle;
    int64_t offset;
    uint32_t origin;
    uint32_t reserved;
} __attribute__((packed));

struct opengat_path_stat {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint32_t attributes;
    uint32_t reserved;
} __attribute__((packed));

enum opengat_path_attributes {
    OPENGAT_PATH_DIRECTORY = UINT32_C(1) << 0,
    OPENGAT_PATH_READ_ONLY = UINT32_C(1) << 1
};

struct opengat_directory_entry {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint32_t attributes;
    uint16_t name_length;
    uint16_t reserved;
    uint8_t name[OPENGAT_DIRECTORY_NAME_MAX];
    uint32_t reserved_tail;
} __attribute__((packed));

struct opengat_rename_request {
    uint32_t size;
    uint32_t version;
    struct opengat_path source;
    struct opengat_path destination;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct opengat_volume_space {
    uint32_t size;
    uint32_t version;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t cluster_bytes;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct opengat_path) == 16U,
    "OpenGAT path ABI changed");
_Static_assert(sizeof(struct opengat_file_open_request) == 32U,
    "OpenGAT file-open ABI changed");
_Static_assert(sizeof(struct opengat_io_request) == 40U,
    "OpenGAT I/O ABI changed");
_Static_assert(sizeof(struct opengat_seek_request) == 32U,
    "OpenGAT seek ABI changed");
_Static_assert(sizeof(struct opengat_path_stat) == 24U,
    "OpenGAT stat ABI changed");
_Static_assert(sizeof(struct opengat_directory_entry) == 40U,
    "OpenGAT directory-entry ABI changed");
_Static_assert(sizeof(struct opengat_rename_request) == 48U,
    "OpenGAT rename ABI changed");
_Static_assert(sizeof(struct opengat_volume_space) == 32U,
    "OpenGAT volume-space ABI changed");

#endif
