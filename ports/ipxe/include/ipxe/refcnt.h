/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Reference counting (include/ipxe/refcnt.h). A count of zero means one
 * reference, as in iPXE; ref_put() below zero calls the free method.
 */
#ifndef OPENRFS_IPXE_REFCNT_H
#define OPENRFS_IPXE_REFCNT_H

struct refcnt {
    int count;
    void (*free)(struct refcnt *refcnt);
};

static inline void ref_init(struct refcnt *refcnt,
    void (*release)(struct refcnt *refcnt))
{
    refcnt->count = 0;
    refcnt->free = release;
}

static inline void ref_get(struct refcnt *refcnt)
{
    if (refcnt != 0) {
        ++refcnt->count;
    }
}

static inline void ref_put(struct refcnt *refcnt)
{
    if (refcnt == 0) {
        return;
    }
    if (--refcnt->count >= 0) {
        return;
    }
    if (refcnt->free != 0) {
        refcnt->free(refcnt);
    }
}

#endif
