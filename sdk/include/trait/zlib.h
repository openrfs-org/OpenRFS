/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TRAIT_SDK_ZLIB_H
#define TRAIT_SDK_ZLIB_H

#include <zlib.h>

/*
 * Prepare a zeroed Z_SOLO stream with Trait OS's checked calloc/free adapter.
 * The caller still owns the matching deflateEnd/inflateEnd on every path.
 */
int trait_zlib_stream_prepare(z_streamp stream);

#endif
