/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The entry points of the iPXE USB layer (ports/ipxe/usb_glue.c) for the
 * rest of the iPXE glue. The USB layer is linked into one object whose only
 * global symbols are these (ports/ipxe/usb-exports.txt), so iPXE's USB core,
 * its host controller and function drivers and its process scheduler never
 * meet the kernel's own symbols.
 */
#ifndef OPENRFS_IPXE_USB_GLUE_H
#define OPENRFS_IPXE_USB_GLUE_H

#include <stdbool.h>
#include <stddef.h>

struct device;
struct net_device;
struct pci_driver;

/* Host controller drivers (PCI), bound only when named. */
size_t ipxe_usb_host_count(void);
const char *ipxe_usb_host_name(size_t index);
const char *ipxe_usb_host_label(size_t index);
const char *ipxe_usb_host_path(size_t index);
struct pci_driver *ipxe_usb_host_driver(size_t index);

/* USB function drivers (hub, CDC-ECM, RNDIS), by selection name. */
size_t ipxe_usb_function_count(void);
const char *ipxe_usb_function_name(size_t index);
const char *ipxe_usb_function_path(size_t index);

/*
 * Before the first controller probe: iPXE's initialisation pass, which
 * starts the USB core's permanent process, and the driver selection. A
 * function driver that enabled() refuses keeps its table entry but matches
 * no device, so the USB core picks configurations as if it were absent.
 */
void ipxe_usb_start(bool (*enabled)(const char *name));

/* One pass of iPXE's scheduler: bus polling, hot-plug, hub refills. */
void ipxe_usb_step(void);

/*
 * The USB function behind a net_device a function driver registered: the
 * generic device of the host controller whose bus it is on, the driver's
 * selection name and source path, and a description naming the USB device.
 * False if netdev does not belong to a USB function.
 */
bool ipxe_usb_describe_netdev(struct net_device *netdev,
    struct device **controller, const char **name, const char **path,
    char *description, size_t capacity);

#endif
