/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * PCI devices for iPXE drivers.
 *
 * Register names, the pci_device/pci_driver layout and the helper contracts
 * follow iPXE's include/ipxe/pci.h and drivers/bus/pci.c (GPL-2.0-or-later
 * OR UBDL). Every configuration write, BAR mapping and bus-master request is
 * routed through the OpenRFS driver framework, which owns the claim; a driver
 * cannot move a BAR or enable decode the claim did not grant.
 */
#ifndef OPENRFS_IPXE_PCI_H
#define OPENRFS_IPXE_PCI_H

#include <stddef.h>
#include <stdint.h>
#include <ipxe/device.h>
#include <ipxe/dma.h>

#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND 0x04
#define PCI_COMMAND_IO 0x0001
#define PCI_COMMAND_MEM 0x0002
#define PCI_COMMAND_MASTER 0x0004
#define PCI_COMMAND_INVALIDATE 0x0010
#define PCI_COMMAND_PARITY 0x0040
#define PCI_COMMAND_SERR 0x0100
#define PCI_COMMAND_INTX_DISABLE 0x0400
#define PCI_STATUS 0x06
#define PCI_STATUS_CAP_LIST 0x0010
#define PCI_STATUS_PARITY 0x0100
#define PCI_STATUS_REC_TARGET_ABORT 0x1000
#define PCI_STATUS_REC_MASTER_ABORT 0x2000
#define PCI_STATUS_SIG_SYSTEM_ERROR 0x4000
#define PCI_STATUS_DETECTED_PARITY 0x8000
#define PCI_REVISION 0x08
#define PCI_REVISION_ID PCI_REVISION
#define PCI_CLASS_PROG 0x09
#define PCI_CLASS_DEVICE 0x0a
#define PCI_CACHE_LINE_SIZE 0x0c
#define PCI_LATENCY_TIMER 0x0d
#define PCI_HEADER_TYPE 0x0e
#define PCI_HEADER_TYPE_NORMAL 0x00
#define PCI_HEADER_TYPE_BRIDGE 0x01
#define PCI_HEADER_TYPE_CARDBUS 0x02
#define PCI_HEADER_TYPE_MASK 0x7f
#define PCI_HEADER_TYPE_MULTI 0x80
#define PCI_BASE_ADDRESS(n) (0x10 + (4 * (n)))
#define PCI_BASE_ADDRESS_0 PCI_BASE_ADDRESS(0)
#define PCI_BASE_ADDRESS_1 PCI_BASE_ADDRESS(1)
#define PCI_BASE_ADDRESS_2 PCI_BASE_ADDRESS(2)
#define PCI_BASE_ADDRESS_3 PCI_BASE_ADDRESS(3)
#define PCI_BASE_ADDRESS_4 PCI_BASE_ADDRESS(4)
#define PCI_BASE_ADDRESS_5 PCI_BASE_ADDRESS(5)
#define PCI_BASE_ADDRESS_SPACE_IO 0x00000001UL
#define PCI_BASE_ADDRESS_MEM_TYPE_64 0x00000004UL
#define PCI_BASE_ADDRESS_MEM_TYPE_MASK 0x00000006UL
#define PCI_BASE_ADDRESS_IO_MASK (~0x03UL)
#define PCI_BASE_ADDRESS_MEM_MASK (~0x0fUL)
#define PCI_SUBSYSTEM_VENDOR_ID 0x2c
#define PCI_SUBSYSTEM_ID 0x2e
#define PCI_ROM_ADDRESS 0x30
#define PCI_CAPABILITY_LIST 0x34
#define PCI_CB_CAPABILITY_LIST 0x14
#define PCI_INTERRUPT_LINE 0x3c
#define PCI_INTERRUPT_PIN 0x3d
#define PCI_MIN_GNT 0x3e
#define PCI_MAX_LAT 0x3f
#define PCI_CAP_ID 0x00
#define PCI_CAP_ID_PM 0x01
#define PCI_CAP_ID_VPD 0x03
#define PCI_CAP_ID_MSI 0x05
#define PCI_CAP_ID_VNDR 0x09
#define PCI_CAP_ID_EXP 0x10
#define PCI_CAP_ID_MSIX 0x11
#define PCI_CAP_ID_EA 0x14
#define PCI_CAP_NEXT 0x01
#define PCI_CAP_LEN 0x02
#define PCI_PM_CTRL 0x04
#define PCI_PM_CTRL_STATE_MASK 0x0003
#define PCI_PM_CTRL_PME_ENABLE 0x0100
#define PCI_PM_CTRL_PME_STATUS 0x8000
#define PCI_EXP_DEVCTL 0x08
#define PCI_EXP_DEVCTL_FLR 0x8000
#define PCI_MSIX_CTRL 0x02
#define PCI_MSIX_CTRL_ENABLE 0x8000
#define PCI_MSIX_CTRL_MASK 0x4000
#define PCI_MSIX_CTRL_SIZE(x) ((x) & 0x07ff)
#define PCI_CLASS_NETWORK 0x02
#define PCI_CLASS_BRIDGE 0x06
#define PCI_CLASS_SERIAL 0x0c
#define PCI_CLASS_SERIAL_USB 0x03
#define PCI_CLASS(base, sub, progif) \
    ((((base) & 0xff) << 16) | (((sub) & 0xff) << 8) | (((progif) & 0xff) << 0))
#define PCI_EXP_FLR_DELAY_MS 100

#define PCI_ANY_ID 0xffff
#define PCI_SEG(busdevfn) (((busdevfn) >> 16) & 0xffff)
#define PCI_BUS(busdevfn) (((busdevfn) >> 8) & 0xff)
#define PCI_SLOT(busdevfn) (((busdevfn) >> 3) & 0x1f)
#define PCI_FUNC(busdevfn) (((busdevfn) >> 0) & 0x07)
#define PCI_BUSDEVFN(segment, bus, slot, func) \
    (((segment) << 16) | ((bus) << 8) | ((slot) << 3) | ((func) << 0))
#define PCI_BASE_CLASS(class) ((class) >> 16)
#define PCI_SUB_CLASS(class) (((class) >> 8) & 0xff)
#define PCI_PROG_INTF(class) ((class) & 0xff)
#define PCI_FMT "%04x:%02x:%02x.%x"
#define PCI_ARGS(pci) PCI_SEG((pci)->busdevfn), PCI_BUS((pci)->busdevfn), \
    PCI_SLOT((pci)->busdevfn), PCI_FUNC((pci)->busdevfn)

/*
 * Legacy drivers name their own PCI_USES_* flags; PCI_DRIVER() discards its
 * class argument, exactly as upstream's macro does.
 */

struct pci_device_id {
    const char *name;
    uint16_t vendor;
    uint16_t device;
    unsigned long driver_data;
};

struct pci_class_id {
    uint32_t class;
    uint32_t mask;
};

#define PCI_CLASS_ID(base, sub, progif) { \
        .class = PCI_CLASS(base, sub, progif), \
        .mask = ((((base) == PCI_ANY_ID) ? 0x00 : 0xff) << 16) | \
            ((((sub) == PCI_ANY_ID) ? 0x00 : 0xff) << 8) | \
            ((((progif) == PCI_ANY_ID) ? 0x00 : 0xff) << 0), \
    }

struct openrfs_ipxe_pci;

struct pci_device {
    struct device dev;
    struct dma_device dma;
    unsigned long membase;
    unsigned long ioaddr;
    uint16_t vendor;
    uint16_t device;
    uint32_t class;
    uint8_t irq;
    uint8_t hdrtype;
    uint32_t busdevfn;
    struct pci_driver *driver;
    void *priv;
    struct pci_device_id *id;
    /* OpenRFS: the framework claim behind this device. */
    struct openrfs_ipxe_pci *openrfs;
};

struct pci_driver {
    struct pci_device_id *ids;
    unsigned int id_count;
    struct pci_class_id class;
    int (*probe)(struct pci_device *pci);
    void (*remove)(struct pci_device *pci);
};

/*
 * iPXE collects drivers through a linker table. OpenRFS names each compiled
 * driver explicitly in ports/ipxe/ipxe_glue.c instead, so the attribute only
 * has to keep the object alive.
 */
#define __pci_driver __attribute__((used))
#define __pci_driver_fallback __attribute__((used))

#define PCI_ID(_vendor, _device, _name, _description, _data) { \
        .vendor = _vendor, .device = _device, .name = _name, \
        .driver_data = _data }
#define PCI_ROM(_vendor, _device, _name, _description, _data) \
    PCI_ID(_vendor, _device, _name, _description, _data)

void adjust_pci_device(struct pci_device *pci);
int pci_bar_is_io(struct pci_device *pci, unsigned int reg);
unsigned long pci_bar_start(struct pci_device *pci, unsigned int reg);
unsigned long pci_bar_size(struct pci_device *pci, unsigned int reg);
void pci_bar_set(struct pci_device *pci, unsigned int reg,
    unsigned long start);
int pci_find_capability(struct pci_device *pci, int capability);
int pci_find_next_capability(struct pci_device *pci, int position,
    int capability);
void pci_reset(struct pci_device *pci, unsigned int exp);
void *pci_ioremap(struct pci_device *pci, unsigned long bus_addr,
    size_t len);
int pci_read_config_byte(struct pci_device *pci, unsigned int where,
    uint8_t *value);
int pci_read_config_word(struct pci_device *pci, unsigned int where,
    uint16_t *value);
int pci_read_config_dword(struct pci_device *pci, unsigned int where,
    uint32_t *value);
int pci_write_config_byte(struct pci_device *pci, unsigned int where,
    uint8_t value);
int pci_write_config_word(struct pci_device *pci, unsigned int where,
    uint16_t value);
int pci_write_config_dword(struct pci_device *pci, unsigned int where,
    uint32_t value);

static inline void pci_set_drvdata(struct pci_device *pci, void *priv)
{
    pci->priv = priv;
}

static inline void *pci_get_drvdata(struct pci_device *pci)
{
    return pci->priv;
}

#endif
