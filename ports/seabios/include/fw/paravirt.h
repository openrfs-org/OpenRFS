/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: platform detection.
 * runningOnQEMU answers from QEMU's fw_cfg signature, probed once.
 */
#ifndef OPENRFS_SEABIOS_FW_PARAVIRT_H
#define OPENRFS_SEABIOS_FW_PARAVIRT_H

#include "config.h"
#include "types.h"

/* QEMU's fw_cfg interface (QEMU docs/specs/fw_cfg.rst), for ramfb.c. */
typedef struct QemuCfgDmaAccess {
    u32 control;
    u32 length;
    u64 address;
} PACKED QemuCfgDmaAccess;

#define PORT_QEMU_CFG_CTL           0x0510
#define PORT_QEMU_CFG_DATA          0x0511
#define PORT_QEMU_CFG_DMA_ADDR_HIGH 0x0514
#define PORT_QEMU_CFG_DMA_ADDR_LOW  0x0518
#define QEMU_CFG_DMA_CTL_ERROR   0x01
#define QEMU_CFG_DMA_CTL_READ    0x02
#define QEMU_CFG_DMA_CTL_SKIP    0x04
#define QEMU_CFG_DMA_CTL_SELECT  0x08
#define QEMU_CFG_DMA_CTL_WRITE   0x10

static inline int runningOnQEMU(void) {
    return openrfs_seabios_running_on_qemu();
}

#endif
