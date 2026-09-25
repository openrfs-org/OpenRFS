/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_AEAD_BACKEND_H
#define OPENRFS_DATA_AEAD_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_aead_manifest.h>
#include <openrfs/vfs_backend.h>

/* The caller holds an exclusive lease on this logical path and its segments. */
enum data_aead_status data_aead_backend_publish_manifest(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    const struct data_aead_manifest *candidate,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *published, unsigned *slot);

enum data_aead_status data_aead_backend_load_manifest(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *manifest, unsigned *slot);

/* Only call under the durable migrating state with exclusive source access. */
enum data_aead_status data_aead_backend_migrate_plain(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *manifest);

/* Destination and workspace are separate; a failed read clears destination. */
enum data_aead_status data_aead_backend_read_file(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t offset, uint8_t *destination, size_t capacity,
    uint8_t *workspace, size_t workspace_bytes, size_t *read_bytes);

#endif
