/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>

#include <openrfs/hwdrv_layers.h>

static const struct hwdrv_layer layers[] = {
    { "ipxe", ipxe_layer_bind_all, ipxe_layer_driver_count,
        ipxe_layer_driver_name }
};

size_t hwdrv_layer_count(void)
{
    return sizeof(layers) / sizeof(layers[0]);
}

const struct hwdrv_layer *hwdrv_layer_at(size_t index)
{
    return index < hwdrv_layer_count() ? &layers[index] : NULL;
}
