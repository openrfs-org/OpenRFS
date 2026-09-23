/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS VGA drivers: configuration.
 *
 * SeaBIOS builds one VGA BIOS per card type, choosing the driver with
 * Kconfig (VGA_STANDARD_VGA, VGA_CIRRUS, VGA_ATI, VGA_BOCHS, DISPLAY_BOCHS,
 * VGA_RAMFB), and vgahw.h dispatches on those constants. OpenRFS does the
 * same: the VGA sources are compiled once per card type, selected by one
 * SEAVGA_VARIANT_* definition, and each build is linked into its own object
 * whose symbols are local. Everything else comes from the storage layer's
 * configuration.
 */
#ifndef OPENRFS_SEAVGA_CONFIG_H
#define OPENRFS_SEAVGA_CONFIG_H

#include_next "config.h"

#if defined(SEAVGA_VARIANT_STDVGA)
#define CONFIG_VGA_STANDARD_VGA 1
#else
#define CONFIG_VGA_STANDARD_VGA 0
#endif
#if defined(SEAVGA_VARIANT_BOCHSVGA)
#define CONFIG_VGA_BOCHS 1
#else
#define CONFIG_VGA_BOCHS 0
#endif
#if defined(SEAVGA_VARIANT_CIRRUS)
#define CONFIG_VGA_CIRRUS 1
#else
#define CONFIG_VGA_CIRRUS 0
#endif
#if defined(SEAVGA_VARIANT_ATI)
#define CONFIG_VGA_ATI 1
#else
#define CONFIG_VGA_ATI 0
#endif
#if defined(SEAVGA_VARIANT_BOCHSDISPLAY)
#define CONFIG_DISPLAY_BOCHS 1
#else
#define CONFIG_DISPLAY_BOCHS 0
#endif
#if defined(SEAVGA_VARIANT_RAMFB)
#define CONFIG_VGA_RAMFB 1
#else
#define CONFIG_VGA_RAMFB 0
#endif

#define CONFIG_VGA_EMULATE_TEXT (CONFIG_DISPLAY_BOCHS || CONFIG_VGA_RAMFB)
#define CONFIG_VGA_STDVGA_PORTS (!CONFIG_VGA_EMULATE_TEXT)
#define CONFIG_VGA_BOCHS_STDVGA CONFIG_VGA_BOCHS
#define CONFIG_VGA_BOCHS_VMWARE 0
#define CONFIG_VGA_BOCHS_QXL 0
#define CONFIG_VGA_BOCHS_VIRTIO 0
#define CONFIG_VGA_GEODEGX2 0
#define CONFIG_VGA_GEODELX 0
#define CONFIG_VGA_COREBOOT 0
#define CONFIG_VGA_OUTPUT_CRT 0
#define CONFIG_VGA_OUTPUT_PANEL 0
#define CONFIG_VGA_OUTPUT_CRT_PANEL 0
#define CONFIG_BUILD_VGABIOS 1
#define CONFIG_VGA_VBE 1
#define CONFIG_VGA_PCI 1
#define CONFIG_VGA_FIXUP_ASM 0
#define CONFIG_VGA_ALLOCATE_EXTRA_STACK 0
#define CONFIG_VGA_EXTRA_STACK_SIZE 512

#endif
