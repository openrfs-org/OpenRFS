/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/network.h>
#include <openrfs/runtime.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_ADDRESS UINT32_C(0x0A000214)
#define OPERATION_NS UINT64_C(5000000000)

static const char expected_body[] = "hello from the OpenRFS network\n";

static uint64_t deadline(void)
{
    return openrfs_monotonic_ns() + OPERATION_NS;
}

static int close_handle(openrfs_handle_t handle)
{
    return openrfs_handle_close(handle) < 0 ? -1 : 0;
}

static int verify_http(const char *response, size_t length,
    const char **body, size_t *body_length)
{
    static const char status[] = "HTTP/1.1 200 OK\r\n";
    static const char content_length[] = "Content-Length: ";
    const char *header_end;
    const char *field;
    char *number_end;
    unsigned long declared;

    if (length < sizeof(status) - 1U ||
        memcmp(response, status, sizeof(status) - 1U) != 0) {
        return -1;
    }
    header_end = strstr(response, "\r\n\r\n");
    field = strstr(response, content_length);
    if (header_end == NULL || field == NULL || field >= header_end) {
        return -1;
    }
    field += sizeof(content_length) - 1U;
    declared = strtoul(field, &number_end, 10);
    if (number_end == field || number_end + 2 > header_end ||
        number_end[0] != '\r' || number_end[1] != '\n') {
        return -1;
    }
    *body = header_end + 4;
    *body_length = length - (size_t)(*body - response);
    return declared == *body_length ? 0 : -1;
}

static int exercise_udp(uint32_t address)
{
    static const char message[] = "native udp echo";
    struct openrfs_ipv4_endpoint destination = {address, 4242U, 0U};
    struct openrfs_ipv4_endpoint source = {0U, 0U, 0U};
    struct openrfs_ipv4_endpoint local = {0U, 0U, 0U};
    char response[32];
    const long opened = openrfs_datagram_open();
    long count;

    if (opened < 0) {
        return -10;
    }
    const openrfs_handle_t datagram = (openrfs_handle_t)opened;
    if (openrfs_datagram_bind(datagram, 50010U) < 0 ||
        openrfs_network_address(datagram, 0, &local) < 0 ||
        local.port != 50010U || local.address == 0U ||
        openrfs_datagram_send(datagram, &destination, message,
            sizeof(message) - 1U, deadline()) != (long)(sizeof(message) - 1U)) {
        (void)close_handle(datagram);
        return -11;
    }
    count = openrfs_datagram_receive(datagram, &source, response,
        sizeof(response), deadline());
    if (count != (long)(sizeof(message) - 1U) ||
        source.address != address || source.port != 4242U ||
        memcmp(response, message, sizeof(message) - 1U) != 0 ||
        close_handle(datagram) != 0) {
        return -12;
    }
    return 0;
}

static int exercise_failures(uint32_t address)
{
    struct openrfs_ipv4_endpoint endpoint = {address, 81U, 0U};
    long opened = openrfs_stream_open();
    openrfs_handle_t stream;

    if (opened < 0) {
        return -20;
    }
    stream = (openrfs_handle_t)opened;
    if (openrfs_stream_connect(stream, &endpoint, deadline()) != -OPENRFS_EIO ||
        close_handle(stream) != 0) {
        return -21;
    }

    opened = openrfs_stream_open();
    if (opened < 0) {
        return -22;
    }
    stream = (openrfs_handle_t)opened;
    endpoint.port = 82U;
    if (openrfs_stream_connect(stream, &endpoint,
            openrfs_monotonic_ns() + UINT64_C(150000000)) !=
            -OPENRFS_ETIMEDOUT || close_handle(stream) != 0) {
        return -23;
    }

    opened = openrfs_stream_open();
    if (opened < 0) {
        return -24;
    }
    stream = (openrfs_handle_t)opened;
    if (openrfs_network_cancel(stream) < 0 ||
        openrfs_stream_connect(stream, &endpoint, deadline()) !=
            -OPENRFS_ECANCELED || close_handle(stream) != 0) {
        return -25;
    }
    const long malformed = openrfs_dns_resolve("malformed.test", deadline());
    if (malformed != -OPENRFS_EIO) {
        printf("OPENRFS NETAPP MALFORMED DNS result=%ld expected=%d\n",
            malformed, -OPENRFS_EIO);
        return -26;
    }
    return 0;
}

static int leave_handles_for_process_teardown(void)
{
    const long stream = openrfs_stream_open();
    const long datagram = openrfs_datagram_open();

    if (stream < 0 || datagram < 0 ||
        openrfs_datagram_bind((openrfs_handle_t)datagram, 50011U) < 0) {
        if (stream >= 0) (void)close_handle((openrfs_handle_t)stream);
        if (datagram >= 0) (void)close_handle((openrfs_handle_t)datagram);
        return -1;
    }
    /* The kernel completion proof requires both objects to die with process. */
    return 0;
}

int main(int argc, char **argv, char **environment)
{
    static const char request[] =
        "GET /welcome.txt HTTP/1.1\r\n"
        "Host: openrfs.test\r\n"
        "Connection: close\r\n\r\n";
    struct openrfs_ipv4_endpoint endpoint;
    struct openrfs_ipv4_endpoint peer;
    struct openrfs_ipv4_endpoint local;
    char response[768];
    const char *body;
    size_t body_length;
    size_t received = 0U;
    long resolved;
    long opened;
    openrfs_handle_t stream;
    FILE *output;

    (void)argc;
    (void)argv;
    (void)environment;
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    puts("OPENRFS NETAPP PHASE start");
    resolved = openrfs_dns_resolve("openrfs.test", deadline());
    if (resolved != (long)HTTP_ADDRESS) {
        return 30;
    }
    puts("OPENRFS NETAPP PHASE dns PASS");
    endpoint = (struct openrfs_ipv4_endpoint){(uint32_t)resolved, 80U, 0U};
    opened = openrfs_stream_open();
    if (opened < 0) {
        return 31;
    }
    stream = (openrfs_handle_t)opened;
    if (openrfs_stream_connect(stream, &endpoint, deadline()) < 0 ||
        openrfs_network_address(stream, 1, &peer) < 0 ||
        openrfs_network_address(stream, 0, &local) < 0 ||
        peer.address != HTTP_ADDRESS || peer.port != 80U ||
        local.address == 0U || local.port == 0U ||
        openrfs_stream_write(stream, request, sizeof(request) - 1U,
            deadline()) != (long)(sizeof(request) - 1U)) {
        (void)close_handle(stream);
        return 32;
    }
    puts("OPENRFS NETAPP PHASE tcp-connect-write PASS");
    while (received < sizeof(response) - 1U) {
        const long count = openrfs_stream_read(stream, response + received,
            sizeof(response) - 1U - received, deadline());

        if (count > 0) {
            received += (size_t)count;
        } else if (count == -OPENRFS_EPIPE) {
            break;
        } else {
            (void)close_handle(stream);
            return 33;
        }
    }
    response[received] = '\0';
    if (verify_http(response, received, &body, &body_length) != 0 ||
        body_length != sizeof(expected_body) - 1U ||
        memcmp(body, expected_body, sizeof(expected_body) - 1U) != 0 ||
        openrfs_stream_shutdown(stream, OPENRFS_SHUTDOWN_WRITE, deadline()) < 0 ||
        close_handle(stream) != 0) {
        return 34;
    }
    puts("OPENRFS NETAPP PHASE http-framing-shutdown PASS");
    output = fopen("HTTP.TXT", "w");
    if (output == NULL || fwrite(body, 1U, body_length, output) != body_length ||
        fflush(output) != 0 || fclose(output) != 0 ||
        openrfs_syscall1(OPENRFS_SYS_VOLUME_SYNC, OPENRFS_VOLUME_DATA) < 0) {
        return 35;
    }
    puts("OPENRFS NETAPP PHASE data-sync PASS");
    {
        const int udp = exercise_udp(HTTP_ADDRESS);
        const int failures = udp == 0 ? exercise_failures(HTTP_ADDRESS) : 0;
        const int teardown = udp == 0 && failures == 0 ?
            leave_handles_for_process_teardown() : 0;

        if (udp != 0 || failures != 0 || teardown != 0) {
            printf("OPENRFS NETAPP FAILURE udp=%d failures=%d teardown=%d\n",
                udp, failures, teardown);
            return 36;
        }
    }
    puts("OPENRFS NETAPP PHASE udp-failures-teardown PASS");
    printf("OPENRFS NETAPP PASS dns=10.0.2.20 http=%u udp=echo timeout reset cancel malformed-dns\n",
        (unsigned int)body_length);
    return 0;
}
