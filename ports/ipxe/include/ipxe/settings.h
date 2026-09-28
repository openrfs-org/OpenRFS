/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * iPXE's settings tree is not part of RSD; a settings block is an opaque
 * anchor so a driver can name one without the tree existing.
 */
#ifndef RSD_IPXE_SETTINGS_H
#define RSD_IPXE_SETTINGS_H

struct settings {
    int unused;
};

struct generic_settings {
    struct settings settings;
};

#endif
