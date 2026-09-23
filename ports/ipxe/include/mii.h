/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_IPXE_MII_COMPAT_H
#define OPENRFS_IPXE_MII_COMPAT_H

/* IEEE 802.3 MII register addresses and bit positions used by this port.
 * Keep this compatibility header limited to the hardware interface values
 * required by the selected iPXE drivers. */
#define MII_BMCR 0x00
#define MII_BMSR 0x01
#define MII_PHYSID1 0x02
#define MII_PHYSID2 0x03
#define MII_CTRL1000 0x09
#define MII_MMD_DATA 0x0e

#define BMCR_RESET 0x8000
#define BMCR_ANENABLE 0x1000
#define BMCR_ANRESTART 0x0200
#define BMSR_LSTATUS 0x0004
#define ADVERTISE_1000FULL 0x0200
#define ADVERTISE_1000HALF 0x0100

#endif
