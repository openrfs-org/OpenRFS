/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * ISA devices, with the structures of iPXE's include/ipxe/isa.h
 * (GPL-2.0-or-later) that a legacy ISA driver declares through
 * ISA_DRIVER(). iPXE gathers ISA drivers in a linker table and probes every
 * address each one lists; OpenRFS's glue probes the addresses of a driver
 * the command line named, one driver at a time.
 */
#ifndef OPENRFS_IPXE_ISA_H
#define OPENRFS_IPXE_ISA_H

#include <stdint.h>
#include <ipxe/device.h>
#include <ipxe/isa_ids.h>

struct isa_driver;

/* An ISA device, identified by its I/O address. */
struct isa_device {
    struct device dev;
    uint16_t ioaddr;
    struct isa_driver *driver;
    void *priv;
};

typedef uint16_t isa_probe_addr_t;

struct isa_driver {
    const char *name;
    isa_probe_addr_t *probe_addrs;
    unsigned int addr_count;
    uint16_t vendor_id;
    uint16_t prod_id;
    int (*probe)(struct isa_device *isa);
    void (*remove)(struct isa_device *isa);
};

/* The glue names its drivers explicitly; there is no linker table. */
#define __isa_driver

static inline void isa_set_drvdata(struct isa_device *isa, void *priv)
{
    isa->priv = priv;
}

static inline void *isa_get_drvdata(struct isa_device *isa)
{
    return isa->priv;
}

#define ISA_ROM(IMAGE, DESCRIPTION)

#endif
