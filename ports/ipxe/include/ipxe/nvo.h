/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Non-volatile option storage (include/ipxe/nvo.h). OpenRFS has no iPXE
 * settings tree, so an NVO block is described but never registered; the
 * driver keeps working exactly as iPXE does when registration is refused.
 */
#ifndef OPENRFS_IPXE_NVO_H
#define OPENRFS_IPXE_NVO_H

#include <stddef.h>
#include <ipxe/nvs.h>
#include <ipxe/refcnt.h>
#include <ipxe/settings.h>

struct nvo_block {
    struct settings settings;
    struct nvs_device *nvs;
    unsigned int address;
    size_t len;
    void *data;
    int (*resize)(struct nvo_block *nvo, size_t len);
    struct refcnt *refcnt;
};

static inline void nvo_init(struct nvo_block *nvo, struct nvs_device *nvs,
    size_t address, size_t len,
    int (*resize)(struct nvo_block *nvo, size_t len), struct refcnt *refcnt)
{
    nvo->nvs = nvs;
    nvo->address = (unsigned int)address;
    nvo->len = len;
    nvo->data = 0;
    nvo->resize = resize;
    nvo->refcnt = refcnt;
}

static inline int register_nvo(struct nvo_block *nvo, struct settings *parent)
{
    (void)nvo;
    (void)parent;
    return 0;
}

static inline void unregister_nvo(struct nvo_block *nvo)
{
    (void)nvo;
}

#endif
