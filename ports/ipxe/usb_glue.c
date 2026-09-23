/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The runtime iPXE's USB stack expects around it.
 *
 * iPXE's USB core (drivers/bus/usb.c) finds function drivers through a
 * linker table, starts its permanent process through iPXE's initialisation
 * pass and runs that process, and every hub's refill process, from iPXE's
 * scheduler (core/process.c, vendored). This file is linked with those
 * sources into one object by ports/ipxe/usb-layer.ld, which keeps every
 * .tbl.* section in name order exactly as iPXE's own linker script does, so
 * table_start(), table_end() and for_each_table_entry() mean what they mean
 * in iPXE. objcopy then makes every symbol local except the entry points
 * ports/ipxe/usb_glue.h declares.
 *
 * Everything else the USB drivers call - memory, DMA, PCI access, timers,
 * I/O buffers and the net_device layer - is the same ports/ipxe/ipxe_glue.c
 * the PCI network drivers use.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ipxe/acpimac.h>
#include <ipxe/init.h>
#include <ipxe/netdevice.h>
#include <ipxe/pci.h>
#include <ipxe/process.h>
#include <ipxe/tables.h>
#include <ipxe/usb.h>

#include "usb_glue.h"

/* The largest number of usb_driver entries one selection name covers. */
#define USB_GLUE_FUNCTION_DRIVERS 2U

struct usb_glue_host {
    const char *name;
    const char *label;
    const char *path;
    struct pci_driver *driver;
};

struct usb_glue_function {
    const char *name;
    const char *label;
    const char *path;
    struct usb_driver *drivers[USB_GLUE_FUNCTION_DRIVERS];
};

extern struct pci_driver xhci_driver;
extern struct pci_driver ehci_driver;
extern struct pci_driver uhci_driver;
extern struct usb_driver usb_hub_driver;
extern struct usb_driver ecm_driver;
extern struct usb_driver cdc_acm_driver;
extern struct usb_driver rf_rndis_driver;

static const struct usb_glue_host hosts[] = {
    { "ipxe-xhci", "xHCI USB controller", "src/drivers/usb/xhci.c",
        &xhci_driver },
    { "ipxe-ehci", "EHCI USB controller", "src/drivers/usb/ehci.c",
        &ehci_driver },
    { "ipxe-uhci", "UHCI USB controller", "src/drivers/usb/uhci.c",
        &uhci_driver }
};

/*
 * acm.c registers two usb_driver entries for the same RNDIS driver: one for
 * RNDIS behind the CDC-ACM class and one for the wireless-controller class
 * some devices use instead. Both are selected as "rndis".
 */
static const struct usb_glue_function functions[] = {
    { "ipxe-usbhub", "USB hub", "src/drivers/usb/usbhub.c",
        { &usb_hub_driver, NULL } },
    { "cdc-ecm", "USB CDC-ECM network adapter", "src/drivers/net/ecm.c",
        { &ecm_driver, NULL } },
    { "rndis", "USB RNDIS network adapter", "src/drivers/net/acm.c",
        { &cdc_acm_driver, &rf_rndis_driver } }
};

#define USB_GLUE_HOST_COUNT (sizeof(hosts) / sizeof(hosts[0]))
#define USB_GLUE_FUNCTION_COUNT (sizeof(functions) / sizeof(functions[0]))

static bool started;

/*
 * ecm.c asks for a system-specific MAC address (iPXE's core/acpimac.c looks
 * for an ACPI AMAC or MACA object, as some docking stations provide). OpenRFS
 * has no such lookup, which is what iPXE reports on a system without one: the
 * adapter keeps the address it reports itself.
 */
int acpi_mac(uint8_t *hw_addr)
{
    (void)hw_addr;
    return -ENOENT;
}

size_t ipxe_usb_host_count(void)
{
    return USB_GLUE_HOST_COUNT;
}

const char *ipxe_usb_host_name(size_t index)
{
    return index < USB_GLUE_HOST_COUNT ? hosts[index].name : NULL;
}

const char *ipxe_usb_host_label(size_t index)
{
    return index < USB_GLUE_HOST_COUNT ? hosts[index].label : NULL;
}

const char *ipxe_usb_host_path(size_t index)
{
    return index < USB_GLUE_HOST_COUNT ? hosts[index].path : NULL;
}

struct pci_driver *ipxe_usb_host_driver(size_t index)
{
    return index < USB_GLUE_HOST_COUNT ? hosts[index].driver : NULL;
}

size_t ipxe_usb_function_count(void)
{
    return USB_GLUE_FUNCTION_COUNT;
}

const char *ipxe_usb_function_name(size_t index)
{
    return index < USB_GLUE_FUNCTION_COUNT ? functions[index].name : NULL;
}

const char *ipxe_usb_function_path(size_t index)
{
    return index < USB_GLUE_FUNCTION_COUNT ? functions[index].path : NULL;
}

void ipxe_usb_start(bool (*enabled)(const char *name))
{
    struct init_fn *init_fn;

    if (started) {
        return;
    }
    started = true;
    for (size_t index = 0U; index < USB_GLUE_FUNCTION_COUNT; ++index) {
        if (enabled != NULL && enabled(functions[index].name)) {
            continue;
        }
        for (size_t entry = 0U; entry < USB_GLUE_FUNCTION_DRIVERS; ++entry) {
            if (functions[index].drivers[entry] != NULL) {
                functions[index].drivers[entry]->id_count = 0U;
            }
        }
    }
    /* iPXE's initialise(): every INIT_FNS entry, in table order. */
    for_each_table_entry(init_fn, INIT_FNS) {
        init_fn->initialise();
    }
}

void ipxe_usb_step(void)
{
    if (started) {
        step();
    }
}

bool ipxe_usb_describe_netdev(struct net_device *netdev,
    struct device **controller, const char **name, const char **path,
    char *description, size_t capacity)
{
    struct usb_function *func;

    if (netdev == NULL || netdev->dev == NULL ||
        netdev->dev->desc.bus_type != BUS_TYPE_USB) {
        return false;
    }
    func = container_of(netdev->dev, struct usb_function, dev);
    for (size_t index = 0U; index < USB_GLUE_FUNCTION_COUNT; ++index) {
        for (size_t entry = 0U; entry < USB_GLUE_FUNCTION_DRIVERS; ++entry) {
            if (functions[index].drivers[entry] == NULL ||
                functions[index].drivers[entry] != func->driver) {
                continue;
            }
            *controller = func->usb->port->hub->bus->dev;
            *name = functions[index].name;
            *path = functions[index].path;
            snprintf(description, capacity, "%s %04x:%04x (%s)",
                functions[index].label, func->desc.vendor,
                func->desc.product, func->name);
            return true;
        }
    }
    return false;
}
