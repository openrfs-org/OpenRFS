/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>

#include <openrfs/hwdrv_layers.h>

static const struct hwdrv_layer layers[] = {
    /*
     * SeaBIOS first: its floppy driver needs a buffer below 16 MiB, and the
     * frame allocator is first-fit, so the layer that needs low memory asks
     * before any other arena exists.
     */
    { "seabios", seabios_layer_bind_all, seabios_layer_driver_count,
        seabios_layer_driver_name, seabios_layer_poll_input },
    /* The ISA sound card's DMA buffer must also lie below 16 MiB. */
    { "minix", minix_layer_bind_all, minix_layer_driver_count,
        minix_layer_driver_name, NULL },
    { "ipxe", ipxe_layer_bind_all, ipxe_layer_driver_count,
        ipxe_layer_driver_name, NULL },
    /* Display adapters, bound only when named (see seavga_host.c). */
    { "seavga", seavga_layer_bind_all, seavga_layer_driver_count,
        seavga_layer_driver_name, NULL }
};

size_t hwdrv_layer_count(void)
{
    return sizeof(layers) / sizeof(layers[0]);
}

const struct hwdrv_layer *hwdrv_layer_at(size_t index)
{
    return index < hwdrv_layer_count() ? &layers[index] : NULL;
}
