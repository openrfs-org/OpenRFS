/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_USER_TLS_H
#define OPENRFS_USER_TLS_H

/* BearSSL 0.6 deliberately leaves BR_DOXYGEN_IGNORE undefined while using it
 * in two #if expressions.  Isolate that exact upstream -Wundef diagnostic so
 * OpenRFS applications can include this public header under -Werror without
 * changing BearSSL's many #ifndef BR_DOXYGEN_IGNORE declarations. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#endif
#include <bearssl.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <stddef.h>
#include <stdint.h>

struct openrfs_tls_client;

#define OPENRFS_HTTPS_MAX_HEADER_BYTES 4096U
#define OPENRFS_HTTPS_MAX_PATH_BYTES 1024U

enum openrfs_tls_status {
    OPENRFS_TLS_OK = 0,
    OPENRFS_TLS_ARGUMENT,
    OPENRFS_TLS_NO_MEMORY,
    OPENRFS_TLS_TRUST,
    OPENRFS_TLS_CLOCK,
    OPENRFS_TLS_ENTROPY,
    OPENRFS_TLS_DNS,
    OPENRFS_TLS_TRANSPORT,
    OPENRFS_TLS_HANDSHAKE,
    OPENRFS_TLS_IO,
    OPENRFS_TLS_CLOSE
};

struct openrfs_tls_client_config {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
};

/* Diagnostics are output-only.  They make a failed open auditable even though
 * no client object is returned to the caller. */
struct openrfs_tls_diagnostics {
    int bearssl_error;
    long transport_error;
};

enum openrfs_https_status {
    OPENRFS_HTTPS_OK = 0,
    OPENRFS_HTTPS_ARGUMENT,
    OPENRFS_HTTPS_NO_MEMORY,
    OPENRFS_HTTPS_TRUST,
    OPENRFS_HTTPS_CLOCK,
    OPENRFS_HTTPS_ENTROPY,
    OPENRFS_HTTPS_DNS,
    OPENRFS_HTTPS_TRANSPORT,
    OPENRFS_HTTPS_TIMEOUT,
    OPENRFS_HTTPS_CANCELED,
    OPENRFS_HTTPS_RESET,
    OPENRFS_HTTPS_TRUNCATED,
    OPENRFS_HTTPS_HOSTNAME,
    OPENRFS_HTTPS_CERTIFICATE_TIME,
    OPENRFS_HTTPS_AUTHENTICATION,
    OPENRFS_HTTPS_HANDSHAKE,
    OPENRFS_HTTPS_IO,
    OPENRFS_HTTPS_HTTP_VERSION,
    OPENRFS_HTTPS_HTTP_STATUS,
    OPENRFS_HTTPS_HTTP_HEADERS,
    OPENRFS_HTTPS_CONTENT_LENGTH_REQUIRED,
    OPENRFS_HTTPS_CONTENT_TOO_LARGE,
    OPENRFS_HTTPS_BODY_TRUNCATED,
    OPENRFS_HTTPS_BODY_EXTRA,
    OPENRFS_HTTPS_CLOSE,
    OPENRFS_HTTPS_BODY_WRITE
};

typedef long (*openrfs_https_body_write_function)(
    void *context,
    const void *bytes,
    size_t byte_count
);

struct openrfs_https_request {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const char *path;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
    void *body;
    size_t body_capacity;
};

struct openrfs_https_response {
    uint16_t status_code;
    uint16_t reserved;
    size_t content_length;
    size_t body_length;
    int bearssl_error;
    long transport_error;
};

struct openrfs_https_stream_request {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const char *path;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
    size_t body_limit;
    openrfs_https_body_write_function write_body;
    void *write_context;
};

enum openrfs_tls_status openrfs_tls_client_open(
    const struct openrfs_tls_client_config *config,
    struct openrfs_tls_client **result);
enum openrfs_tls_status openrfs_tls_client_open_diagnostic(
    const struct openrfs_tls_client_config *config,
    struct openrfs_tls_diagnostics *diagnostics,
    struct openrfs_tls_client **result);
long openrfs_tls_client_read(struct openrfs_tls_client *client, void *buffer,
    size_t length, uint64_t deadline_ns);
long openrfs_tls_client_write(struct openrfs_tls_client *client,
    const void *buffer, size_t length, uint64_t deadline_ns);
enum openrfs_tls_status openrfs_tls_client_flush(
    struct openrfs_tls_client *client, uint64_t deadline_ns);
long openrfs_tls_client_cancel(struct openrfs_tls_client *client);
enum openrfs_tls_status openrfs_tls_client_close(
    struct openrfs_tls_client *client, uint64_t deadline_ns);
enum openrfs_tls_status openrfs_tls_client_status(
    const struct openrfs_tls_client *client);
int openrfs_tls_client_bearssl_error(const struct openrfs_tls_client *client);
long openrfs_tls_client_transport_error(const struct openrfs_tls_client *client);
const char *openrfs_tls_status_string(enum openrfs_tls_status status);
enum openrfs_https_status openrfs_https_get(
    const struct openrfs_https_request *request,
    struct openrfs_https_response *response);
enum openrfs_https_status openrfs_https_get_stream(
    const struct openrfs_https_stream_request *request,
    struct openrfs_https_response *response);
const char *openrfs_https_status_string(enum openrfs_https_status status);

#endif
