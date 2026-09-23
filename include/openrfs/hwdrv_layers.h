/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_HWDRV_LAYERS_H
#define OPENRFS_HWDRV_LAYERS_H

#include <stddef.h>

#include <openrfs/hwdrv.h>

/*
 * The compatibility layers the framework binds, in binding order. Each layer
 * walks the enumerated PCI functions itself, binds what its compiled drivers
 * match and records every binding through hwdrv_record_binding().
 */
struct hwdrv_layer {
    const char *name;
    enum hwdrv_status (*bind_all)(void);
    size_t (*driver_count)(void);
    const char *(*driver_name)(size_t index);
};

size_t hwdrv_layer_count(void);
const struct hwdrv_layer *hwdrv_layer_at(size_t index);

/* iPXE network drivers: ports/ipxe, vendor/ipxe. */
enum hwdrv_status ipxe_layer_bind_all(void);
size_t ipxe_layer_driver_count(void);
const char *ipxe_layer_driver_name(size_t index);

#endif
