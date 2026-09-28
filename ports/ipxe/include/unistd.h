/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RSD_IPXE_UNISTD_H
#define RSD_IPXE_UNISTD_H

#include <stddef.h>
#include <stdarg.h>
#include <ipxe/timer.h>

static inline __attribute__((always_inline)) void usleep(unsigned long usecs)
{
    udelay(usecs);
}

#endif
