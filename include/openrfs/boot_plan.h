/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_BOOT_PLAN_H
#define OPENRFS_BOOT_PLAN_H

#include <stdbool.h>
#include <stdint.h>

#include <openrfs/boot_ledger.h>
#include <openrfs/ui.h>

void boot_context_initialize(
    struct boot_context *context,
    uint32_t multiboot_magic,
    uintptr_t multiboot_information_address
);
enum boot_ledger_status boot_plan_build(struct boot_ledger *ledger);
bool boot_plan_pointer_absence_self_test(void);
enum ui_status boot_plan_start_desktop(void);

#endif
