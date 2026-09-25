/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_AEAD_BACKEND_H
#define OPENRFS_DATA_AEAD_BACKEND_H

#include <stdbool.h>
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

/* A retained reader holds no key; supply the live session key for each read. */
struct data_aead_backend_reader {
    const struct vfs_backend_ops *backend;
    struct data_aead_manifest manifest;
    openrfsfs_handle segments[DATA_AEAD_SEGMENTS_MAX];
    uint64_t physical_bytes[DATA_AEAD_SEGMENTS_MAX];
    bool segment_open[DATA_AEAD_SEGMENTS_MAX];
    bool active;
};

enum data_aead_status data_aead_backend_reader_open(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_backend_reader *reader);
enum data_aead_status data_aead_backend_reader_read(
    const struct data_aead_backend_reader *reader,
    const uint8_t key[DATA_AEAD_KEY_BYTES], uint64_t offset,
    uint8_t *destination, size_t capacity, uint8_t *workspace,
    size_t workspace_bytes, size_t *read_bytes);
enum data_aead_status data_aead_backend_reader_close(
    struct data_aead_backend_reader *reader);

/* Only call under the durable migrating state with exclusive source access. */
enum data_aead_status data_aead_backend_migrate_plain(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *manifest);
enum data_aead_status data_aead_backend_migrate_plain_as(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *source_path,
    const char *binding_path, uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *manifest);

/* Destination and workspace are separate; a failed read clears destination. */
enum data_aead_status data_aead_backend_read_file(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t offset, uint8_t *destination, size_t capacity,
    uint8_t *workspace, size_t workspace_bytes, size_t *read_bytes);

enum data_aead_status data_aead_backend_append_file(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t expected_generation, const uint8_t *addition,
    size_t addition_bytes, uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *published);

enum data_aead_status data_aead_backend_rewrite_file(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *canonical_path,
    uint64_t expected_generation, uint64_t new_plaintext_bytes,
    uint64_t patch_offset, const uint8_t *patch, size_t patch_bytes,
    uint8_t *workspace, size_t workspace_bytes,
    struct data_aead_manifest *published);

#endif
