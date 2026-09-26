/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_ENCRYPTED_BACKEND_H
#define OPENRFS_DATA_ENCRYPTED_BACKEND_H

#include <openrfs/data_aead_backend.h>

void data_encrypted_backend_bind(const struct vfs_backend_ops *physical,
    void (*forget_key)(void));
enum openrfsfs_status data_encrypted_backend_activate(
    const uint8_t key[DATA_AEAD_KEY_BYTES]);
enum openrfsfs_status data_encrypted_backend_migration_preflight(
    const uint8_t key[DATA_AEAD_KEY_BYTES]);
enum openrfsfs_status data_encrypted_backend_migrate(
    const uint8_t key[DATA_AEAD_KEY_BYTES]);
void data_encrypted_backend_deactivate(void);
const struct vfs_backend_ops *data_encrypted_backend_ops(void);

#endif
