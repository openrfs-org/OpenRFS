/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENGAT_USER_TLS_H
#define OPENGAT_USER_TLS_H

/* BearSSL 0.6 deliberately leaves BR_DOXYGEN_IGNORE undefined while using it
 * in two #if expressions.  Isolate that exact upstream -Wundef diagnostic so
 * OpenGAT applications can include this public header under -Werror without
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

struct opengat_tls_client;

#define OPENGAT_HTTPS_MAX_HEADER_BYTES 4096U
#define OPENGAT_HTTPS_MAX_PATH_BYTES 1024U

enum opengat_tls_status {
    OPENGAT_TLS_OK = 0,
    OPENGAT_TLS_ARGUMENT,
    OPENGAT_TLS_NO_MEMORY,
    OPENGAT_TLS_TRUST,
    OPENGAT_TLS_CLOCK,
    OPENGAT_TLS_ENTROPY,
    OPENGAT_TLS_DNS,
    OPENGAT_TLS_TRANSPORT,
    OPENGAT_TLS_HANDSHAKE,
    OPENGAT_TLS_IO,
    OPENGAT_TLS_CLOSE
};

struct opengat_tls_client_config {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
};

/* Diagnostics are output-only.  They make a failed open auditable even though
 * no client object is returned to the caller. */
struct opengat_tls_diagnostics {
    int bearssl_error;
    long transport_error;
};

enum opengat_https_status {
    OPENGAT_HTTPS_OK = 0,
    OPENGAT_HTTPS_ARGUMENT,
    OPENGAT_HTTPS_NO_MEMORY,
    OPENGAT_HTTPS_TRUST,
    OPENGAT_HTTPS_CLOCK,
    OPENGAT_HTTPS_ENTROPY,
    OPENGAT_HTTPS_DNS,
    OPENGAT_HTTPS_TRANSPORT,
    OPENGAT_HTTPS_TIMEOUT,
    OPENGAT_HTTPS_CANCELED,
    OPENGAT_HTTPS_RESET,
    OPENGAT_HTTPS_TRUNCATED,
    OPENGAT_HTTPS_HOSTNAME,
    OPENGAT_HTTPS_CERTIFICATE_TIME,
    OPENGAT_HTTPS_AUTHENTICATION,
    OPENGAT_HTTPS_HANDSHAKE,
    OPENGAT_HTTPS_IO,
    OPENGAT_HTTPS_HTTP_VERSION,
    OPENGAT_HTTPS_HTTP_STATUS,
    OPENGAT_HTTPS_HTTP_HEADERS,
    OPENGAT_HTTPS_CONTENT_LENGTH_REQUIRED,
    OPENGAT_HTTPS_CONTENT_TOO_LARGE,
    OPENGAT_HTTPS_BODY_TRUNCATED,
    OPENGAT_HTTPS_BODY_EXTRA,
    OPENGAT_HTTPS_CLOSE,
    OPENGAT_HTTPS_BODY_WRITE
};

typedef long (*opengat_https_body_write_function)(
    void *context,
    const void *bytes,
    size_t byte_count
);

struct opengat_https_request {
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

struct opengat_https_response {
    uint16_t status_code;
    uint16_t reserved;
    size_t content_length;
    size_t body_length;
    int bearssl_error;
    long transport_error;
};

struct opengat_https_stream_request {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const char *path;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
    size_t body_limit;
    opengat_https_body_write_function write_body;
    void *write_context;
};

enum opengat_tls_status opengat_tls_client_open(
    const struct opengat_tls_client_config *config,
    struct opengat_tls_client **result);
enum opengat_tls_status opengat_tls_client_open_diagnostic(
    const struct opengat_tls_client_config *config,
    struct opengat_tls_diagnostics *diagnostics,
    struct opengat_tls_client **result);
long opengat_tls_client_read(struct opengat_tls_client *client, void *buffer,
    size_t length, uint64_t deadline_ns);
long opengat_tls_client_write(struct opengat_tls_client *client,
    const void *buffer, size_t length, uint64_t deadline_ns);
enum opengat_tls_status opengat_tls_client_flush(
    struct opengat_tls_client *client, uint64_t deadline_ns);
long opengat_tls_client_cancel(struct opengat_tls_client *client);
enum opengat_tls_status opengat_tls_client_close(
    struct opengat_tls_client *client, uint64_t deadline_ns);
enum opengat_tls_status opengat_tls_client_status(
    const struct opengat_tls_client *client);
int opengat_tls_client_bearssl_error(const struct opengat_tls_client *client);
long opengat_tls_client_transport_error(const struct opengat_tls_client *client);
const char *opengat_tls_status_string(enum opengat_tls_status status);
enum opengat_https_status opengat_https_get(
    const struct opengat_https_request *request,
    struct opengat_https_response *response);
enum opengat_https_status opengat_https_get_stream(
    const struct opengat_https_stream_request *request,
    struct opengat_https_response *response);
const char *opengat_https_status_string(enum opengat_https_status status);

#endif
