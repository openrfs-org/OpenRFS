/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Time for iPXE drivers (include/ipxe/timer.h). currticks() counts
 * milliseconds of the OpenRFS monotonic clock, so TICKS_PER_SEC is 1000;
 * iPXE's BIOS build uses 1024, and drivers only ever scale by the macro.
 */
#ifndef OPENRFS_IPXE_TIMER_H
#define OPENRFS_IPXE_TIMER_H

#define TICKS_PER_SEC 1000
#define TICKS_PER_MS 1

void udelay(unsigned long usecs);
void mdelay(unsigned long msecs);
unsigned long currticks(void);
unsigned int sleep(unsigned int seconds);
void sleep_fixed(unsigned int seconds);

#endif
