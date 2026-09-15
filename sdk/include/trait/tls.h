/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_USER_TLS_H
#define TRAIT_USER_TLS_H

/* BearSSL 0.6 deliberately leaves BR_DOXYGEN_IGNORE undefined while using it
 * in two #if expressions.  Isolate that exact upstream -Wundef diagnostic so
 * Trait OS applications can include this public header under -Werror without
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

struct trait_tls_client;

#define TRAIT_HTTPS_MAX_HEADER_BYTES 4096U
#define TRAIT_HTTPS_MAX_PATH_BYTES 1024U

enum trait_tls_status {
    TRAIT_TLS_OK = 0,
    TRAIT_TLS_ARGUMENT,
    TRAIT_TLS_NO_MEMORY,
    TRAIT_TLS_TRUST,
    TRAIT_TLS_CLOCK,
    TRAIT_TLS_ENTROPY,
    TRAIT_TLS_DNS,
    TRAIT_TLS_TRANSPORT,
    TRAIT_TLS_HANDSHAKE,
    TRAIT_TLS_IO,
    TRAIT_TLS_CLOSE
};

struct trait_tls_client_config {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
};

/* Diagnostics are output-only.  They make a failed open auditable even though
 * no client object is returned to the caller. */
struct trait_tls_diagnostics {
    int bearssl_error;
    long transport_error;
};

enum trait_https_status {
    TRAIT_HTTPS_OK = 0,
    TRAIT_HTTPS_ARGUMENT,
    TRAIT_HTTPS_NO_MEMORY,
    TRAIT_HTTPS_TRUST,
    TRAIT_HTTPS_CLOCK,
    TRAIT_HTTPS_ENTROPY,
    TRAIT_HTTPS_DNS,
    TRAIT_HTTPS_TRANSPORT,
    TRAIT_HTTPS_TIMEOUT,
    TRAIT_HTTPS_CANCELED,
    TRAIT_HTTPS_RESET,
    TRAIT_HTTPS_TRUNCATED,
    TRAIT_HTTPS_HOSTNAME,
    TRAIT_HTTPS_CERTIFICATE_TIME,
    TRAIT_HTTPS_AUTHENTICATION,
    TRAIT_HTTPS_HANDSHAKE,
    TRAIT_HTTPS_IO,
    TRAIT_HTTPS_HTTP_VERSION,
    TRAIT_HTTPS_HTTP_STATUS,
    TRAIT_HTTPS_HTTP_HEADERS,
    TRAIT_HTTPS_CONTENT_LENGTH_REQUIRED,
    TRAIT_HTTPS_CONTENT_TOO_LARGE,
    TRAIT_HTTPS_BODY_TRUNCATED,
    TRAIT_HTTPS_BODY_EXTRA,
    TRAIT_HTTPS_CLOSE,
    TRAIT_HTTPS_BODY_WRITE
};

typedef long (*trait_https_body_write_function)(
    void *context,
    const void *bytes,
    size_t byte_count
);

struct trait_https_request {
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

struct trait_https_response {
    uint16_t status_code;
    uint16_t reserved;
    size_t content_length;
    size_t body_length;
    int bearssl_error;
    long transport_error;
};

struct trait_https_stream_request {
    const char *hostname;
    uint16_t port;
    uint16_t reserved;
    const char *path;
    const br_x509_trust_anchor *trust_anchors;
    size_t trust_anchor_count;
    uint64_t deadline_ns;
    size_t body_limit;
    trait_https_body_write_function write_body;
    void *write_context;
};

enum trait_tls_status trait_tls_client_open(
    const struct trait_tls_client_config *config,
    struct trait_tls_client **result);
enum trait_tls_status trait_tls_client_open_diagnostic(
    const struct trait_tls_client_config *config,
    struct trait_tls_diagnostics *diagnostics,
    struct trait_tls_client **result);
long trait_tls_client_read(struct trait_tls_client *client, void *buffer,
    size_t length, uint64_t deadline_ns);
long trait_tls_client_write(struct trait_tls_client *client,
    const void *buffer, size_t length, uint64_t deadline_ns);
enum trait_tls_status trait_tls_client_flush(
    struct trait_tls_client *client, uint64_t deadline_ns);
long trait_tls_client_cancel(struct trait_tls_client *client);
enum trait_tls_status trait_tls_client_close(
    struct trait_tls_client *client, uint64_t deadline_ns);
enum trait_tls_status trait_tls_client_status(
    const struct trait_tls_client *client);
int trait_tls_client_bearssl_error(const struct trait_tls_client *client);
long trait_tls_client_transport_error(const struct trait_tls_client *client);
const char *trait_tls_status_string(enum trait_tls_status status);
enum trait_https_status trait_https_get(
    const struct trait_https_request *request,
    struct trait_https_response *response);
enum trait_https_status trait_https_get_stream(
    const struct trait_https_stream_request *request,
    struct trait_https_response *response);
const char *trait_https_status_string(enum trait_https_status status);

#endif
