/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_USER_TLS_H
#define RSD_USER_TLS_H

/* BearSSL 0.6 deliberately leaves BR_DOXYGEN_IGNORE undefined while using it
 * in two #if expressions.  Isolate that exact upstream -Wundef diagnostic so
 * RSD applications can include this public header under -Werror without
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

struct rsd_tls_client;

#define RSD_HTTPS_MAX_HEADER_BYTES 4096U
#define RSD_HTTPS_MAX_PATH_BYTES 1024U

enum rsd_tls_status {
    RSD_TLS_OK = 0,
    RSD_TLS_ARGUMENT,
    RSD_TLS_NO_MEMORY,
    RSD_TLS_TRUST,
    RSD_TLS_CLOCK,
    RSD_TLS_ENTROPY,
    RSD_TLS_DNS,
    RSD_TLS_TRANSPORT,
    RSD_TLS_HANDSHAKE,
    RSD_TLS_IO,
    RSD_TLS_CLOSE
};

struct rsd_tls_client_config {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
};

/* Diagnostics are output-only.  They make a failed open auditable even though
 * no client object is returned to the caller. */
struct rsd_tls_diagnostics {
    int bearssl_error;
    long transport_error;
};

enum rsd_https_status {
    RSD_HTTPS_OK = 0,
    RSD_HTTPS_ARGUMENT,
    RSD_HTTPS_NO_MEMORY,
    RSD_HTTPS_TRUST,
    RSD_HTTPS_CLOCK,
    RSD_HTTPS_ENTROPY,
    RSD_HTTPS_DNS,
    RSD_HTTPS_TRANSPORT,
    RSD_HTTPS_TIMEOUT,
    RSD_HTTPS_CANCELED,
    RSD_HTTPS_RESET,
    RSD_HTTPS_TRUNCATED,
    RSD_HTTPS_HOSTNAME,
    RSD_HTTPS_CERTIFICATE_TIME,
    RSD_HTTPS_AUTHENTICATION,
    RSD_HTTPS_HANDSHAKE,
    RSD_HTTPS_IO,
    RSD_HTTPS_HTTP_VERSION,
    RSD_HTTPS_HTTP_STATUS,
    RSD_HTTPS_HTTP_HEADERS,
    RSD_HTTPS_CONTENT_LENGTH_REQUIRED,
    RSD_HTTPS_CONTENT_TOO_LARGE,
    RSD_HTTPS_BODY_TRUNCATED,
    RSD_HTTPS_BODY_EXTRA,
    RSD_HTTPS_CLOSE,
    RSD_HTTPS_BODY_WRITE
};

typedef long (*rsd_https_body_write_function)(
    void *context,
    const void *bytes,
    size_t byte_count
);

struct rsd_https_request {
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

struct rsd_https_response {
    uint16_t status_code;
    uint16_t reserved;
    size_t content_length;
    size_t body_length;
    int bearssl_error;
    long transport_error;
};

struct rsd_https_stream_request {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const char *path;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
    size_t body_limit;
    rsd_https_body_write_function write_body;
    void *write_context;
};

enum rsd_tls_status rsd_tls_client_open(
    const struct rsd_tls_client_config *config,
    struct rsd_tls_client **result);
enum rsd_tls_status rsd_tls_client_open_diagnostic(
    const struct rsd_tls_client_config *config,
    struct rsd_tls_diagnostics *diagnostics,
    struct rsd_tls_client **result);
long rsd_tls_client_read(struct rsd_tls_client *client, void *buffer,
    size_t length, uint64_t deadline_ns);
long rsd_tls_client_write(struct rsd_tls_client *client,
    const void *buffer, size_t length, uint64_t deadline_ns);
enum rsd_tls_status rsd_tls_client_flush(
    struct rsd_tls_client *client, uint64_t deadline_ns);
long rsd_tls_client_cancel(struct rsd_tls_client *client);
enum rsd_tls_status rsd_tls_client_close(
    struct rsd_tls_client *client, uint64_t deadline_ns);
enum rsd_tls_status rsd_tls_client_status(
    const struct rsd_tls_client *client);
int rsd_tls_client_bearssl_error(const struct rsd_tls_client *client);
long rsd_tls_client_transport_error(const struct rsd_tls_client *client);
const char *rsd_tls_status_string(enum rsd_tls_status status);
enum rsd_https_status rsd_https_get(
    const struct rsd_https_request *request,
    struct rsd_https_response *response);
enum rsd_https_status rsd_https_get_stream(
    const struct rsd_https_stream_request *request,
    struct rsd_https_response *response);
const char *rsd_https_status_string(enum rsd_https_status status);

#endif
