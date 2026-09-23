/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: platform detection.
 * runningOnQEMU answers from QEMU's fw_cfg signature, probed once.
 */
#ifndef OPENRFS_SEABIOS_FW_PARAVIRT_H
#define OPENRFS_SEABIOS_FW_PARAVIRT_H

#include "config.h"
#include "types.h"

static inline int runningOnQEMU(void) {
    return openrfs_seabios_running_on_qemu();
}

#endif
