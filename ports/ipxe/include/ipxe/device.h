/* SPDX-License-Identifier: GPL-3.0-only */
/* Hardware device description (include/ipxe/device.h). */
#ifndef OPENRFS_IPXE_DEVICE_H
#define OPENRFS_IPXE_DEVICE_H

#include <stddef.h>
#include <ipxe/list.h>

#define BUS_TYPE_PCI 1
#define BUS_TYPE_ISAPNP 2
#define BUS_TYPE_EISA 3
#define BUS_TYPE_MCA 4
#define BUS_TYPE_ISA 5
#define BUS_TYPE_TAP 6
#define BUS_TYPE_EFI 7
#define BUS_TYPE_XEN 8
#define BUS_TYPE_HV 9
#define BUS_TYPE_USB 10
#define BUS_TYPE_DT 11

struct device_description {
    unsigned int bus_type;
    unsigned int location;
    unsigned int vendor;
    unsigned int device;
    unsigned long class;
    unsigned long ioaddr;
    unsigned int irq;
};

struct device {
    char name[40];
    const char *driver_name;
    struct device_description desc;
    struct list_head siblings;
    struct list_head children;
    struct device *parent;
};

#endif
