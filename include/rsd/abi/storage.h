/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_ABI_STORAGE_H
#define RSD_ABI_STORAGE_H

#include <rsd/abi/base.h>

#define RSD_PATH_MAX 127U
#define RSD_DIRECTORY_NAME_MAX 12U

/* PATH_MKDIR value 0 keeps legacy mode 0755. This flag admits an explicit
 * low-12-bit mode, including 0000, without changing existing callers. */
#define RSD_MKDIR_MODE_PRESENT (UINT64_C(1) << 16)

enum rsd_volume {
    RSD_VOLUME_SYSTEM = 1,
    RSD_VOLUME_DATA = 2
};

/* PATH_UNLINK's second argument. Zero preserves the original remove-any API. */
enum rsd_unlink_kind {
    RSD_UNLINK_ANY = 0,
    RSD_UNLINK_FILE = 1,
    RSD_UNLINK_DIRECTORY = 2,
};

enum rsd_open_flags {
    RSD_OPEN_READ = UINT32_C(1) << 0,
    RSD_OPEN_WRITE = UINT32_C(1) << 1,
    RSD_OPEN_CREATE = UINT32_C(1) << 2,
    RSD_OPEN_TRUNCATE = UINT32_C(1) << 3,
    RSD_OPEN_APPEND = UINT32_C(1) << 4,
    RSD_OPEN_MODE_PRESENT = UINT32_C(1) << 5,
    RSD_OPEN_EXCLUSIVE = UINT32_C(1) << 6,
};

#define RSD_OPEN_FLAGS_V1 (RSD_OPEN_READ | RSD_OPEN_WRITE | \
    RSD_OPEN_CREATE | RSD_OPEN_TRUNCATE | RSD_OPEN_APPEND | RSD_OPEN_MODE_PRESENT | RSD_OPEN_EXCLUSIVE)

enum rsd_seek_origin {
    RSD_SEEK_START = 0,
    RSD_SEEK_CURRENT = 1,
    RSD_SEEK_END = 2
};

struct rsd_path {
    uint64_t address;
    uint32_t length;
    uint16_t volume;
    uint16_t reserved;
} __attribute__((packed));

struct rsd_file_open_request {
    uint32_t size;
    uint32_t version;
    struct rsd_path path;
    uint32_t flags;
    /* Low-12-bit creation mode when MODE_PRESENT is set; otherwise zero.
     * Existing native requests retain their default creation mode of 0644. */
    uint32_t reserved;
} __attribute__((packed));

struct rsd_io_request {
    uint32_t size;
    uint32_t version;
    rsd_handle_t handle;
    uint64_t buffer;
    uint64_t offset;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

struct rsd_seek_request {
    uint32_t size;
    uint32_t version;
    rsd_handle_t handle;
    int64_t offset;
    uint32_t origin;
    uint32_t reserved;
} __attribute__((packed));

struct rsd_path_stat {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint32_t attributes;
    uint32_t reserved;
} __attribute__((packed));

#define RSD_METADATA_NOFOLLOW UINT32_C(1)
#define RSD_METADATA_UNIX_FIELDS UINT32_C(1)

/* Additive metadata ABI: PATH_STAT and its 24-byte output remain unchanged. */
struct rsd_path_metadata {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint64_t object_id;
    int64_t atime_seconds;
    int64_t mtime_seconds;
    int64_t ctime_seconds;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t links;
    uint32_t atime_nanos;
    uint32_t mtime_nanos;
    uint32_t ctime_nanos;
    uint32_t flags;
} __attribute__((packed));

_Static_assert(sizeof(struct rsd_path_metadata) == 80U, "path metadata ABI");

enum rsd_path_attributes {
    RSD_PATH_DIRECTORY = UINT32_C(1) << 0,
    RSD_PATH_READ_ONLY = UINT32_C(1) << 1
};

struct rsd_directory_entry {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint32_t attributes;
    uint16_t name_length;
    uint16_t reserved;
    uint8_t name[RSD_DIRECTORY_NAME_MAX];
    uint32_t reserved_tail;
} __attribute__((packed));

struct rsd_rename_request {
    uint32_t size;
    uint32_t version;
    struct rsd_path source;
    struct rsd_path destination;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed));

struct rsd_directory_entry_long {
    uint32_t size;
    uint32_t version;
    uint64_t byte_length;
    uint32_t attributes;
    uint16_t name_length;
    uint16_t reserved;
    uint8_t name[255];
    uint8_t reserved_tail;
} __attribute__((packed));

_Static_assert(sizeof(struct rsd_directory_entry_long) == 280U, "RSD long directory ABI changed");

struct rsd_volume_space {
    uint32_t size;
    uint32_t version;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t cluster_bytes;
    uint32_t reserved;
} __attribute__((packed));

enum rsd_xattr_operation {
    RSD_XATTR_GET = 0,
    RSD_XATTR_SET = 1,
    RSD_XATTR_REMOVE = 2
};

struct rsd_file_times {
    uint64_t atime_seconds;
    uint64_t mtime_seconds;
    uint32_t atime_nanos;
    uint32_t mtime_nanos;
} __attribute__((packed));

struct rsd_set_times_request {
    uint32_t size;
    uint32_t version;
    struct rsd_path path;
    struct rsd_file_times times;
} __attribute__((packed));

_Static_assert(sizeof(struct rsd_set_times_request) == 48U, "RSD set-times ABI changed");

struct rsd_xattr_request {
    uint32_t size;
    uint32_t version;
    struct rsd_path path;
    uint64_t name;
    uint32_t name_length;
    uint32_t operation;
    uint64_t value;
    uint32_t value_length;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct rsd_xattr_request) == 56U, "RSD xattr ABI changed");

_Static_assert(sizeof(struct rsd_path) == 16U,
    "RSD path ABI changed");
_Static_assert(sizeof(struct rsd_file_open_request) == 32U,
    "RSD file-open ABI changed");
_Static_assert(sizeof(struct rsd_io_request) == 40U,
    "RSD I/O ABI changed");
_Static_assert(sizeof(struct rsd_seek_request) == 32U,
    "RSD seek ABI changed");
_Static_assert(sizeof(struct rsd_path_stat) == 24U,
    "RSD stat ABI changed");
_Static_assert(sizeof(struct rsd_directory_entry) == 40U,
    "RSD directory-entry ABI changed");
_Static_assert(sizeof(struct rsd_rename_request) == 48U,
    "RSD rename ABI changed");
_Static_assert(sizeof(struct rsd_volume_space) == 32U,
    "RSD volume-space ABI changed");

#endif
