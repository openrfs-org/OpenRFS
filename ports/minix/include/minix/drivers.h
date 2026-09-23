/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored MINIX 3 audio drivers.
 *
 * In MINIX 3 a driver is a user process, and <minix/drivers.h> gathers the
 * system library it links against. The audio drivers use a small part of
 * it: port I/O performed by the kernel on their behalf (sys_inb .. sys_outl
 * and the vectored sys_voutb), the PCI library's device list and
 * configuration access, printf and panic, and the error codes. This header
 * declares exactly those, with MINIX's names and types;
 * ports/minix/audio_glue.c implements them. The constants below are
 * MINIX's own (<minix/const.h>, <minix/devio.h>).
 */
#ifndef OPENRFS_MINIX_DRIVERS_H
#define OPENRFS_MINIX_DRIVERS_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#define OK 0
#define TRUE 1
#define FALSE 0
#define EXTERN extern
#define UNUSED(v) UNUSED_ ## v __attribute__((unused))

/* One (port, value) pair of a vectored port write. */
typedef struct { u16_t port; u8_t value; } pvb_pair_t;

void panic(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

/* MINIX's pv_set(), which refuses a port or value that does not fit. */
#define pv_set(pv, p, v) do {                                   \
        u32_t _p = (p), _v = (v);                               \
        (pv).port = _p;                                         \
        (pv).value = _v;                                        \
        if ((pv).port != _p || (pv).value != _v)                \
            panic("pv_set(" #pv ", " #p ", " #v ")");           \
    } while (0)

int sys_inb(int port, u32_t *value);
int sys_inw(int port, u32_t *value);
int sys_inl(int port, u32_t *value);
int sys_outb(int port, u32_t value);
int sys_outw(int port, u32_t value);
int sys_outl(int port, u32_t value);
int sys_voutb(pvb_pair_t *pvb_pairs, int nr_ports);

void pci_init(void);
int pci_first_dev(int *devindp, u16_t *vidp, u16_t *didp);
int pci_next_dev(int *devindp, u16_t *vidp, u16_t *didp);
void pci_reserve(int devind);
u8_t pci_attr_r8(int devind, int port);
u16_t pci_attr_r16(int devind, int port);
u32_t pci_attr_r32(int devind, int port);
void pci_attr_w8(int devind, int port, u8_t value);
void pci_attr_w16(int devind, int port, u16_t value);
void pci_attr_w32(int devind, int port, u32_t value);
char *pci_dev_name(u16_t vid, u16_t did);

#endif
