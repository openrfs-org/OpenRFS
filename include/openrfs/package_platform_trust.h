/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_PACKAGE_PLATFORM_TRUST_H
#define OPENRFS_PACKAGE_PLATFORM_TRUST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/package_trust.h>

/* Generated build asset; it is linked into the kernel's read-only segment. */
extern const uint8_t openrfs_package_trust_asset[];
extern const size_t openrfs_package_trust_asset_bytes;

enum package_trust_status package_platform_trust_initialize(void);
bool package_platform_trust_manager(struct package_manager_trust *result);
size_t package_platform_trust_key_count(void);

#endif
