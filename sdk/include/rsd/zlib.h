/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_SDK_ZLIB_H
#define RSD_SDK_ZLIB_H

#include <zlib.h>

/*
 * Prepare a zeroed Z_SOLO stream with RSD's checked calloc/free adapter.
 * The caller still owns the matching deflateEnd/inflateEnd on every path.
 */
int rsd_zlib_stream_prepare(z_streamp stream);

#endif
