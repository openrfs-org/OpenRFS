/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored MINIX 3 audio drivers: the PCI
 * configuration offsets MINIX's <machine/pci.h> names, from the PCI Local
 * Bus Specification's type 0 header.
 */
#ifndef OPENRFS_MINIX_MACHINE_PCI_H
#define OPENRFS_MINIX_MACHINE_PCI_H

#define PCI_VID 0x00  /* vendor ID, 16-bit */
#define PCI_DID 0x02  /* device ID, 16-bit */
#define PCI_CR 0x04   /* command register, 16-bit */
#define PCI_SR 0x06   /* status register, 16-bit */
#define PCI_REV 0x08  /* revision ID */
#define PCI_BAR 0x10  /* first base address register */
#define PCI_ILR 0x3C  /* interrupt line register */

#endif
