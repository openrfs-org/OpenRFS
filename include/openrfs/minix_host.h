/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_MINIX_HOST_H
#define OPENRFS_MINIX_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The boundary between the MINIX 3 audio drivers and the kernel.
 *
 * A MINIX audio driver implements the drv_* interface of <minix/audio_fw.h>
 * and links against libaudiodriver, which owns the device node, the DMA
 * buffer, the fragment bookkeeping and the interrupt path. OpenRFS compiles
 * each driver with ports/minix/audio_glue.c in libaudiodriver's place and
 * links the pair into one object whose only global symbol is that driver's
 * minix_<driver>_dispatch entry, so two drivers that both define drv_init,
 * sub_dev and drv never meet.
 *
 * The glue reproduces libaudiodriver's playback path for minor device 0
 * (/dev/audio): open_sub_dev() and init_buffers(), msg_ioctl(),
 * msg_write() with data_from_user() and get_started(), msg_hardware() with
 * handle_int_write(), and close_sub_dev(). The one difference is where the
 * interrupt comes from: MINIX runs msg_hardware() when the IRQ fires, and
 * OpenRFS runs it whenever the kernel services the device. Both paths ask
 * the driver first (drv_int_sum(), drv_int()), which reads the hardware's
 * own interrupt status.
 */

enum minix_audio_call_kind {
    /* drv_init(), the sub-device table, drv_init_hw(), drv_get_irq(). */
    MINIX_AUDIO_CALL_PROBE = 0,
    /* msg_open() of minor 0: the write channel and its DMA buffer. */
    MINIX_AUDIO_CALL_OPEN,
    /* msg_ioctl() on minor 0's control channel. */
    MINIX_AUDIO_CALL_CONFIGURE,
    /* msg_write(): hand over exactly one fragment. */
    MINIX_AUDIO_CALL_WRITE,
    /* msg_hardware(): collect the interrupts the device has raised. */
    MINIX_AUDIO_CALL_SERVICE,
    /* msg_close() of minor 0. */
    MINIX_AUDIO_CALL_CLOSE
};

/* What MINIX_AUDIO_CALL_CONFIGURE sets, as the DSPIO* ioctls name it. */
enum minix_audio_setting {
    MINIX_AUDIO_SET_RATE = 0,
    MINIX_AUDIO_SET_STEREO,
    MINIX_AUDIO_SET_BITS,
    MINIX_AUDIO_SET_SIGN
};

struct minix_audio_call {
    enum minix_audio_call_kind kind;
    /* The kernel's record of the device; an ISA card has no PCI function. */
    void *handle;
    enum minix_audio_setting setting;
    uint32_t value;
    const void *fragment;
    /*
     * Out: 0 or a MINIX error code. WRITE sets accepted when the fragment
     * went into the DMA buffer or the extra buffers, and leaves it clear
     * when both were full.
     */
    int result;
    bool accepted;
    /* Out, every call: the driver's name, IRQ and write channel state. */
    const char *driver_name;
    int irq;
    uint32_t fragment_bytes;
    uint32_t dma_fragments_queued;   /* sub_dev.DmaLength */
    uint32_t extra_fragments_queued; /* sub_dev.BufLength */
    bool dma_busy;
    bool out_of_data;
    uint64_t interrupts;             /* handle_int_write() runs */
    uint64_t fragments_written;
    uint64_t pauses;                 /* drv_pause() after running dry */
};

/* Kernel services for the glue, implemented in src/kernel/minix_host.c. */
void minix_host_console_write(const char *text);
_Noreturn void minix_host_panic(const char *text);
/*
 * Configuration access to the device's PCI function (false for an ISA
 * card). Writes go through the framework's claim authority: a command
 * register write that asks for I/O decode or bus mastering is granted
 * through the claim, with the device's arena as the only memory it may
 * master, and anything else the kernel does not grant is dropped.
 */
bool minix_host_config_read(void *handle, unsigned int offset,
    unsigned int width, uint32_t *value);
bool minix_host_config_write(void *handle, unsigned int offset,
    unsigned int width, uint32_t value);
/*
 * Memory from the device's own arena: identity-mapped, so the returned
 * address is also the bus address. The arena lies below 16 MiB for an ISA
 * card, whose 8237 DMA channel reaches no higher.
 */
void *minix_host_alloc(void *handle, size_t size, size_t alignment);

/* Glue entry points, one per driver. */
void minix_es1370_dispatch(struct minix_audio_call *call);
void minix_sb16_dispatch(struct minix_audio_call *call);

#endif
