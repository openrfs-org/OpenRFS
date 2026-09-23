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
    /* Collect reports from polled input devices; NULL if the layer has none. */
    void (*poll_input)(void);
};

size_t hwdrv_layer_count(void);
const struct hwdrv_layer *hwdrv_layer_at(size_t index);

/* iPXE network drivers and USB stack: ports/ipxe, vendor/ipxe. */
enum hwdrv_status ipxe_layer_bind_all(void);
size_t ipxe_layer_driver_count(void);
const char *ipxe_layer_driver_name(size_t index);

enum hwdrv_status seabios_layer_bind_all(void);
size_t seabios_layer_driver_count(void);
const char *seabios_layer_driver_name(size_t index);
void seabios_layer_poll_input(void);

/* MINIX 3 audio drivers: ports/minix, vendor/minix. */
enum hwdrv_status minix_layer_bind_all(void);
size_t minix_layer_driver_count(void);
const char *minix_layer_driver_name(size_t index);

/* SeaBIOS VGA drivers: ports/seavga, vendor/seabios/vgasrc. */
enum hwdrv_status seavga_layer_bind_all(void);
size_t seavga_layer_driver_count(void);
const char *seavga_layer_driver_name(size_t index);

#endif
