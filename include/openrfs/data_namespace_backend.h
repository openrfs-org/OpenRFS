/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DATA_NAMESPACE_BACKEND_H
#define OPENRFS_DATA_NAMESPACE_BACKEND_H

#include <openrfs/data_aead_backend.h>
#include <openrfs/data_namespace.h>

/* Create only during a recorded migration; missing encrypted state is an error. */
enum data_ns_status data_ns_backend_create_empty(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], uint8_t *workspace,
    size_t workspace_bytes, uint64_t *generation);
enum data_ns_status data_ns_backend_load(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], uint8_t *workspace,
    size_t workspace_bytes, struct data_ns_state *state,
    uint64_t *generation);
enum data_ns_status data_ns_backend_append(
    const struct vfs_backend_ops *backend, enum openrfsfs_volume volume,
    const uint8_t key[DATA_AEAD_KEY_BYTES], uint8_t *workspace,
    size_t workspace_bytes, struct data_ns_state *state,
    struct data_ns_entry *scratch_entries, uint64_t expected_generation,
    const struct data_ns_event *event, uint64_t *generation);

#endif
