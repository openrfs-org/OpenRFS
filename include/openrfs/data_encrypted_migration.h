/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_ENCRYPTED_MIGRATION_H
#define OPENRFS_DATA_ENCRYPTED_MIGRATION_H

#include <openrfs/data_aead_backend.h>

enum openrfsfs_status data_encrypted_migration_preflight(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES]);
enum openrfsfs_status data_encrypted_migration_run(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES]);

#endif
