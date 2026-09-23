/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: threads.
 *
 * SeaBIOS can run device detection in cooperative threads. OpenRFS builds
 * the drivers with CONFIG_THREADS off, which is a configuration SeaBIOS
 * supports: run_thread runs the function to completion before returning,
 * yield only relaxes the processor, and the mutexes are never contended.
 */
#ifndef OPENRFS_SEABIOS_STACKS_H
#define OPENRFS_SEABIOS_STACKS_H

#include "types.h"

struct mutex_s { u32 isLocked; };

/*
 * ata.c builds bus-master PRD tables here, but only with CONFIG_ATA_DMA,
 * which OpenRFS leaves at its default of off; the symbol is deliberately
 * never defined, so enabling that path fails to link rather than using
 * memory no device owns.
 */
extern u8 ExtraStack[];

void yield(void);
void yield_toirq(void);
void run_thread(void (*func)(void*), void *data);
void wait_threads(void);
void mutex_lock(struct mutex_s *mutex);
void mutex_unlock(struct mutex_s *mutex);
void start_preempt(void);
void finish_preempt(void);
int wait_preempt(void);
void check_preempt(void);

/*
 * usb.c reaches the xHCI poll routine through call32_params because SeaBIOS
 * polls USB from 16-bit mode. Here everything is flat, so it is a call.
 */
#define call32_params(func, eax, edx, ecx, errret) ({   \
        (void)(ecx);                                    \
        (void)(errret);                                 \
        func((void *)(eax), (void *)(edx));             \
    })

#endif
