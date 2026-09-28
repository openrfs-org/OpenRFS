/* SPDX-License-Identifier: GPL-3.0-only */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *kind, const uint8_t *data,
                  uint32_t len)
{
    uint8_t head[4];
    uint32_t crc;

    be32(head, len);
    fwrite(head, 1, 4, f);
    fwrite(kind, 1, 4, f);
    if (len != 0U) {
        fwrite(data, 1, len, f);
    }
    crc = (uint32_t)crc32(crc32(0L, (const Bytef *)kind, 4),
                          (const Bytef *)data, len);
    be32(head, crc);
    fwrite(head, 1, 4, f);
}

int png_write(const char *path, const uint8_t *rgb, uint32_t w, uint32_t h)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n',
                                    0x1A, '\n' };
    uint8_t ihdr[13];
    uint8_t *raw;
    uint8_t *zbuf;
    uLongf zlen;
    FILE *f;
    uint32_t y;
    size_t rawlen = (size_t)h * ((size_t)w * 3U + 1U);

    raw = malloc(rawlen);
    if (raw == NULL) {
        return 1;
    }
    for (y = 0U; y < h; ++y) {
        raw[y * (w * 3U + 1U)] = 0U;          /* filter: none */
        memcpy(&raw[y * (w * 3U + 1U) + 1U], &rgb[(size_t)y * w * 3U],
               (size_t)w * 3U);
    }
    zlen = compressBound(rawlen);
    zbuf = malloc(zlen);
    if (zbuf == NULL || compress2(zbuf, &zlen, raw, rawlen, 9) != Z_OK) {
        free(raw); free(zbuf); return 1;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        free(raw); free(zbuf); return 1;
    }
    fwrite(sig, 1, 8, f);
    be32(ihdr, w); be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13U);
    chunk(f, "IDAT", zbuf, (uint32_t)zlen);
    chunk(f, "IEND", NULL, 0U);
    fclose(f);
    free(raw); free(zbuf);
    return 0;
}
