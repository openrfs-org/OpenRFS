/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_SEAVGA_HOST_H
#define OPENRFS_SEAVGA_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The boundary between the SeaBIOS VGA drivers and the kernel.
 *
 * SeaBIOS builds one VGA BIOS per card type: vgahw.h dispatches on Kconfig
 * constants, so a build contains exactly one card's driver (plus the
 * standard VGA code every card falls back on). OpenRFS compiles vgasrc/ the
 * same way, once per card type, and links each build into its own object
 * whose only global symbol is that card's seavga_<card>_dispatch entry. The
 * kernel runs every entry through seabios_host_run(), on the SeaBIOS
 * layer's arena stack, because ramfb.c hands QEMU's fw_cfg DMA engine the
 * addresses of its stack variables.
 *
 * Like SeaBIOS's VGA BIOS, a build drives one adapter: the mode tables and
 * the VBE_* globals it fills in at setup belong to that adapter.
 */

enum seavga_call_kind {
    /* vgahw_setup(): probe the adapter and fill in its mode tables. */
    SEAVGA_CALL_BIND = 0,
    /* vgahw_find_mode() + vgahw_set_mode() for an exact width/height/depth. */
    SEAVGA_CALL_SET_MODE
};

/* A SET_MODE result: the adapter lists no mode of that geometry. */
#define SEAVGA_RESULT_NO_SUCH_MODE (-2)

struct seavga_call {
    enum seavga_call_kind kind;
    void *handle;               /* the claimed adapter; NULL for ramfb */
    int bdf;                    /* bus << 8 | device << 3 | function */
    uint32_t width;
    uint32_t height;
    uint32_t bits_per_pixel;
    /* Out: 0, or negative when the driver refused. */
    int result;
    uint32_t mode_number;
    uint32_t memory_model;      /* SeaBIOS's MM_* */
    uint32_t pitch;
    uint64_t framebuffer;       /* physical */
    uint32_t total_memory;      /* VBE_total_memory after setup */
    bool linear;                /* scanned out of the linear framebuffer */
};

/* Kernel services for the VGA glue, implemented in src/kernel/seavga_host.c. */
bool seavga_host_config_read(void *handle, unsigned int offset,
    unsigned int width, uint32_t *value);
bool seavga_host_config_write(void *handle, unsigned int offset,
    unsigned int width, uint32_t value);
/* A BAR as the claim recorded it at probe time, for BAR sizing reads. */
bool seavga_host_bar(void *handle, unsigned int bar_index, uint64_t *base,
    uint64_t *size, bool *io);
/*
 * allocate_pmm() for ramfb.c: physically contiguous, identity-mapped memory
 * below 4 GiB for a framebuffer that lives in RAM. Returns the physical
 * address, or zero.
 */
uint32_t seavga_host_allocate_framebuffer(uint32_t size);

/* Glue entry points, one per card type (ports/seavga/seavga_glue.c). */
void seavga_stdvga_dispatch(void *call);
void seavga_bochsvga_dispatch(void *call);
void seavga_cirrus_dispatch(void *call);
void seavga_ati_dispatch(void *call);
void seavga_bochs_display_dispatch(void *call);
void seavga_ramfb_dispatch(void *call);

#endif
