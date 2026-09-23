/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * libaudiodriver's place for one vendored MINIX 3 audio driver.
 *
 * A MINIX audio driver is only the drv_* half of a driver process: MINIX's
 * libaudiodriver (minix/lib/libaudiodriver/audio_fw.c) is the other half. It
 * owns /dev/audio, allocates each channel's DMA buffer, keeps the ring of
 * DMA fragments and the extra buffers behind it full, and on every
 * interrupt asks the driver which channel finished a fragment. This file
 * does the same for the playback channel of minor device 0, following
 * audio_fw.c's functions step for step - open_sub_dev() and
 * init_buffers(), msg_ioctl(), msg_write() with data_from_user() and
 * get_started(), msg_hardware() with handle_int_write(), close_sub_dev() -
 * with two differences, both at the edge of the process:
 *
 * - Data arrives as whole fragments handed over by the kernel instead of
 *   through safecopies from a blocked writer. A write that finds the DMA
 *   buffer and the extra buffers full is refused rather than parked, and
 *   the kernel services the device before offering it again.
 * - msg_hardware() runs when the kernel services the device instead of
 *   when the IRQ message arrives. Either way it starts by asking the
 *   driver (drv_int_sum(), drv_int()), which reads the device's interrupt
 *   status, so nothing is serviced that the hardware did not report.
 *
 * It also supplies the part of MINIX's system library the drivers call:
 * port I/O (which the MINIX kernel performs for a driver process, and
 * which runs directly here), the PCI library over the one function the
 * kernel claimed, printf and panic.
 *
 * Compiled once per driver, with MINIX_AUDIO_DISPATCH naming the entry.
 */
#include <stdarg.h>
#include <stdbool.h>

#include <openrfs/minix_host.h>

#include <machine/pci.h>
#include <minix/drivers.h>
#include <minix/audio_fw.h>
#include <sys/ioc_sound.h>

#ifndef MINIX_AUDIO_DISPATCH
#error "MINIX_AUDIO_DISPATCH must name this build's entry point"
#endif

#define GLUE_PRINT_BUFFER 256

static void *glue_handle;
static bool glue_probed;
static int glue_irq = -1;
static uint64_t glue_interrupts;
static uint64_t glue_fragments_written;
static uint64_t glue_pauses;

/****************************************************************
 * printf and panic
 ****************************************************************/

struct glue_out {
    char *buffer;
    size_t size;
    size_t used;
};

static void glue_put(struct glue_out *out, char c)
{
    if (out->used + 1 < out->size)
        out->buffer[out->used] = c;
    out->used++;
}

static void glue_number(struct glue_out *out, unsigned long long value,
                        unsigned int base, int negative, int width,
                        char pad)
{
    char digits[24];
    int count = 0;

    do {
        unsigned int digit = (unsigned int)(value % base);
        digits[count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value && count < (int)sizeof(digits));
    if (negative)
        digits[count++] = '-';
    while (width-- > count)
        glue_put(out, pad);
    while (count)
        glue_put(out, digits[--count]);
}

static int glue_vsnprintf(char *buffer, size_t size, const char *fmt,
                          va_list args)
{
    struct glue_out out = { buffer, size, 0 };

    for (; *fmt; fmt++) {
        int longs = 0, width = 0;
        char pad = ' ';

        if (*fmt != '%') {
            glue_put(&out, *fmt);
            continue;
        }
        fmt++;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') {
            longs++;
            fmt++;
        }
        switch (*fmt) {
        case 'd':
        case 'i': {
            long long value = longs ? va_arg(args, long) : va_arg(args, int);
            glue_number(&out, value < 0 ? -(unsigned long long)value
                        : (unsigned long long)value, 10, value < 0, width,
                        pad);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long long value = longs ? va_arg(args, unsigned long)
                : va_arg(args, unsigned int);
            glue_number(&out, value, *fmt == 'u' ? 10 : 16, 0, width, pad);
            break;
        }
        case 'p':
            glue_put(&out, '0');
            glue_put(&out, 'x');
            glue_number(&out, (unsigned long)va_arg(args, void *), 16, 0, 0,
                        ' ');
            break;
        case 's': {
            const char *text = va_arg(args, const char *);
            if (!text)
                text = "(null)";
            while (*text)
                glue_put(&out, *text++);
            break;
        }
        case 'c':
            glue_put(&out, (char)va_arg(args, int));
            break;
        case '%':
            glue_put(&out, '%');
            break;
        default:
            glue_put(&out, '%');
            if (*fmt)
                glue_put(&out, *fmt);
            else
                fmt--;
            break;
        }
    }
    if (size)
        buffer[out.used < size ? out.used : size - 1] = '\0';
    return (int)out.used;
}

int printf(const char *fmt, ...)
{
    char text[GLUE_PRINT_BUFFER];
    va_list args;
    int length;

    va_start(args, fmt);
    length = glue_vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    minix_host_console_write("MINIX: ");
    minix_host_console_write(text);
    return length;
}

void panic(const char *fmt, ...)
{
    char text[GLUE_PRINT_BUFFER];
    va_list args;

    va_start(args, fmt);
    glue_vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    minix_host_panic(text);
}

/****************************************************************
 * Port I/O (the MINIX kernel's sys_devio calls)
 ****************************************************************/

int sys_inb(int port, u32_t *value)
{
    u8_t byte;

    __asm__ volatile ("inb %w1, %0" : "=a"(byte) : "Nd"(port) : "memory");
    *value = byte;
    return OK;
}

int sys_inw(int port, u32_t *value)
{
    u16_t word;

    __asm__ volatile ("inw %w1, %0" : "=a"(word) : "Nd"(port) : "memory");
    *value = word;
    return OK;
}

int sys_inl(int port, u32_t *value)
{
    u32_t dword;

    __asm__ volatile ("inl %w1, %0" : "=a"(dword) : "Nd"(port) : "memory");
    *value = dword;
    return OK;
}

int sys_outb(int port, u32_t value)
{
    __asm__ volatile ("outb %b0, %w1" : : "a"(value), "Nd"(port) : "memory");
    return OK;
}

int sys_outw(int port, u32_t value)
{
    __asm__ volatile ("outw %w0, %w1" : : "a"(value), "Nd"(port) : "memory");
    return OK;
}

int sys_outl(int port, u32_t value)
{
    __asm__ volatile ("outl %0, %w1" : : "a"(value), "Nd"(port) : "memory");
    return OK;
}

int sys_voutb(pvb_pair_t *pvb_pairs, int nr_ports)
{
    int index;

    for (index = 0; index < nr_ports; index++)
        sys_outb(pvb_pairs[index].port, pvb_pairs[index].value);
    return OK;
}

/****************************************************************
 * The PCI library, over the one function the kernel claimed
 ****************************************************************/

static u32_t glue_config_read(int port, unsigned int width)
{
    uint32_t value = 0xffffffff;

    if (!minix_host_config_read(glue_handle, (unsigned int)port, width,
            &value))
        printf("%s: config read 0x%x refused\n", drv.DriverName, port);
    return value;
}

static void glue_config_write(int port, unsigned int width, u32_t value)
{
    if (!minix_host_config_write(glue_handle, (unsigned int)port, width,
            value))
        printf("%s: config write 0x%x = 0x%x refused\n", drv.DriverName,
               port, value);
}

void pci_init(void)
{
}

int pci_first_dev(int *devindp, u16_t *vidp, u16_t *didp)
{
    uint32_t id;

    if (!minix_host_config_read(glue_handle, PCI_VID, 4, &id))
        return 0;
    *devindp = 0;
    *vidp = (u16_t)id;
    *didp = (u16_t)(id >> 16);
    return 1;
}

int pci_next_dev(int *devindp, u16_t *vidp, u16_t *didp)
{
    return 0;
}

void pci_reserve(int devind)
{
    /* The kernel's claim already reserves the function for this driver. */
}

u8_t pci_attr_r8(int devind, int port)
{
    return (u8_t)glue_config_read(port, 1);
}

u16_t pci_attr_r16(int devind, int port)
{
    return (u16_t)glue_config_read(port, 2);
}

u32_t pci_attr_r32(int devind, int port)
{
    return glue_config_read(port, 4);
}

void pci_attr_w8(int devind, int port, u8_t value)
{
    glue_config_write(port, 1, value);
}

void pci_attr_w16(int devind, int port, u16_t value)
{
    glue_config_write(port, 2, value);
}

void pci_attr_w32(int devind, int port, u32_t value)
{
    glue_config_write(port, 4, value);
}

char *pci_dev_name(u16_t vid, u16_t did)
{
    static char es1370[] = "Ensoniq AudioPCI ES1370";

    if (vid == 0x1274 && did == 0x5000)
        return es1370;
    return NULL;
}

/****************************************************************
 * libaudiodriver (audio_fw.c) for the playback channel of minor 0
 ****************************************************************/

static special_file_t *get_special_file(int minor_dev_nr)
{
    int i;

    for (i = 0; i < drv.NrOfSpecialFiles; i++) {
        if (special_file[i].minor_dev_nr == minor_dev_nr)
            return &special_file[i];
    }
    printf("%s: No subdevice specified for minor device %d!\n",
           drv.DriverName, minor_dev_nr);
    return NULL;
}

/* sef_cb_init_fresh(), less the IRQ policy: see the file comment. */
static int glue_init(void)
{
    int i;
    char irq;
    sub_dev_t *sub_dev_ptr;

    if (drv_init() != OK) {
        printf("libaudiodriver: Could not initialize driver\n");
        return EIO;
    }
    for (i = 0; i < drv.NrOfSubDevices; i++) {
        sub_dev_ptr = &sub_dev[i];
        sub_dev_ptr->Opened = FALSE;
        sub_dev_ptr->DmaBusy = FALSE;
        sub_dev_ptr->DmaMode = NO_DMA;
        sub_dev_ptr->DmaReadNext = 0;
        sub_dev_ptr->DmaFillNext = 0;
        sub_dev_ptr->DmaLength = 0;
        sub_dev_ptr->BufReadNext = 0;
        sub_dev_ptr->BufFillNext = 0;
        sub_dev_ptr->RevivePending = FALSE;
        sub_dev_ptr->OutOfData = FALSE;
        sub_dev_ptr->Nr = i;
        sub_dev_ptr->DmaBuf = NULL;
        sub_dev_ptr->ExtraBuf = NULL;
    }
    if (drv_init_hw() != OK) {
        printf("%s: Could not initialize hardware\n", drv.DriverName);
        return EIO;
    }
    if (drv_get_irq(&irq) != OK) {
        printf("%s: init driver couldn't get IRQ", drv.DriverName);
        return EIO;
    }
    glue_irq = (unsigned char)irq;
    return OK;
}

/*
 * init_buffers(): a DMA buffer that does not cross a 64 KiB boundary (the
 * arena is below 16 MiB for an ISA card) and the extra buffers behind it.
 * The buffers of a channel are kept across close and reopen.
 */
static int init_buffers(sub_dev_t *sub_dev_ptr)
{
    unsigned left;

    if (!sub_dev_ptr->DmaBuf) {
        sub_dev_ptr->DmaBuf = minix_host_alloc(glue_handle,
            sub_dev_ptr->DmaSize, 64 * 1024);
        if (!sub_dev_ptr->DmaBuf) {
            printf("%s: failed to allocate dma buffer for a channel\n",
                   drv.DriverName);
            return EIO;
        }
    }
    if (!sub_dev_ptr->ExtraBuf) {
        sub_dev_ptr->ExtraBuf = minix_host_alloc(glue_handle,
            sub_dev_ptr->NrOfExtraBuffers * sub_dev_ptr->DmaSize /
            sub_dev_ptr->NrOfDmaFragments, 16);
        if (!sub_dev_ptr->ExtraBuf) {
            printf("%s failed to allocate extra buffer for a channel\n",
                   drv.DriverName);
            return EIO;
        }
    }
    sub_dev_ptr->DmaPtr = sub_dev_ptr->DmaBuf;
    sub_dev_ptr->DmaPhys = (phys_bytes)(unsigned long)sub_dev_ptr->DmaBuf;
    if ((left = dma_bytes_left(sub_dev_ptr->DmaPhys)) <
            (unsigned int)sub_dev_ptr->DmaSize) {
        /* Aligned to 64 KiB, the buffer never starts this way. */
        printf("%s: DMA buffer crosses a 64K boundary\n", drv.DriverName);
        return EIO;
    }
    drv_set_dma(sub_dev_ptr->DmaPhys, sub_dev_ptr->DmaSize, sub_dev_ptr->Nr);
    return OK;
}

static int open_sub_dev(int sub_dev_nr, int dma_mode)
{
    sub_dev_t *sub_dev_ptr = &sub_dev[sub_dev_nr];

    if (sub_dev_ptr->Opened) {
        printf("%s: Sub device %d is already opened\n",
               drv.DriverName, sub_dev_nr);
        return EBUSY;
    }
    if (sub_dev_ptr->DmaBusy) {
        printf("%s: Sub device %d is still busy\n", drv.DriverName,
               sub_dev_nr);
        return EBUSY;
    }
    sub_dev_ptr->Opened = TRUE;
    sub_dev_ptr->DmaReadNext = 0;
    sub_dev_ptr->DmaFillNext = 0;
    sub_dev_ptr->DmaLength = 0;
    sub_dev_ptr->DmaMode = dma_mode;
    sub_dev_ptr->BufReadNext = 0;
    sub_dev_ptr->BufFillNext = 0;
    sub_dev_ptr->BufLength = 0;
    sub_dev_ptr->RevivePending = FALSE;
    sub_dev_ptr->OutOfData = TRUE;
    if (dma_mode != NO_DMA) {
        if (init_buffers(sub_dev_ptr) != OK)
            return EIO;
    }
    return OK;
}

static int close_sub_dev(int sub_dev_nr)
{
    sub_dev_t *sub_dev_ptr = &sub_dev[sub_dev_nr];

    if (sub_dev_ptr->DmaMode == WRITE_DMA && !sub_dev_ptr->OutOfData) {
        /* Still data in the buffers: it plays out, then the channel stops. */
        sub_dev_ptr->Opened = FALSE;
        return OK;
    }
    if (sub_dev_ptr->DmaMode == NO_DMA) {
        sub_dev_ptr->Opened = FALSE;
        return OK;
    }
    sub_dev_ptr->Opened = FALSE;
    sub_dev_ptr->DmaBusy = FALSE;
    drv_stop(sub_dev_ptr->Nr);
    return OK;
}

/* msg_open() for minor 0. */
static int glue_open(void)
{
    special_file_t *special_file_ptr = get_special_file(0);
    int read_chan, write_chan, io_ctl;

    if (!special_file_ptr)
        return EIO;
    read_chan = special_file_ptr->read_chan;
    write_chan = special_file_ptr->write_chan;
    io_ctl = special_file_ptr->io_ctl;
    if (write_chan == NO_CHANNEL) {
        printf("%s: minor 0 has no write channel\n", drv.DriverName);
        return EIO;
    }
    if (read_chan == write_chan && read_chan != NO_CHANNEL)
        return EIO;
    if (open_sub_dev(write_chan, WRITE_DMA) != OK)
        return EIO;
    if (read_chan != NO_CHANNEL && open_sub_dev(read_chan, READ_DMA) != OK)
        return EIO;
    if (read_chan == io_ctl || write_chan == io_ctl)
        return OK;
    if (io_ctl != NO_CHANNEL && open_sub_dev(io_ctl, NO_DMA) != OK)
        return EIO;
    return OK;
}

/* msg_close() for minor 0. */
static int glue_close(void)
{
    special_file_t *special_file_ptr = get_special_file(0);
    int r = OK;

    if (!special_file_ptr)
        return EIO;
    if (special_file_ptr->write_chan != NO_CHANNEL &&
        close_sub_dev(special_file_ptr->write_chan) != OK)
        r = EIO;
    if (special_file_ptr->read_chan != NO_CHANNEL &&
        close_sub_dev(special_file_ptr->read_chan) != OK)
        r = EIO;
    if (special_file_ptr->read_chan == special_file_ptr->io_ctl ||
        special_file_ptr->write_chan == special_file_ptr->io_ctl)
        return r;
    if (special_file_ptr->io_ctl != NO_CHANNEL &&
        close_sub_dev(special_file_ptr->io_ctl) != OK)
        r = EIO;
    return r;
}

/* msg_ioctl() for minor 0, with the value already in the kernel's hands. */
static int glue_ioctl(enum minix_audio_setting setting, u32_t value)
{
    special_file_t *special_file_ptr = get_special_file(0);
    unsigned long request;
    u32_t io_ctl_buf = value;
    int len = sizeof(io_ctl_buf);
    int chan;

    if (!special_file_ptr)
        return EIO;
    chan = special_file_ptr->io_ctl;
    if (chan == NO_CHANNEL || !sub_dev[chan].Opened) {
        printf("%s: io control impossible - not opened!\n", drv.DriverName);
        return EIO;
    }
    switch (setting) {
    case MINIX_AUDIO_SET_RATE:
        request = DSPIORATE;
        break;
    case MINIX_AUDIO_SET_STEREO:
        request = DSPIOSTEREO;
        break;
    case MINIX_AUDIO_SET_BITS:
        request = DSPIOBITS;
        break;
    case MINIX_AUDIO_SET_SIGN:
        request = DSPIOSIGN;
        break;
    default:
        return EINVAL;
    }
    return drv_io_ctl(request, &io_ctl_buf, &len, chan);
}

static int get_started(sub_dev_t *sub_dev_ptr)
{
    if (drv_start(sub_dev_ptr->Nr, sub_dev_ptr->DmaMode) != OK) {
        printf("%s: Could not start device %d\n",
               drv.DriverName, sub_dev_ptr->Nr);
    }
    sub_dev_ptr->DmaBusy = TRUE;
    sub_dev_ptr->DmaReadNext = 0;
    return OK;
}

/* data_from_user(), with the fragment already in kernel memory. */
static int data_from_user(sub_dev_t *subdev, const void *fragment)
{
    const unsigned char *from = fragment;
    char *to;
    u32_t index;

    if (subdev->DmaLength == subdev->NrOfDmaFragments &&
        subdev->BufLength == subdev->NrOfExtraBuffers)
        return 0;
    if (!fragment)
        return 0;
    if (subdev->DmaLength < subdev->NrOfDmaFragments) {
        to = subdev->DmaPtr + subdev->DmaFillNext * subdev->FragSize;
        for (index = 0; index < subdev->FragSize; index++)
            to[index] = (char)from[index];
        subdev->DmaLength += 1;
        subdev->DmaFillNext =
            (subdev->DmaFillNext + 1) % subdev->NrOfDmaFragments;
    } else {
        to = subdev->ExtraBuf + subdev->BufFillNext * subdev->FragSize;
        for (index = 0; index < subdev->FragSize; index++)
            to[index] = (char)from[index];
        subdev->BufLength += 1;
        subdev->BufFillNext =
            (subdev->BufFillNext + 1) % subdev->NrOfExtraBuffers;
    }
    if (subdev->OutOfData) {
        subdev->OutOfData = FALSE;
        drv_reenable_int(subdev->Nr);
        drv_resume(subdev->Nr);
    }
    return 1;
}

/* msg_write() for minor 0. */
static int glue_write(const void *fragment, bool *accepted)
{
    special_file_t *special_file_ptr = get_special_file(0);
    sub_dev_t *sub_dev_ptr;
    int chan;

    *accepted = false;
    if (!special_file_ptr)
        return EIO;
    chan = special_file_ptr->write_chan;
    if (chan == NO_CHANNEL) {
        printf("%s: No write channel specified!\n", drv.DriverName);
        return EIO;
    }
    sub_dev_ptr = &sub_dev[chan];
    if (!sub_dev_ptr->DmaBusy) {
        if (drv_get_frag_size(&(sub_dev_ptr->FragSize),
                sub_dev_ptr->Nr) != OK) {
            printf("%s; Failed to get fragment size!\n", drv.DriverName);
            return EIO;
        }
    }
    if (sub_dev_ptr->DmaBusy && sub_dev_ptr->DmaMode != WRITE_DMA) {
        printf("Already busy with something else than writing\n");
        return EBUSY;
    }
    if (!data_from_user(sub_dev_ptr, fragment))
        return OK;
    *accepted = true;
    glue_fragments_written++;
    if (!sub_dev_ptr->DmaBusy) {
        get_started(sub_dev_ptr);
        sub_dev_ptr->DmaMode = WRITE_DMA;
    }
    return OK;
}

/* handle_int_write(): one fragment finished playing. */
static void handle_int_write(int sub_dev_nr)
{
    sub_dev_t *sub_dev_ptr = &sub_dev[sub_dev_nr];
    u32_t index;

    glue_interrupts++;
    sub_dev_ptr->DmaReadNext =
        (sub_dev_ptr->DmaReadNext + 1) % sub_dev_ptr->NrOfDmaFragments;
    sub_dev_ptr->DmaLength -= 1;
    if (sub_dev_ptr->BufLength != 0) {
        char *to = sub_dev_ptr->DmaPtr +
            sub_dev_ptr->DmaFillNext * sub_dev_ptr->FragSize;
        const char *from = sub_dev_ptr->ExtraBuf +
            sub_dev_ptr->BufReadNext * sub_dev_ptr->FragSize;

        for (index = 0; index < sub_dev_ptr->FragSize; index++)
            to[index] = from[index];
        sub_dev_ptr->BufReadNext =
            (sub_dev_ptr->BufReadNext + 1) % sub_dev_ptr->NrOfExtraBuffers;
        sub_dev_ptr->DmaFillNext =
            (sub_dev_ptr->DmaFillNext + 1) % sub_dev_ptr->NrOfDmaFragments;
        sub_dev_ptr->BufLength -= 1;
        sub_dev_ptr->DmaLength += 1;
    }
    if (sub_dev_ptr->DmaLength == 0) {
        sub_dev_ptr->OutOfData = TRUE;
        glue_pauses++;
        if (!sub_dev_ptr->Opened) {
            close_sub_dev(sub_dev_ptr->Nr);
            return;
        }
        drv_pause(sub_dev_ptr->Nr);
        return;
    }
    drv_reenable_int(sub_dev_nr);
}

/* msg_hardware(). */
static void glue_service(void)
{
    int i;

    if (drv_int_sum()) {
        for (i = 0; i < drv.NrOfSubDevices; i++) {
            if (drv_int(i) && sub_dev[i].DmaBusy) {
                if (sub_dev[i].DmaMode == WRITE_DMA)
                    handle_int_write(i);
            }
        }
    }
}

static void glue_report(struct minix_audio_call *call)
{
    special_file_t *special_file_ptr = glue_probed ? get_special_file(0)
        : NULL;
    u32_t fragment = 0;

    call->driver_name = glue_probed ? drv.DriverName : NULL;
    call->irq = glue_irq;
    call->interrupts = glue_interrupts;
    call->fragments_written = glue_fragments_written;
    call->pauses = glue_pauses;
    if (special_file_ptr && special_file_ptr->write_chan != NO_CHANNEL) {
        sub_dev_t *sub_dev_ptr = &sub_dev[special_file_ptr->write_chan];

        if (drv_get_frag_size(&fragment, sub_dev_ptr->Nr) != OK)
            fragment = 0;
        call->fragment_bytes = fragment;
        call->dma_fragments_queued = sub_dev_ptr->DmaLength;
        call->extra_fragments_queued = sub_dev_ptr->BufLength;
        call->dma_busy = sub_dev_ptr->DmaBusy;
        call->out_of_data = sub_dev_ptr->OutOfData;
    }
}

void MINIX_AUDIO_DISPATCH(struct minix_audio_call *call)
{
    glue_handle = call->handle;
    call->accepted = false;
    switch (call->kind) {
    case MINIX_AUDIO_CALL_PROBE:
        call->result = glue_probed ? EBUSY : glue_init();
        if (call->result == OK)
            glue_probed = true;
        break;
    case MINIX_AUDIO_CALL_OPEN:
        call->result = glue_probed ? glue_open() : EIO;
        break;
    case MINIX_AUDIO_CALL_CONFIGURE:
        call->result = glue_probed ? glue_ioctl(call->setting, call->value)
            : EIO;
        break;
    case MINIX_AUDIO_CALL_WRITE:
        call->result = glue_probed ? glue_write(call->fragment,
            &call->accepted) : EIO;
        break;
    case MINIX_AUDIO_CALL_SERVICE:
        if (glue_probed)
            glue_service();
        call->result = glue_probed ? OK : EIO;
        break;
    case MINIX_AUDIO_CALL_CLOSE:
        call->result = glue_probed ? glue_close() : EIO;
        break;
    default:
        call->result = EINVAL;
        break;
    }
    glue_report(call);
}
