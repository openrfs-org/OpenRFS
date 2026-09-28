/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_ABI_PROCESS_H
#define OPENRFS_ABI_PROCESS_H

#include <stdint.h>

#include <openrfs/abi/base.h>

#define OPENRFS_EXEC_PATH_MAX 127U
#define OPENRFS_EXEC_VECTOR_MAX 32U
#define OPENRFS_EXEC_STRING_BYTES 8192U
#define OPENRFS_EXEC_DESCRIPTOR_COUNT 32U
#define OPENRFS_EXEC_FD_CLOEXEC UINT32_C(1)
#define OPENRFS_EXEC_FD_CLOFORK UINT32_C(2)
#define OPENRFS_AUX_EXEC_DESCRIPTORS UINT64_C(0x53500005)
#define OPENRFS_AUX_EXEC_DESCRIPTOR_COUNT UINT64_C(0x53500006)

enum openrfs_exec_descriptor_kind {
    OPENRFS_EXEC_DESCRIPTOR_NONE = 0,
    OPENRFS_EXEC_DESCRIPTOR_FILE = 1,
    OPENRFS_EXEC_DESCRIPTOR_CONSOLE_IN = 2,
    OPENRFS_EXEC_DESCRIPTOR_CONSOLE_OUT = 3,
    OPENRFS_EXEC_DESCRIPTOR_PIPE_READ = 4,
    OPENRFS_EXEC_DESCRIPTOR_PIPE_WRITE = 5
};

struct openrfs_exec_descriptor {
    openrfs_handle_t handle;
    uint32_t open_flags;
    uint32_t descriptor_flags;
    uint16_t volume;
    uint8_t kind;
    uint8_t active;
    uint32_t reserved;
};

struct openrfs_exec_request {
    uint32_t size;
    uint32_t version;
    uint64_t path_address;
    uint64_t argv_address;
    uint64_t envp_address;
    uint64_t descriptors_address;
    uint32_t descriptor_count;
    uint32_t reserved;
};

_Static_assert(sizeof(struct openrfs_exec_descriptor) == 24U,
    "exec descriptor ABI changed");
_Static_assert(sizeof(struct openrfs_exec_request) == 48U,
    "exec request ABI changed");

#endif
