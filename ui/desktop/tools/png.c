/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * A PNG writer, for the harness only.
 *
 * The shell itself never encodes anything: this exists so a frame can be
 * looked at, which is the only honest way to check a thing that draws.
 * Stored (uncompressed) deflate blocks - a frame is written once and read
 * by a human, so the bytes are not worth a compressor.
 */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    for (uint32_t n = 0U; n < 256U; ++n) {
        uint32_t c = n;

        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static uint32_t crc(const uint8_t *buf, size_t len, uint32_t start)
{
    uint32_t c = start;

    if (!crc_ready) {
        crc_init();
    }
    for (size_t n = 0U; n < len; ++n) {
        c = crc_table[(c ^ buf[n]) & 0xFFU] ^ (c >> 8);
    }
    return c;
}

static void be32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static int chunk(FILE *file, const char *tag, const uint8_t *data,
    size_t len)
{
    uint8_t header[4];
    uint8_t tail[4];
    uint32_t sum;

    be32(header, (uint32_t)len);
    if (fwrite(header, 1U, 4U, file) != 4U) {
        return 0;
    }
    if (fwrite(tag, 1U, 4U, file) != 4U) {
        return 0;
    }
    sum = crc((const uint8_t *)tag, 4U, 0xFFFFFFFFU);
    if (len != 0U) {
        if (fwrite(data, 1U, len, file) != len) {
            return 0;
        }
        sum = crc(data, len, sum);
    }
    be32(tail, sum ^ 0xFFFFFFFFU);
    return fwrite(tail, 1U, 4U, file) == 4U;
}

int png_write(const char *path, const uint32_t *pixels, uint32_t width,
    uint32_t height)
{
    static const uint8_t SIGNATURE[8] = {
        137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U
    };
    FILE *file = fopen(path, "wb");
    uint8_t ihdr[13];
    size_t raw_len = (size_t)height * ((size_t)width * 3U + 1U);
    uint8_t *raw;
    uint8_t *idat;
    size_t idat_len;
    size_t at = 0U;
    uint32_t a = 1U;
    uint32_t b = 0U;
    int ok;

    if (file == NULL) {
        return 0;
    }
    raw = malloc(raw_len);
    if (raw == NULL) {
        (void)fclose(file);
        return 0;
    }
    for (uint32_t y = 0U; y < height; ++y) {
        raw[at++] = 0U;               /* filter: none */
        for (uint32_t x = 0U; x < width; ++x) {
            uint32_t p = pixels[(size_t)y * width + x];

            raw[at++] = (uint8_t)(p >> 16);
            raw[at++] = (uint8_t)(p >> 8);
            raw[at++] = (uint8_t)p;
        }
    }
    for (size_t n = 0U; n < raw_len; ++n) {
        a = (a + raw[n]) % 65521U;
        b = (b + a) % 65521U;
    }

    /* zlib: 2 header bytes, stored blocks of at most 65535, adler32. */
    size_t blocks = (raw_len + 65534U) / 65535U;

    idat_len = 2U + raw_len + blocks * 5U + 4U;
    idat = malloc(idat_len);
    if (idat == NULL) {
        free(raw);
        (void)fclose(file);
        return 0;
    }
    idat[0] = 0x78U;
    idat[1] = 0x01U;
    at = 2U;
    for (size_t n = 0U; n < raw_len; n += 65535U) {
        size_t run = raw_len - n > 65535U ? 65535U : raw_len - n;

        idat[at++] = (n + run >= raw_len) ? 1U : 0U;
        idat[at++] = (uint8_t)run;
        idat[at++] = (uint8_t)(run >> 8);
        idat[at++] = (uint8_t)(~run);
        idat[at++] = (uint8_t)(~run >> 8);
        memcpy(idat + at, raw + n, run);
        at += run;
    }
    be32(idat + at, (b << 16) | a);
    at += 4U;

    be32(ihdr, width);
    be32(ihdr + 4, height);
    ihdr[8] = 8U;    /* bit depth */
    ihdr[9] = 2U;    /* truecolour */
    ihdr[10] = 0U;
    ihdr[11] = 0U;
    ihdr[12] = 0U;

    ok = fwrite(SIGNATURE, 1U, 8U, file) == 8U &&
        chunk(file, "IHDR", ihdr, sizeof(ihdr)) &&
        chunk(file, "IDAT", idat, at) &&
        chunk(file, "IEND", NULL, 0U);
    free(idat);
    free(raw);
    (void)fclose(file);
    return ok;
}
