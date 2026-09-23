/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: PCI configuration
 * space layout, as defined by the PCI Local Bus Specification 3.0 (type 0
 * header, capability list) with the names SeaBIOS's drivers use.
 */
#ifndef OPENRFS_SEABIOS_PCI_REGS_H
#define OPENRFS_SEABIOS_PCI_REGS_H

#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define  PCI_COMMAND_IO         0x1
#define  PCI_COMMAND_MEMORY     0x2
#define  PCI_COMMAND_MASTER     0x4
#define PCI_STATUS              0x06
#define  PCI_STATUS_CAP_LIST    0x10
#define PCI_CLASS_REVISION      0x08
#define PCI_REVISION_ID         0x08
#define PCI_CLASS_PROG          0x09
#define PCI_CLASS_DEVICE        0x0a
#define PCI_HEADER_TYPE         0x0e
#define PCI_BASE_ADDRESS_0      0x10
#define PCI_BASE_ADDRESS_1      0x14
#define PCI_BASE_ADDRESS_2      0x18
#define PCI_BASE_ADDRESS_3      0x1c
#define PCI_BASE_ADDRESS_4      0x20
#define PCI_BASE_ADDRESS_5      0x24
#define  PCI_BASE_ADDRESS_SPACE         0x01
#define  PCI_BASE_ADDRESS_SPACE_IO      0x01
#define  PCI_BASE_ADDRESS_SPACE_MEMORY  0x00
#define  PCI_BASE_ADDRESS_MEM_TYPE_MASK 0x06
#define  PCI_BASE_ADDRESS_MEM_TYPE_32   0x00
#define  PCI_BASE_ADDRESS_MEM_TYPE_64   0x04
#define  PCI_BASE_ADDRESS_MEM_PREFETCH  0x08
#define  PCI_BASE_ADDRESS_MEM_MASK      (~0x0fUL)
#define  PCI_BASE_ADDRESS_IO_MASK       (~0x03UL)
#define PCI_ROM_ADDRESS         0x30
#define PCI_CAPABILITY_LIST     0x34
#define PCI_INTERRUPT_LINE      0x3c
#define PCI_INTERRUPT_PIN       0x3d

#define PCI_CAP_LIST_ID         0
#define PCI_CAP_LIST_NEXT       1
#define PCI_CAP_FLAGS           2
#define PCI_CAP_ID_PM           0x01
#define PCI_CAP_ID_MSI          0x05
#define PCI_CAP_ID_VNDR         0x09
#define PCI_CAP_ID_EXP          0x10
#define PCI_CAP_ID_MSIX         0x11

#endif
