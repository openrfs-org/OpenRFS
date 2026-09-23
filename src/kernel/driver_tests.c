/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * In-guest plans for the upstream driver suite.
 *
 * Each plan proves one class of driver against the device QEMU models,
 * through the same kernel paths ordinary use takes: a network driver must
 * carry DHCP, ICMP and a verified HTTP transfer for the IPv4 stack; later
 * plans prove block, USB, display and audio drivers the same way. Every line
 * a plan prints starts with "ST DRV" so the runner can require it exactly.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/clock.h>
#include <openrfs/console.h>
#include <openrfs/driver_tests.h>
#include <openrfs/hwdrv.h>
#include <openrfs/netdev.h>
#include <openrfs/network.h>

#define DRIVER_TEST_MAX_TOKEN 64U
#define DRIVER_TEST_DOWNLOAD_BYTES (256U * 1024U)
#define DRIVER_TEST_DHCP_TIMEOUT_NS UINT64_C(15000000000)
#define DRIVER_TEST_PING_TIMEOUT_NS UINT64_C(3000000000)
/* The stack bounds every synchronous operation at 30 seconds. */
#define DRIVER_TEST_HTTP_TIMEOUT_NS UINT64_C(30000000000)
#define DRIVER_TEST_PING_COUNT 3U
#define DRIVER_TEST_CARRIER_TIMEOUT_NS UINT64_C(10000000000)
/* QEMU user networking: the host, reachable as the gateway. */
#define DRIVER_TEST_SLIRP_HOST UINT32_C(0x0A000202)

struct driver_test_options {
    char plan[DRIVER_TEST_MAX_TOKEN];
    char driver[DRIVER_TEST_MAX_TOKEN];
    uint32_t port;
    uint32_t bytes;
};

static uint8_t download[DRIVER_TEST_DOWNLOAD_BYTES];

static bool token_value(const char *command_line, size_t length,
    const char *prefix, char *value, size_t capacity)
{
    size_t prefix_length = 0U;
    size_t offset = 0U;

    while (prefix[prefix_length] != '\0') {
        ++prefix_length;
    }
    while (command_line != NULL && offset < length) {
        size_t start;
        size_t end;
        bool matches = true;

        while (offset < length && command_line[offset] == ' ') {
            ++offset;
        }
        start = offset;
        while (offset < length && command_line[offset] != ' ' &&
            command_line[offset] != '\0') {
            ++offset;
        }
        end = offset;
        if (end - start <= prefix_length) {
            continue;
        }
        for (size_t index = 0U; index < prefix_length; ++index) {
            if (command_line[start + index] != prefix[index]) {
                matches = false;
                break;
            }
        }
        if (!matches || end - start - prefix_length >= capacity) {
            continue;
        }
        for (size_t index = 0U; index < end - start - prefix_length; ++index) {
            value[index] = command_line[start + prefix_length + index];
        }
        value[end - start - prefix_length] = '\0';
        return true;
    }
    return false;
}

static bool parse_decimal(const char *text, uint32_t *value)
{
    uint64_t result = 0U;

    if (text[0] == '\0') {
        return false;
    }
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        result = result * 10U + (uint64_t)(text[index] - '0');
        if (result > UINT32_MAX) {
            return false;
        }
    }
    *value = (uint32_t)result;
    return true;
}

static bool text_equal(const char *left, const char *right)
{
    size_t index = 0U;

    while (left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}

static void write_ipv4(uint32_t address)
{
    char text[16];

    network_format_ipv4(address, text);
    console_write(text);
}

static void write_mac(const uint8_t mac[6])
{
    static const char digits[] = "0123456789abcdef";

    for (size_t index = 0U; index < 6U; ++index) {
        console_putc(digits[mac[index] >> 4U]);
        console_putc(digits[mac[index] & 0x0FU]);
        if (index + 1U < 6U) {
            console_putc(':');
        }
    }
}

/* The payload the runner serves; both sides compute it the same way. */
static uint8_t payload_byte(uint32_t index)
{
    return (uint8_t)((index * 131U) ^ (index >> 7U) ^ 0x5AU);
}

static bool network_plan(const struct driver_test_options *options,
    const char **reason)
{
    struct network_state state = network_get_state();
    struct network_ping_result ping;
    struct network_http_result http;
    char url[64];
    size_t used = 0U;
    enum network_status status;
    uint32_t mismatch = UINT32_MAX;

    if (!state.active || netdev_active_kind() != NETDEV_KIND_REGISTERED) {
        *reason = "no upstream network interface is active";
        return false;
    }
    if (options->driver[0] != '\0' &&
        !text_equal(options->driver, netdev_active_driver())) {
        *reason = "the active interface is bound by a different driver";
        return false;
    }
    /*
     * Autonegotiation takes real time on real parts and on some models; wait
     * for carrier the way an operator would before asking for a lease.
     */
    if (!state.device.link_up) {
        console_write("ST DRV carrier wait\n");
    }
    for (uint64_t deadline = clock_monotonic_ns() +
             DRIVER_TEST_CARRIER_TIMEOUT_NS;
         !state.device.link_up && clock_monotonic_ns() < deadline;) {
        (void)network_service();
        state = network_get_state();
    }
    console_write("ST DRV ");
    console_write(netdev_active_name());
    console_write(" driver ");
    console_write(netdev_active_driver());
    console_write(" mac ");
    write_mac(state.device.mac);
    console_write(state.device.link_up ? " link up\n" : " link down\n");

    status = network_start_dhcp(DRIVER_TEST_DHCP_TIMEOUT_NS);
    state = network_get_state();
    if (status != NETWORK_STATUS_OK || !state.configuration.configured) {
        console_write("ST DRV dhcp failed status ");
        console_write(network_status_string(status));
        console_write(" polls ");
        console_write_u64(state.device.statistics.polling_passes);
        console_write(" rx ");
        console_write_u64(state.device.statistics.rx_frames);
        console_write(" tx ");
        console_write_u64(state.device.statistics.tx_frames);
        console_write(" dropped ");
        console_write_u64(state.device.statistics.dropped_frames);
        console_putc('\n');
        *reason = "DHCP through the upstream driver failed";
        return false;
    }
    console_write("ST DRV dhcp address ");
    write_ipv4(state.configuration.address);
    console_write(" gateway ");
    write_ipv4(state.configuration.gateway);
    console_putc('\n');

    status = network_ping(state.configuration.gateway != 0U ?
        state.configuration.gateway : DRIVER_TEST_SLIRP_HOST,
        DRIVER_TEST_PING_COUNT, DRIVER_TEST_PING_TIMEOUT_NS, &ping);
    if (status != NETWORK_STATUS_OK || ping.received == 0U) {
        *reason = "ICMP echo through the upstream driver failed";
        return false;
    }
    console_write("ST DRV ping sent ");
    console_write_u64(ping.sent);
    console_write(" received ");
    console_write_u64(ping.received);
    console_putc('\n');

    if (options->port != 0U && options->bytes != 0U) {
        const char prefix[] = "http://10.0.2.2:";
        char digits[6];
        size_t digit_count = 0U;
        uint32_t port = options->port;

        if (options->bytes > DRIVER_TEST_DOWNLOAD_BYTES || port > 65535U) {
            *reason = "HTTP transfer parameters are out of range";
            return false;
        }
        for (size_t index = 0U; prefix[index] != '\0'; ++index) {
            url[used++] = prefix[index];
        }
        do {
            digits[digit_count++] = (char)('0' + port % 10U);
            port /= 10U;
        } while (port != 0U && digit_count < sizeof(digits));
        while (digit_count > 0U) {
            url[used++] = digits[--digit_count];
        }
        for (const char *path = "/payload.bin"; *path != '\0'; ++path) {
            url[used++] = *path;
        }
        url[used] = '\0';
        status = network_http_memory(NETWORK_OWNER_SHELL, url, false,
            DRIVER_TEST_HTTP_TIMEOUT_NS, download, sizeof(download), &http);
        if (status != NETWORK_STATUS_OK || http.status_code != 200U ||
            http.body_bytes != options->bytes) {
            console_write("ST DRV http failed url ");
            console_write(url);
            console_write(" status ");
            console_write(network_status_string(status));
            console_write(" code ");
            console_write_u64(http.status_code);
            console_write(" bytes ");
            console_write_u64(http.body_bytes);
            console_putc('\n');
            *reason = "HTTP transfer through the upstream driver failed";
            return false;
        }
        for (uint32_t index = 0U; index < options->bytes; ++index) {
            if (download[index] != payload_byte(index)) {
                mismatch = index;
                break;
            }
        }
        if (mismatch != UINT32_MAX) {
            *reason = "HTTP payload received through the driver is corrupt";
            return false;
        }
        console_write("ST DRV http bytes ");
        console_write_u64(http.body_bytes);
        console_write(" verified\n");
    }
    state = network_get_state();
    console_write("ST DRV counters rx ");
    console_write_u64(state.device.statistics.rx_frames);
    console_write(" tx ");
    console_write_u64(state.device.statistics.tx_frames);
    console_write(" dropped ");
    console_write_u64(state.device.statistics.dropped_frames);
    console_putc('\n');
    return true;
}

bool driver_tests_run(const char *command_line, size_t length,
    const char **reason)
{
    struct driver_test_options options;
    char number[DRIVER_TEST_MAX_TOKEN];
    struct hwdrv_state framework = hwdrv_get_state();

    *reason = NULL;
    for (size_t index = 0U; index < sizeof(options); ++index) {
        ((uint8_t *)&options)[index] = 0U;
    }
    if (!token_value(command_line, length, "openrfs.drvtest=", options.plan,
            sizeof(options.plan))) {
        *reason = "no openrfs.drvtest plan was given";
        return false;
    }
    (void)token_value(command_line, length, "openrfs.drvdriver=",
        options.driver, sizeof(options.driver));
    if (token_value(command_line, length, "openrfs.drvport=", number,
            sizeof(number)) && !parse_decimal(number, &options.port)) {
        *reason = "openrfs.drvport is not a number";
        return false;
    }
    if (token_value(command_line, length, "openrfs.drvbytes=", number,
            sizeof(number)) && !parse_decimal(number, &options.bytes)) {
        *reason = "openrfs.drvbytes is not a number";
        return false;
    }
    console_write("ST DRV framework bindings ");
    console_write_u64(framework.active_bindings);
    console_write(" claimed ");
    console_write_u64(framework.claimed_devices);
    console_write(" bus-masters ");
    console_write_u64(framework.bus_masters);
    console_write(" probe-failures ");
    console_write_u64(framework.probe_failures);
    console_putc('\n');
    for (size_t index = 0U; index < hwdrv_binding_count(); ++index) {
        const struct hwdrv_binding *binding = hwdrv_binding_at(index);

        console_write("ST DRV bound ");
        console_write(binding->instance);
        console_write(" ");
        console_write(binding->driver);
        console_write(" ");
        console_write(binding->origin->project);
        console_write(" ");
        console_write(binding->origin->path);
        console_write(" [");
        console_write(binding->origin->license);
        console_write("] ");
        console_write(binding->description);
        console_putc('\n');
    }
    if (text_equal(options.plan, "net")) {
        return network_plan(&options, reason);
    }
    *reason = "unknown openrfs.drvtest plan";
    return false;
}
