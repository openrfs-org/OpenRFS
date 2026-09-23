/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: build configuration.
 *
 * SeaBIOS generates autoconf.h from Kconfig. The values below are the
 * Kconfig defaults for every driver OpenRFS compiles, with two deliberate
 * differences: CONFIG_THREADS is off because OpenRFS runs each driver entry
 * to completion on one stack, and CONFIG_QEMU is decided at run time by the
 * same fw_cfg signature SeaBIOS's QEMU build trusts, so the three drivers
 * SeaBIOS only enables on QEMU (lsi-scsi, esp-scsi, mpt-scsi) decline on
 * other machines exactly as a non-QEMU SeaBIOS build would.
 */
#ifndef OPENRFS_SEABIOS_CONFIG_H
#define OPENRFS_SEABIOS_CONFIG_H

int openrfs_seabios_running_on_qemu(void);

#ifndef CONFIG_DEBUG_LEVEL
#define CONFIG_DEBUG_LEVEL 1
#endif
#define CONFIG_THREADS 0
#define CONFIG_DRIVES 1
#define CONFIG_HARDWARE_IRQ 1
#define CONFIG_QEMU_HARDWARE 1
#define CONFIG_QEMU (openrfs_seabios_running_on_qemu())

#define CONFIG_ATA 1
#define CONFIG_ATA_DMA 0
#define CONFIG_ATA_PIO32 0
#define CONFIG_MAX_ATA_INTERFACES 4
#define CONFIG_AHCI 1
#define CONFIG_SDCARD 1
#define CONFIG_VIRTIO_BLK 1
#define CONFIG_VIRTIO_SCSI 1
#define CONFIG_PVSCSI 1
#define CONFIG_ESP_SCSI 1
#define CONFIG_LSI_SCSI 1
#define CONFIG_MEGASAS 1
#define CONFIG_MPT_SCSI 1
#define CONFIG_FLOPPY 1
#define CONFIG_NVME 1

#define CONFIG_USB 1
#define CONFIG_USB_UHCI 1
#define CONFIG_USB_OHCI 1
#define CONFIG_USB_EHCI 1
#define CONFIG_USB_XHCI 1
#define CONFIG_USB_MSC 1
#define CONFIG_USB_UAS 1
#define CONFIG_USB_HUB 1
#define CONFIG_USB_KEYBOARD 1
#define CONFIG_USB_MOUSE 1
#define CONFIG_KEYBOARD 1
#define CONFIG_MOUSE 1

#define BUILD_BIOS_ADDR           0xf0000
#define BUILD_LOWRAM_END          0xa0000
#define SEG_IVT      0x0000
#define SEG_BDA      0x0040
#define SEG_BIOS     0xf000

#define DEBUG_ISR_0e 9
#define DEBUG_ISR_76 10
#define DEBUG_ISR_hwpic1 5
#define DEBUG_ISR_hwpic2 5

#endif
