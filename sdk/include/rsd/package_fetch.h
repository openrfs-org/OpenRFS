/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_USER_PACKAGE_FETCH_H
#define RSD_USER_PACKAGE_FETCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rsd/tls.h>
#include "package_upload.h"

#define RSD_PACKAGE_FETCH_SHA256_BYTES 32U
#define RSD_PACKAGE_FETCH_MAX_BYTES RSD_PACKAGE_UPLOAD_MAX_BYTES

enum rsd_package_fetch_status {
    RSD_PACKAGE_FETCH_OK = 0,
    RSD_PACKAGE_FETCH_ARGUMENT,
    RSD_PACKAGE_FETCH_OPEN,
    RSD_PACKAGE_FETCH_HTTPS,
    RSD_PACKAGE_FETCH_WRITE,
    RSD_PACKAGE_FETCH_CLOSE,
    RSD_PACKAGE_FETCH_LENGTH,
    RSD_PACKAGE_FETCH_DIGEST,
    RSD_PACKAGE_FETCH_SYNC,
    RSD_PACKAGE_FETCH_PUBLISH,
    RSD_PACKAGE_FETCH_UPLOAD_OPEN,
    RSD_PACKAGE_FETCH_UPLOAD_SEAL,
    RSD_PACKAGE_FETCH_COUNT
};

struct rsd_package_fetch_upload_request {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const char *path;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
    size_t expected_bytes;
    const uint8_t *expected_sha256;
};

struct rsd_package_fetch_request {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const char *path;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
    size_t maximum_bytes;
    /* Zero means the signed caller has not supplied an exact length yet. */
    size_t expected_bytes;
    /* NULL only when expected_bytes is zero (for an unparsed index fetch). */
    const uint8_t *expected_sha256;
    const char *temporary_path;
    const char *staged_path;
};

struct rsd_package_fetch_report {
    enum rsd_https_status https_status;
    int bearssl_error;
    long transport_error;
    long storage_error;
    long cleanup_error;
    rsd_handle_t upload;
    size_t bytes_received;
    uint8_t sha256[RSD_PACKAGE_FETCH_SHA256_BYTES];
    uint32_t upload_flags;
    bool published;
    bool durable;
};

/*
 * Fetch into an inert Data-volume staging path. This does not authenticate a
 * repository/package signature and cannot install or advance package state.
 */
enum rsd_package_fetch_status rsd_package_fetch_stage(
    const struct rsd_package_fetch_request *request,
    struct rsd_package_fetch_report *report
);

/*
 * Streams a package directly into a kernel-owned upload handle. Success leaves
 * report.upload open, exactly sealed to the supplied expected metadata, and
 * durable. Package control must independently bind that metadata to an
 * admitted signed repository before use. The caller must transfer the handle
 * to package control or close it.
 */
enum rsd_package_fetch_status rsd_package_fetch_upload(
    const struct rsd_package_fetch_upload_request *request,
    struct rsd_package_fetch_report *report
);

const char *rsd_package_fetch_status_string(
    enum rsd_package_fetch_status status
);

#endif
