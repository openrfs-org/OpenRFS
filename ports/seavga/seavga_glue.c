/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The SeaBIOS-facing half of one SeaBIOS VGA build.
 *
 * In SeaBIOS, vgainit.c probes the adapter, vgabios.c answers INT 10h and
 * vbe.c answers the VESA calls. None of those are compiled here: OpenRFS
 * calls the card driver directly, vgahw_setup() when the kernel binds the
 * adapter and vgahw_find_mode()/vgahw_set_mode() when it asks for a mode.
 * This file supplies what the card drivers reach for outside themselves:
 * the globals vgainit.c and vbe.c own, the vgabios.c helpers they call,
 * configuration access to the one adapter the kernel claimed, resolution
 * of the real-mode segments they address, and the fw_cfg checks that keep
 * ramfb.c from waiting on a DMA interface that is not there.
 *
 * It is compiled once per card type, with SEAVGA_VARIANT_<card> choosing
 * the driver in vgahw.h (see ports/seavga/include/config.h) and
 * SEAVGA_DISPATCH naming the build's one exported entry point.
 */
#include <stdarg.h>

#include <openrfs/seabios_host.h>
#include <openrfs/seavga_host.h>

#include "biosvar.h"
#include "byteorder.h"
#include "config.h"
#include "output.h"
#include "string.h"
#include "types.h"
#include "util.h"
#include "x86.h"
#include "hw/pci.h"
#include "hw/pci_regs.h"
#include "std/vga.h"
#include "stdvga.h"
#include "vgabios.h"
#include "vgafb.h"
#include "vgahw.h"
#include "vgautil.h"

#ifndef SEAVGA_DISPATCH
#error "SEAVGA_DISPATCH must name this build's entry point"
#endif

#define GLUE_PRINT_BUFFER 256
#define GLUE_MAX_MODES 128
#define GLUE_BAR_COUNT 6

/****************************************************************
 * State vgainit.c, vbe.c and the ROM header would own
 ****************************************************************/

int VgaBDF = -1;
int HaveRunInit;
u32 VBE_total_memory;
u32 VBE_capabilities;
u32 VBE_framebuffer;
u16 VBE_win_granularity;
u8 VBE_edid[256];
struct video_func_static static_functionality;
/* atiext.c records its fake BIOS tables here, in the ROM header. */
u16 _rom_header_ati_table_anchor;

/*
 * The VGA BIOS keeps its own state in the BIOS Data Area (vgabios.h's
 * VGA_CUSTOM_BDA). Like the storage layer's, this copy is private: segment
 * 0x40 resolves here, never to physical 0x400.
 */
struct bios_data_area_s openrfs_seabios_bda;
struct rmode_IVT openrfs_seabios_ivt;

_Static_assert(sizeof(struct bios_data_area_s) >=
               VGA_CUSTOM_BDA + sizeof(struct vga_bda_s),
               "the VGA BIOS state must fit in the private BDA");

static void *glue_handle;
static int glue_bound;
static int glue_bar_sizing[GLUE_BAR_COUNT];

/****************************************************************
 * Diagnostics (output.c)
 ****************************************************************/

int openrfs_seabios_vsnprintf(char *buffer, size_t size, const char *fmt,
                              va_list args);

static int at_line_start = 1;

static void glue_puts(const char *text)
{
    char line[GLUE_PRINT_BUFFER + 16];
    size_t used = 0;

    while (*text) {
        if (at_line_start) {
            const char *prefix = "SeaBIOS VGA: ";
            while (*prefix && used + 1 < sizeof(line))
                line[used++] = *prefix++;
            at_line_start = 0;
        }
        if (used + 1 >= sizeof(line)) {
            line[used] = '\0';
            seabios_host_console_write(line);
            used = 0;
        }
        line[used++] = *text;
        if (*text == '\n')
            at_line_start = 1;
        text++;
    }
    line[used] = '\0';
    if (used)
        seabios_host_console_write(line);
}

void __dprintf(const char *fmt, ...)
{
    char text[GLUE_PRINT_BUFFER];
    va_list args;

    va_start(args, fmt);
    openrfs_seabios_vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    glue_puts(text);
}

void panic(const char *fmt, ...)
{
    char text[GLUE_PRINT_BUFFER];
    va_list args;

    va_start(args, fmt);
    openrfs_seabios_vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    seabios_host_panic(text);
}

void __warn_internalerror(int lineno, const char *fname)
{
    dprintf(1, "WARNING - internal error detected at %s:%d!\n",
            fname, lineno);
}

void __warn_noalloc(int lineno, const char *fname)
{
    dprintf(1, "WARNING - Unable to allocate resource at %s:%d!\n",
            fname, lineno);
}

void __warn_timeout(int lineno, const char *fname)
{
    dprintf(1, "WARNING - Timeout at %s:%d!\n", fname, lineno);
}

/****************************************************************
 * Real-mode segments (farptr.h)
 ****************************************************************/

/*
 * Resolve seg:offset for an access of size bytes. Segment zero is flat
 * memory (GET_SEG and get_global_seg() are zero here). Segment 0x40 is the
 * private BIOS Data Area. Segments A000 to BFFF are the VGA memory windows
 * at physical 0xA0000-0xBFFFF, which the identity map covers and the
 * adapter decodes. Anything else is a driver path this port never takes,
 * and reaching it is a bug worth stopping for.
 */
static void *glue_far(u16 seg, const volatile void *offset, u32 size)
{
    u32 off = (u32)(unsigned long)offset;

    if (!seg)
        return (void *)offset;
    if (seg == SEG_BDA) {
        if (off > sizeof(openrfs_seabios_bda) ||
            size > sizeof(openrfs_seabios_bda) - off)
            panic("SeaBIOS VGA: BDA access at 0x%x (%u bytes)", off, size);
        return (u8 *)&openrfs_seabios_bda + off;
    }
    if (seg >= SEG_GRAPH && seg <= 0xBFFF && off <= 0xFFFF) {
        u32 linear = ((u32)seg << 4) + off;
        if (linear < 0xC0000 && size <= 0xC0000 - linear)
            return (void *)(unsigned long)linear;
    }
    panic("SeaBIOS VGA: far access to %04x:%04x (%u bytes)", seg, off, size);
}

void *openrfs_seavga_far(u16 seg, const volatile void *offset, u32 size)
{
    return glue_far(seg, offset, size);
}

void memcpy_far(u16 d_seg, void *d_far, u16 s_seg, const void *s_far,
                size_t len)
{
    if (!len)
        return;
    memmove(glue_far(d_seg, d_far, len), glue_far(s_seg, s_far, len), len);
}

void memset_far(u16 d_seg, void *d_far, u8 c, size_t len)
{
    volatile u8 *d;
    size_t index;

    if (!len)
        return;
    d = glue_far(d_seg, d_far, len);
    for (index = 0; index < len; index++)
        d[index] = c;
}

/* Video memory takes 16-bit stores here exactly as it does in SeaBIOS. */
void memset16_far(u16 d_seg, void *d_far, u16 c, size_t len)
{
    volatile u16 *d;
    size_t index;

    len /= 2;
    if (!len)
        return;
    d = glue_far(d_seg, d_far, len * 2);
    for (index = 0; index < len; index++)
        d[index] = c;
}

int memcmp_far(u16 s1seg, const void *s1, u16 s2seg, const void *s2,
               size_t n)
{
    if (!n)
        return 0;
    return memcmp(glue_far(s1seg, s1, n), glue_far(s2seg, s2, n), n);
}

/* vgafb.c's copy to the linear framebuffer; addresses are flat here. */
void memcpy_high(void *dest, void *src, u32 len)
{
    volatile u8 *d = dest;
    const u8 *s = src;
    u32 index;

    for (index = 0; index < len; index++)
        d[index] = s[index];
}

/****************************************************************
 * vgabios.c helpers the card drivers call
 ****************************************************************/

int vga_bpp(struct vgamode_s *vmode_g)
{
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case MM_TEXT:
        return 16;
    case MM_PLANAR:
        return 1;
    }
    u8 depth = GET_GLOBAL(vmode_g->depth);
    if (depth > 8)
        return ALIGN(depth, 8);
    return depth;
}

u16 calc_page_size(u8 memmodel, u16 width, u16 height)
{
    switch (memmodel) {
    case MM_TEXT:
        return ALIGN(width * height * 2, 2*1024);
    case MM_CGA:
        return 16*1024;
    default:
        return DIV_ROUND_UP(width * height, 8);
    }
}

/*
 * The INT 10h save/restore service is not offered, so no driver's
 * save_restore path is ever entered; refuse rather than pretend.
 */
int bda_save_restore(int cmd, u16 seg, void *data)
{
    return -1;
}

/*
 * vgafb.c's graphics operations serve INT 10h text output in emulated-text
 * modes and legacy mode switches. cbvga_set_mode() reaches them only for
 * MF_LEGACY or an extra-stack BIOS, and this port passes neither.
 */
void init_gfx_op(struct gfx_op *op, struct vgamode_s *curmode_g)
{
    panic("SeaBIOS VGA: init_gfx_op reached; no INT 10h text service");
}

void handle_gfx_op(struct gfx_op *op)
{
    panic("SeaBIOS VGA: handle_gfx_op reached; no INT 10h text service");
}

/* clext.c returns this INT 10h callback's address; nothing calls it. */
void a0h_callback(void)
{
    panic("SeaBIOS VGA: INT 10h callback reached");
}

/* cbvga_setup() (coreboot's framebuffer) is not a card type built here. */
struct cb_header *find_cb_table(void)
{
    return NULL;
}

void *find_cb_subtable(struct cb_header *cbh, u32 tag)
{
    return NULL;
}

u32 allocate_pmm(u32 size, int highmem, int aligned)
{
    return seavga_host_allocate_framebuffer(size);
}

/****************************************************************
 * PCI configuration access (pci.c) for the claimed adapter
 ****************************************************************/

/*
 * atiext.c sizes its framebuffer BAR the classic way: write all ones, read
 * the mask back, restore. The kernel does not let a driver rewrite a BAR of
 * a device that is decoding, so the sizing is answered from the claim's own
 * probe of that BAR, which was taken with decoding off.
 */
static int glue_bar_index(u32 addr)
{
    if (addr < PCI_BASE_ADDRESS_0 || addr > PCI_BASE_ADDRESS_5 || addr & 3)
        return -1;
    return (addr - PCI_BASE_ADDRESS_0) / 4;
}

static int glue_owns(u16 bdf)
{
    return glue_handle && VgaBDF >= 0 && bdf == (u16)VgaBDF;
}

static u32 glue_config_read(u16 bdf, u32 addr, unsigned int width)
{
    uint32_t value = 0xffffffff;

    if (!glue_owns(bdf)) {
        dprintf(1, "config read %04x:%02x refused: not this adapter\n",
                bdf, addr);
        return value;
    }
    if (width == 4) {
        int bar = glue_bar_index(addr);
        if (bar >= 0 && glue_bar_sizing[bar]) {
            uint64_t base = 0, size = 0;
            bool io = false;
            u32 current = 0;
            seavga_host_config_read(glue_handle, addr, 4, &current);
            if (!seavga_host_bar(glue_handle, bar, &base, &size, &io) ||
                !size)
                return 0;
            if (io)
                return ((u32)~(size - 1) & PCI_BASE_ADDRESS_IO_MASK)
                    | (current & ~PCI_BASE_ADDRESS_IO_MASK);
            return ((u32)~(size - 1) & PCI_BASE_ADDRESS_MEM_MASK)
                | (current & ~PCI_BASE_ADDRESS_MEM_MASK);
        }
    }
    if (!seavga_host_config_read(glue_handle, addr, width, &value))
        dprintf(1, "config read %04x:%02x failed\n", bdf, addr);
    return value;
}

static void glue_config_write(u16 bdf, u32 addr, u32 val, unsigned int width)
{
    int bar = width == 4 ? glue_bar_index(addr) : -1;

    if (!glue_owns(bdf)) {
        dprintf(1, "config write %04x:%02x refused: not this adapter\n",
                bdf, addr);
        return;
    }
    if (bar >= 0) {
        uint32_t current = 0;
        if (val == 0xffffffff) {
            glue_bar_sizing[bar] = 1;
            return;
        }
        if (seavga_host_config_read(glue_handle, addr, 4, &current)
            && val == current) {
            glue_bar_sizing[bar] = 0;
            return;
        }
    }
    if (!seavga_host_config_write(glue_handle, addr, width, val))
        dprintf(1, "config write %04x:%02x refused\n", bdf, addr);
}

u32 pci_config_readl(u16 bdf, u32 addr)
{
    return glue_config_read(bdf, addr, 4);
}

u16 pci_config_readw(u16 bdf, u32 addr)
{
    return glue_config_read(bdf, addr, 2);
}

u8 pci_config_readb(u16 bdf, u32 addr)
{
    return glue_config_read(bdf, addr, 1);
}

void pci_config_writel(u16 bdf, u32 addr, u32 val)
{
    glue_config_write(bdf, addr, val, 4);
}

void pci_config_writew(u16 bdf, u32 addr, u16 val)
{
    glue_config_write(bdf, addr, val, 2);
}

void pci_config_writeb(u16 bdf, u32 addr, u8 val)
{
    glue_config_write(bdf, addr, val, 1);
}

/****************************************************************
 * QEMU fw_cfg (ramfb.c)
 ****************************************************************/

#define GLUE_CFG_CTL 0x510
#define GLUE_CFG_DATA 0x511
#define GLUE_CFG_SIGNATURE 0x0000
#define GLUE_CFG_ID 0x0001
#define GLUE_CFG_VERSION_DMA 0x02

/*
 * ramfb.c talks to fw_cfg only through its DMA interface and waits for
 * each transfer without a timeout, as SeaBIOS may on QEMU. Before its setup
 * runs, the legacy interface must answer with QEMU's signature and report
 * the DMA feature; otherwise the build is not bound.
 */
static int glue_fw_cfg_dma_present(void)
{
    static const char signature[4] = "QEMU";
    u32 id = 0;
    int index;

    outw(GLUE_CFG_SIGNATURE, GLUE_CFG_CTL);
    for (index = 0; index < 4; index++)
        if (inb(GLUE_CFG_DATA) != (u8)signature[index])
            return 0;
    outw(GLUE_CFG_ID, GLUE_CFG_CTL);
    for (index = 0; index < 4; index++)
        id |= (u32)inb(GLUE_CFG_DATA) << (8 * index);
    return (id & GLUE_CFG_VERSION_DMA) != 0;
}

/****************************************************************
 * Entry point
 ****************************************************************/

static void glue_bind(struct seavga_call *call)
{
    int ret;

    call->result = -1;
    if (glue_bound) {
        dprintf(1, "this build already drives an adapter\n");
        return;
    }
    glue_handle = call->handle;
    VgaBDF = call->handle ? call->bdf : -1;
    if (CONFIG_VGA_RAMFB && !glue_fw_cfg_dma_present()) {
        dprintf(1, "ramfb: no QEMU fw_cfg DMA interface\n");
        return;
    }
    HaveRunInit = 0;
    ret = vgahw_setup();
    call->result = ret;
    call->framebuffer = VBE_framebuffer;
    call->total_memory = VBE_total_memory;
    if (ret == 0) {
        /* Later setups (a second vgahw_setup) must not re-probe. */
        HaveRunInit = 1;
        glue_bound = 1;
    }
}

/*
 * bochs-display and ramfb scan out one fixed framebuffer, set up in their
 * setup functions. cbvga also lists the smaller VBE modes that fit inside
 * it, for NTLDR, and "setting" one leaves the scanout as it was, so a
 * picture drawn for it lands in the top left corner. Only the mode that is
 * the whole framebuffer is offered.
 */
static int glue_native_geometry(struct vgamode_s *vmode_g, int linelength)
{
    u32 bytes = vga_bpp(vmode_g) / 8;

    if (linelength <= 0)
        return 0;
    return (u32)linelength == GET_GLOBAL(vmode_g->width) * bytes
        && VBE_total_memory / (u32)linelength == GET_GLOBAL(vmode_g->height);
}

static void glue_set_mode(struct seavga_call *call)
{
    u16 modes[GLUE_MAX_MODES];
    struct vgamode_s *vmode_g = NULL;
    u16 mode = 0xffff;
    int index, linear, ret, linelength;

    call->result = SEAVGA_RESULT_NO_SUCH_MODE;
    if (!glue_bound) {
        call->result = -1;
        return;
    }
    for (index = 0; index < GLUE_MAX_MODES; index++)
        modes[index] = 0xffff;
    vgahw_list_modes(0, modes, &modes[GLUE_MAX_MODES - 1]);
    for (index = 0; index < GLUE_MAX_MODES && modes[index] != 0xffff;
         index++) {
        struct vgamode_s *candidate = vgahw_find_mode(modes[index]);
        u8 memmodel;
        if (!candidate)
            continue;
        memmodel = GET_GLOBAL(candidate->memmodel);
        if ((memmodel != MM_PACKED && memmodel != MM_DIRECT)
            || GET_GLOBAL(candidate->width) != call->width
            || GET_GLOBAL(candidate->height) != call->height
            || GET_GLOBAL(candidate->depth) != call->bits_per_pixel)
            continue;
        vmode_g = candidate;
        mode = modes[index];
        break;
    }
    if (!vmode_g)
        return;
    /*
     * VBE modes (0x100 and up) scan out of the linear framebuffer; the
     * legacy packed mode 13h scans out of the 64 KiB window at A0000.
     */
    linear = mode >= 0x100 && VBE_framebuffer;
    if (mode >= 0x100 && !linear)
        return;
    /* cbvga reports its fixed line length before any mode is set. */
    if (CONFIG_VGA_EMULATE_TEXT
        && !glue_native_geometry(vmode_g, vgahw_get_linelength(vmode_g))) {
        dprintf(1, "mode %x is not the adapter's scanout geometry\n", mode);
        return;
    }
    ret = vgahw_set_mode(vmode_g, linear ? MF_LINEARFB : 0);
    if (ret) {
        call->result = ret < 0 ? ret : -1;
        return;
    }
    /*
     * The pitch is the one SeaBIOS's vbe.c reports in the mode information
     * block (bytes_per_scanline), which is what an OS draws with. Not every
     * card build can read a live line length back: atiext.c programs its
     * own CRTC_PITCH, which vgahw_get_linelength() does not look at.
     */
    linelength = vgahw_minimum_linelength(vmode_g);
    if (linelength <= 0) {
        call->result = -1;
        return;
    }
    call->mode_number = mode;
    call->memory_model = GET_GLOBAL(vmode_g->memmodel);
    call->pitch = linelength;
    call->linear = linear;
    call->framebuffer = linear ? VBE_framebuffer
        : (u32)GET_GLOBAL(vmode_g->sstart) << 4;
    call->result = 0;
}

void SEAVGA_DISPATCH(void *argument)
{
    struct seavga_call *call = argument;

    switch (call->kind) {
    case SEAVGA_CALL_BIND:
        glue_bind(call);
        break;
    case SEAVGA_CALL_SET_MODE:
        glue_set_mode(call);
        break;
    default:
        call->result = -1;
        break;
    }
}
