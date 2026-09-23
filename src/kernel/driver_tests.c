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

#include <openrfs/blockdev.h>
#include <openrfs/clock.h>
#include <openrfs/console.h>
#include <openrfs/display.h>
#include <openrfs/driver_tests.h>
#include <openrfs/hwdrv.h>
#include <openrfs/keyboard.h>
#include <openrfs/netdev.h>
#include <openrfs/network.h>
#include <openrfs/pointer.h>
#include <openrfs/screen.h>

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

/* The fixture every storage scenario's medium carries, in 512-byte units. */
#define DRIVER_TEST_UNIT_BYTES 512U
#define DRIVER_TEST_READ_RUN_BYTES (160U * 1024U)
#define DRIVER_TEST_WRITE_UNITS 48U

/* How long the HID plan waits for the runner's injected input. */
#define DRIVER_TEST_HID_TIMEOUT_NS UINT64_C(30000000000)

/* How long the display plan waits for the runner to capture the screen. */
#define DRIVER_TEST_DISPLAY_TIMEOUT_NS UINT64_C(60000000000)
/* A geometry no adapter lists, for the refusal control. */
#define DRIVER_TEST_DISPLAY_BOGUS_WIDTH 1234U
#define DRIVER_TEST_DISPLAY_BOGUS_HEIGHT 567U

struct driver_test_options {
    char plan[DRIVER_TEST_MAX_TOKEN];
    char driver[DRIVER_TEST_MAX_TOKEN];
    char kind[DRIVER_TEST_MAX_TOKEN];
    char text[DRIVER_TEST_MAX_TOKEN];
    char mode[DRIVER_TEST_MAX_TOKEN];
    uint32_t port;
    uint32_t bytes;
    uint32_t units;
    uint32_t write;
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

/*
 * The storage fixture. Unit u (512 bytes at byte offset u * 512) holds an
 * eight-byte signature, u as a little-endian 64-bit number, then byte k of
 * the unit equal to (u + k) mod 256. tools/run_driver_tests.py writes the
 * same image, and after a write test checks the rewritten units host-side.
 */
static uint8_t fixture_byte(uint64_t unit, uint32_t offset, bool written)
{
    static const char original[8] = { 'O', 'R', 'F', 'S', 'B', 'L', 'K', '1' };
    static const char rewritten[8] = { 'O', 'R', 'F', 'S', 'W', 'R', 'T', '1' };

    if (offset < 8U) {
        return (uint8_t)(written ? rewritten[offset] : original[offset]);
    }
    if (offset < 16U) {
        return (uint8_t)(unit >> ((offset - 8U) * 8U));
    }
    if (written) {
        return (uint8_t)(unit * 3U + offset * 5U + 0x11U);
    }
    return (uint8_t)(unit + offset);
}

static void fill_units(uint8_t *buffer, uint64_t first_unit, uint32_t units,
    bool written)
{
    for (uint32_t unit = 0U; unit < units; ++unit) {
        for (uint32_t offset = 0U; offset < DRIVER_TEST_UNIT_BYTES; ++offset) {
            buffer[unit * DRIVER_TEST_UNIT_BYTES + offset] =
                fixture_byte(first_unit + unit, offset, written);
        }
    }
}

/* The first unit whose bytes differ from the fixture, or UINT64_MAX. */
static uint64_t check_units(const uint8_t *buffer, uint64_t first_unit,
    uint32_t units, bool written)
{
    for (uint32_t unit = 0U; unit < units; ++unit) {
        for (uint32_t offset = 0U; offset < DRIVER_TEST_UNIT_BYTES; ++offset) {
            if (buffer[unit * DRIVER_TEST_UNIT_BYTES + offset] !=
                fixture_byte(first_unit + unit, offset, written)) {
                return first_unit + unit;
            }
        }
    }
    return UINT64_MAX;
}

static bool kind_matches(const char *kind, enum blockdev_kind actual)
{
    return (text_equal(kind, "disk") && actual == BLOCKDEV_KIND_DISK) ||
        (text_equal(kind, "cd") && actual == BLOCKDEV_KIND_OPTICAL) ||
        (text_equal(kind, "fd") && actual == BLOCKDEV_KIND_FLOPPY) ||
        (text_equal(kind, "sd") && actual == BLOCKDEV_KIND_FLASH);
}

/* Read blocks [lba, lba + count) and compare them with the fixture. */
static bool read_and_check(size_t index, const struct blockdev_info *info,
    uint64_t lba, uint32_t count, bool written, const char **reason)
{
    const uint32_t units_per_block =
        info->geometry.block_size / DRIVER_TEST_UNIT_BYTES;
    enum blockdev_status status = blockdev_read(index, lba, count, download,
        sizeof(download));
    uint64_t mismatch;

    if (status != BLOCKDEV_STATUS_OK) {
        console_write("ST DRV blk read lba ");
        console_write_u64(lba);
        console_write(" failed: ");
        console_write(blockdev_status_string(status));
        console_putc('\n');
        *reason = "a block read through the upstream driver failed";
        return false;
    }
    mismatch = check_units(download, lba * units_per_block,
        count * units_per_block, written);
    if (mismatch != UINT64_MAX) {
        console_write("ST DRV blk read lba ");
        console_write_u64(lba);
        console_write(" mismatch at unit ");
        console_write_u64(mismatch);
        console_putc('\n');
        *reason = "data read through the upstream driver is corrupt";
        return false;
    }
    console_write("ST DRV blk read lba ");
    console_write_u64(lba);
    console_write(" count ");
    console_write_u64(count);
    console_write(written ? " verified rewritten\n" : " verified\n");
    return true;
}

static bool storage_plan(const struct driver_test_options *options,
    const char **reason)
{
    struct blockdev_info info;
    size_t index = SIZE_MAX;
    uint64_t blocks;
    uint32_t run;
    enum blockdev_status status;

    for (size_t candidate = 0U; candidate < blockdev_count(); ++candidate) {
        struct blockdev_info probe;

        if (!blockdev_info(candidate, &probe)) {
            continue;
        }
        console_write("ST DRV blk candidate ");
        console_write(probe.name);
        console_write(" driver ");
        console_write(probe.driver);
        console_write(" blocks ");
        console_write_u64(probe.geometry.block_count);
        console_write(" x ");
        console_write_u64(probe.geometry.block_size);
        console_putc('\n');
        if (index == SIZE_MAX &&
            (options->driver[0] == '\0' ||
                text_equal(options->driver, probe.driver)) &&
            kind_matches(options->kind, probe.geometry.kind) &&
            probe.geometry.block_size % DRIVER_TEST_UNIT_BYTES == 0U &&
            probe.geometry.block_count * (probe.geometry.block_size /
                DRIVER_TEST_UNIT_BYTES) == options->units) {
            index = candidate;
        }
    }
    if (index == SIZE_MAX || !blockdev_info(index, &info)) {
        *reason = "no block device matches the scenario's medium";
        return false;
    }
    blocks = info.geometry.block_count;
    console_write("ST DRV blk ");
    console_write(info.name);
    console_write(" driver ");
    console_write(info.driver);
    console_write(" kind ");
    console_write(blockdev_kind_string(info.geometry.kind));
    console_write(" block ");
    console_write_u64(info.geometry.block_size);
    console_write(" blocks ");
    console_write_u64(blocks);
    console_write(info.geometry.read_only ? " read-only" : " writable");
    console_write(" desc ");
    console_write(info.description);
    console_putc('\n');

    /* The first blocks, a run long enough to need several transfers, the
     * last block. */
    run = DRIVER_TEST_READ_RUN_BYTES / info.geometry.block_size;
    if (run > blocks / 2U) {
        run = (uint32_t)(blocks / 2U);
    }
    if (!read_and_check(index, &info, 0U, blocks < 8U ? (uint32_t)blocks : 8U,
            false, reason) ||
        (run != 0U && !read_and_check(index, &info, blocks / 2U, run, false,
            reason)) ||
        !read_and_check(index, &info, blocks - 1U, 1U, false, reason)) {
        return false;
    }
    status = blockdev_read(index, blocks, 1U, download, sizeof(download));
    if (status != BLOCKDEV_STATUS_RANGE) {
        *reason = "a read past the end of the medium was not refused";
        return false;
    }
    console_write("ST DRV blk range refused\n");

    if (info.geometry.read_only) {
        status = blockdev_write(index, 0U, 1U, download, sizeof(download));
        if (status != BLOCKDEV_STATUS_READ_ONLY) {
            *reason = "a write to read-only media was not refused";
            return false;
        }
        console_write("ST DRV blk write refused read-only\n");
    } else if (options->write != 0U) {
        const uint32_t units_per_block =
            info.geometry.block_size / DRIVER_TEST_UNIT_BYTES;
        const uint32_t count = DRIVER_TEST_WRITE_UNITS / units_per_block;
        const uint64_t lba = blocks - 2U * count;

        fill_units(download, lba * units_per_block, count * units_per_block,
            true);
        status = blockdev_write(index, lba, count, download,
            sizeof(download));
        if (status != BLOCKDEV_STATUS_OK) {
            console_write("ST DRV blk write failed: ");
            console_write(blockdev_status_string(status));
            console_putc('\n');
            *reason = "a block write through the upstream driver failed";
            return false;
        }
        for (size_t byte = 0U; byte < count * info.geometry.block_size;
             ++byte) {
            download[byte] = 0U;
        }
        if (!read_and_check(index, &info, lba, count, true, reason) ||
            !read_and_check(index, &info, lba - 1U, 1U, false, reason) ||
            !read_and_check(index, &info, lba + count, 1U, false, reason)) {
            return false;
        }
        console_write("ST DRV blk write lba ");
        console_write_u64(lba);
        console_write(" count ");
        console_write_u64(count);
        console_write(" verified\n");
    }
    if (!blockdev_info(index, &info)) {
        *reason = "the block device vanished";
        return false;
    }
    console_write("ST DRV blk counters reads ");
    console_write_u64(info.reads);
    console_write(" writes ");
    console_write_u64(info.writes);
    console_write(" blocks-read ");
    console_write_u64(info.blocks_read);
    console_write(" blocks-written ");
    console_write_u64(info.blocks_written);
    console_write(" errors ");
    console_write_u64(info.errors);
    console_putc('\n');
    return info.errors == 0U;
}

/*
 * USB HID: the runner types openrfs.drvtext on the emulated keyboard and
 * moves and clicks the emulated mouse through QEMU's input-send-event, once
 * "ST DRV hid ready" appears. The events must arrive through the USB host
 * controller, the upstream HID driver, and OpenRFS's own keyboard queue and
 * pointer decoder - the same path the shell and desktop read.
 */
static bool hid_plan(const struct driver_test_options *options,
    const char **reason)
{
    const bool want_keyboard = text_equal(options->kind, "kbd") ||
        text_equal(options->kind, "both");
    const bool want_mouse = text_equal(options->kind, "mouse") ||
        text_equal(options->kind, "both");
    char typed[DRIVER_TEST_MAX_TOKEN];
    size_t typed_length = 0U;
    struct pointer_state pointer;
    uint32_t start_x = 0U;
    uint32_t start_y = 0U;
    uint64_t start_transitions = 0U;
    uint64_t start_pointer_packets = 0U;
    uint64_t start_pointer_interrupts = 0U;
    const struct keyboard_state keyboard_before = keyboard_get_state();
    struct keyboard_state keyboard_after;
    uint64_t deadline;
    bool keys_done = !want_keyboard;
    bool mouse_done = !want_mouse;

    if ((want_keyboard && hwdrv_find_binding("kbd0") == NULL) ||
        (want_mouse && hwdrv_find_binding("mouse0") == NULL)) {
        *reason = "the expected USB HID devices are not bound";
        return false;
    }
    if (want_mouse) {
        if (pointer_set_bounds(1024U, 768U) != POINTER_STATUS_OK) {
            *reason = "the pointer layer is not available";
            return false;
        }
        pointer = pointer_get_state();
        start_x = pointer.x;
        start_y = pointer.y;
        start_transitions = pointer.button_transitions;
        start_pointer_packets = pointer.submitted_packets;
        start_pointer_interrupts = pointer.interrupts;
    }
    while (keyboard_read(&(struct keyboard_event){ 0 }) == KEYBOARD_STATUS_OK) {
    }
    console_write("ST DRV hid ready\n");
    deadline = clock_monotonic_ns() + DRIVER_TEST_HID_TIMEOUT_NS;
    while ((!keys_done || !mouse_done) && clock_monotonic_ns() < deadline) {
        struct keyboard_event event;

        while (want_keyboard &&
            keyboard_read(&event) == KEYBOARD_STATUS_OK) {
            if (event.pressed && event.character != '\0' &&
                typed_length + 1U < sizeof(typed)) {
                typed[typed_length++] = event.character;
                typed[typed_length] = '\0';
            }
        }
        if (want_keyboard && typed_length != 0U &&
            text_equal(typed, options->text)) {
            keys_done = true;
        }
        if (want_mouse) {
            pointer = pointer_get_state();
            if (pointer.x != start_x && pointer.y != start_y &&
                pointer.button_transitions >= start_transitions + 2U &&
                !pointer.left) {
                mouse_done = true;
            }
        }
        __asm__ volatile ("pause" : : : "memory");
    }
    typed[typed_length] = '\0';
    keyboard_after = keyboard_get_state();
    if (want_keyboard) {
        /* Which path carried the keys: bytes submitted by the USB HID driver
         * versus IRQ 1 from the i8042, which must not have moved. */
        console_write("ST DRV hid keys ");
        console_write(typed_length != 0U ? typed : "(none)");
        console_write(" usb-bytes ");
        console_write_u64(keyboard_after.submitted - keyboard_before.submitted);
        console_write(" i8042-interrupts ");
        console_write_u64(keyboard_after.interrupts -
            keyboard_before.interrupts);
        console_putc('\n');
    }
    if (want_mouse) {
        pointer = pointer_get_state();
        console_write("ST DRV hid pointer dx ");
        console_write_u64(pointer.x - start_x);
        console_write(" dy ");
        console_write_u64(pointer.y - start_y);
        console_write(" button-transitions ");
        console_write_u64(pointer.button_transitions - start_transitions);
        console_write(" usb-packets ");
        console_write_u64(pointer.submitted_packets - start_pointer_packets);
        console_write(" i8042-interrupts ");
        console_write_u64(pointer.interrupts - start_pointer_interrupts);
        console_putc('\n');
    }
    if (!keys_done) {
        *reason = "typed keys did not arrive through the USB keyboard";
        return false;
    }
    if (!mouse_done) {
        *reason = "mouse input did not arrive through the USB mouse";
        return false;
    }
    return true;
}

/* "800x600x32" */
static bool parse_mode(const char *text, uint32_t values[3])
{
    size_t field = 0U;
    uint32_t current = 0U;
    bool digits = false;

    for (size_t index = 0U; text[index] != '\0'; ++index) {
        const char character = text[index];

        if (character >= '0' && character <= '9') {
            current = current * 10U + (uint32_t)(character - '0');
            if (current > 65535U) {
                return false;
            }
            digits = true;
        } else if (character == 'x' && digits && field < 2U) {
            values[field++] = current;
            current = 0U;
            digits = false;
        } else {
            return false;
        }
    }
    if (!digits || field != 2U) {
        return false;
    }
    values[2] = current;
    return true;
}

/*
 * The quadrant pattern, top left to bottom right: red, green, blue, white.
 * Direct-colour modes use the channel layouts SeaBIOS's vbe.c reports for
 * each depth; the 8-bit modes use the standard VGA palette the drivers load,
 * whose entries 4, 2, 1 and 15 are red, green, blue and white.
 */
static bool pattern_colors(const struct display_mode *mode,
    uint32_t colors[4])
{
    static const uint32_t palette[4] = { 4U, 2U, 1U, 15U };
    static const uint32_t rgb555[4] = { 0x7C00U, 0x03E0U, 0x001FU, 0x7FFFU };
    static const uint32_t rgb565[4] = { 0xF800U, 0x07E0U, 0x001FU, 0xFFFFU };
    static const uint32_t rgb888[4] = {
        0xFF0000U, 0x00FF00U, 0x0000FFU, 0xFFFFFFU
    };
    const uint32_t *source;

    switch (mode->bits_per_pixel) {
    case 8U:
        if (!mode->palette) {
            return false;
        }
        source = palette;
        break;
    case 15U:
        source = rgb555;
        break;
    case 16U:
        source = rgb565;
        break;
    case 24U:
    case 32U:
        source = rgb888;
        break;
    default:
        return false;
    }
    for (size_t index = 0U; index < 4U; ++index) {
        colors[index] = source[index];
    }
    return true;
}

static void put_pixel(volatile uint8_t *at, uint32_t bytes, uint32_t value)
{
    switch (bytes) {
    case 1U:
        at[0] = (uint8_t)value;
        break;
    case 2U:
        *(volatile uint16_t *)at = (uint16_t)value;
        break;
    case 3U:
        at[0] = (uint8_t)value;
        at[1] = (uint8_t)(value >> 8U);
        at[2] = (uint8_t)(value >> 16U);
        break;
    default:
        *(volatile uint32_t *)at = value;
        break;
    }
}

static uint32_t get_pixel(const volatile uint8_t *at, uint32_t bytes)
{
    switch (bytes) {
    case 1U:
        return at[0];
    case 2U:
        return *(const volatile uint16_t *)at;
    case 3U:
        return (uint32_t)at[0] | ((uint32_t)at[1] << 8U) |
            ((uint32_t)at[2] << 16U);
    default:
        return *(const volatile uint32_t *)at;
    }
}

static uint32_t quadrant_color(const struct display_mode *mode,
    const uint32_t colors[4], uint32_t x, uint32_t y)
{
    return colors[(y >= mode->height / 2U ? 2U : 0U) +
        (x >= mode->width / 2U ? 1U : 0U)];
}

static bool display_plan(const struct driver_test_options *options,
    const char **reason)
{
    uint32_t geometry[3];
    uint32_t colors[4];
    uint32_t bytes;
    uint64_t mismatches = 0U;
    uint64_t deadline;
    struct display_info info;
    struct display_mode mode = { 0 };
    enum display_status status;
    size_t index = SIZE_MAX;
    bool acknowledged = false;

    if (!parse_mode(options->mode, geometry)) {
        *reason = "openrfs.drvmode must be WIDTHxHEIGHTxBPP";
        return false;
    }
    for (size_t slot = 0U; slot < display_count(); ++slot) {
        if (!display_info(slot, &info)) {
            continue;
        }
        console_write("ST DRV display candidate ");
        console_write(info.name);
        console_write(" ");
        console_write(info.driver);
        console_write(" ");
        console_write(info.description);
        console_putc('\n');
        if (index == SIZE_MAX && (options->driver[0] == '\0' ||
                text_equal(info.driver, options->driver))) {
            index = slot;
        }
    }
    if (index == SIZE_MAX) {
        *reason = "no display adapter was bound by the named driver";
        return false;
    }
    /* A geometry the adapter does not list is refused, not approximated. */
    status = display_set_mode(index, DRIVER_TEST_DISPLAY_BOGUS_WIDTH,
        DRIVER_TEST_DISPLAY_BOGUS_HEIGHT, geometry[2], &mode);
    console_write("ST DRV display refusal ");
    console_write_u64(DRIVER_TEST_DISPLAY_BOGUS_WIDTH);
    console_putc('x');
    console_write_u64(DRIVER_TEST_DISPLAY_BOGUS_HEIGHT);
    console_write(" ");
    console_write(display_status_string(status));
    console_putc('\n');
    if (status != DISPLAY_STATUS_NO_SUCH_MODE) {
        *reason = "a mode the adapter does not list was not refused";
        return false;
    }
    /*
     * From here the adapter is the driver's: the kernel's screen console
     * and the VGA text mirror stop drawing on it before the mode changes.
     */
    if (screen_is_active()) {
        (void)screen_release();
    }
    console_release_vga_text();
    status = display_set_mode(index, geometry[0], geometry[1], geometry[2],
        &mode);
    if (status != DISPLAY_STATUS_OK) {
        console_write("ST DRV display set-mode ");
        console_write(display_status_string(status));
        console_putc('\n');
        *reason = "the driver did not set the requested mode";
        return false;
    }
    console_write("ST DRV display mode ");
    console_write_u64(mode.width);
    console_putc('x');
    console_write_u64(mode.height);
    console_putc('x');
    console_write_u64(mode.bits_per_pixel);
    console_write(" pitch ");
    console_write_u64(mode.pitch);
    console_write(" framebuffer ");
    console_write_hex(mode.framebuffer);
    console_write(" mode-number ");
    console_write_hex(mode.mode_number);
    console_write(mode.palette ? " palette" : " direct");
    console_putc('\n');
    if (!pattern_colors(&mode, colors)) {
        *reason = "the mode's pixel format is not one the plan draws";
        return false;
    }
    bytes = (mode.bits_per_pixel + 7U) / 8U;
    for (uint32_t y = 0U; y < mode.height; ++y) {
        volatile uint8_t *row = mode.pixels + (uint64_t)y * mode.pitch;

        for (uint32_t x = 0U; x < mode.width; ++x) {
            put_pixel(row + (uint64_t)x * bytes, bytes,
                quadrant_color(&mode, colors, x, y));
        }
    }
    /* Every pixel reads back as written: the mapping is the adapter's. */
    for (uint32_t y = 0U; y < mode.height; ++y) {
        const volatile uint8_t *row = mode.pixels + (uint64_t)y * mode.pitch;

        for (uint32_t x = 0U; x < mode.width; ++x) {
            if (get_pixel(row + (uint64_t)x * bytes, bytes) !=
                quadrant_color(&mode, colors, x, y)) {
                ++mismatches;
            }
        }
    }
    console_write("ST DRV display pattern ");
    console_write_u64((uint64_t)mode.width * mode.height);
    console_write(" pixels mismatches ");
    console_write_u64(mismatches);
    console_putc('\n');
    if (mismatches != 0U) {
        *reason = "the framebuffer did not hold the pattern";
        return false;
    }
    while (keyboard_read(&(struct keyboard_event){ 0 }) ==
        KEYBOARD_STATUS_OK) {
    }
    /* The runner captures the screen now and answers with a key press. */
    console_write("ST DRV display ready\n");
    deadline = clock_monotonic_ns() + DRIVER_TEST_DISPLAY_TIMEOUT_NS;
    while (!acknowledged && clock_monotonic_ns() < deadline) {
        struct keyboard_event event;

        while (keyboard_read(&event) == KEYBOARD_STATUS_OK) {
            if (event.pressed) {
                acknowledged = true;
            }
        }
        __asm__ volatile ("pause" : : : "memory");
    }
    if (!acknowledged) {
        *reason = "the runner never captured the screen";
        return false;
    }
    console_write("ST DRV display captured\n");
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
    (void)token_value(command_line, length, "openrfs.drvkind=",
        options.kind, sizeof(options.kind));
    (void)token_value(command_line, length, "openrfs.drvtext=",
        options.text, sizeof(options.text));
    (void)token_value(command_line, length, "openrfs.drvmode=",
        options.mode, sizeof(options.mode));
    if (token_value(command_line, length, "openrfs.drvunits=", number,
            sizeof(number)) && !parse_decimal(number, &options.units)) {
        *reason = "openrfs.drvunits is not a number";
        return false;
    }
    if (token_value(command_line, length, "openrfs.drvwrite=", number,
            sizeof(number)) && !parse_decimal(number, &options.write)) {
        *reason = "openrfs.drvwrite is not a number";
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
    if (text_equal(options.plan, "hid")) {
        return hid_plan(&options, reason);
    }
    if (text_equal(options.plan, "display")) {
        return display_plan(&options, reason);
    }
    if (text_equal(options.plan, "blk")) {
        if (!storage_plan(&options, reason)) {
            if (*reason == NULL) {
                *reason = "the block device reported errors";
            }
            return false;
        }
        return true;
    }
    *reason = "unknown openrfs.drvtest plan";
    return false;
}
