/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The SeaBIOS-facing half of the SeaBIOS compatibility layer.
 *
 * This file gives the vendored SeaBIOS drivers the services SeaBIOS's own
 * POST code would: a PCI device list, configuration access, BAR enabling,
 * memory, timers, diagnostics, the process_op dispatcher from block.c and
 * the boot_add_* registration calls from boot.c. It is compiled in the
 * SeaBIOS environment (ports/seabios/include) and reaches the kernel only
 * through include/openrfs/seabios_host.h.
 *
 * Each driver is bound the way SeaBIOS's device_hardware_setup binds it:
 * its own *_setup() function runs and walks the PCI device list, and every
 * medium it finds arrives through boot_add_hd, boot_add_cd or
 * boot_add_floppy. The difference is that the list holds exactly one
 * function - the one the kernel just claimed for that driver - so a driver
 * can never reach a device the framework did not give it.
 */
#include <stdarg.h>

#include <openrfs/seabios_host.h>

#include "biosvar.h"
#include "block.h"
#include "byteorder.h"
#include "config.h"
#include "malloc.h"
#include "output.h"
#include "pci_ids.h"
#include "pci_regs.h"
#include "romfile.h"
#include "stacks.h"
#include "string.h"
#include "util.h"
#include "x86.h"
#include "hw/ahci.h"
#include "hw/ata.h"
#include "hw/blockcmd.h"
#include "hw/esp-scsi.h"
#include "hw/lsi-scsi.h"
#include "hw/megasas.h"
#include "hw/mpt-scsi.h"
#include "hw/nvme.h"
#include "hw/pci.h"
#include "hw/pcidevice.h"
#include "hw/pic.h"
#include "hw/pvscsi.h"
#include "hw/rtc.h"
#include "hw/virtio-blk.h"
#include "hw/virtio-scsi.h"
#include "std/disk.h"

#define GLUE_MAX_PCI 32
#define GLUE_PRINT_BUFFER 256

/****************************************************************
 * State SeaBIOS's POST would own
 ****************************************************************/

struct hlist_head PCIDevices;
int MaxPCIBus;
struct bios_data_area_s openrfs_seabios_bda;
struct floppy_dbt_s diskette_param_table;
u8 *bounce_buf_fl;

struct glue_pci {
    struct pci_device pci;
    void *handle;
    size_t index;
    int claimed;
};

/*
 * Drivers keep pointers to their pci_device for as long as they drive it,
 * so entries are never reused.
 */
static struct glue_pci glue_pci[GLUE_MAX_PCI];
static int glue_pci_used;

struct glue_driver {
    const char *name;
    const char *path;
    int (*match)(const struct seabios_host_pci_info *info);
    void (*setup)(void);
};

/* What the bind in progress has published so far. */
static void *bind_handle;
static const struct glue_driver *bind_driver;
static int bind_published;
static int binding;

/****************************************************************
 * Driver table: each match mirrors the driver's own PCI test
 ****************************************************************/

static int match_ahci(const struct seabios_host_pci_info *info)
{
    /* ahci.c ahci_scan: SATA class, AHCI 1.0 programming interface. */
    return info->class_code == 0x01 && info->subclass == 0x06 &&
        info->prog_if == 0x01;
}

static int match_ata(const struct seabios_host_pci_info *info)
{
    /* ata.c pci_ata_tbl: any IDE-class function. */
    return info->class_code == 0x01 && info->subclass == 0x01;
}

static int match_virtio_blk(const struct seabios_host_pci_info *info)
{
    return info->vendor_id == PCI_VENDOR_ID_REDHAT_QUMRANET &&
        (info->device_id == PCI_DEVICE_ID_VIRTIO_BLK_09 ||
         info->device_id == PCI_DEVICE_ID_VIRTIO_BLK_10);
}

static int match_virtio_scsi(const struct seabios_host_pci_info *info)
{
    return info->vendor_id == PCI_VENDOR_ID_REDHAT_QUMRANET &&
        (info->device_id == PCI_DEVICE_ID_VIRTIO_SCSI_09 ||
         info->device_id == PCI_DEVICE_ID_VIRTIO_SCSI_10);
}

static int match_lsi_scsi(const struct seabios_host_pci_info *info)
{
    return info->vendor_id == PCI_VENDOR_ID_LSI_LOGIC &&
        info->device_id == PCI_DEVICE_ID_LSI_53C895A;
}

static int match_esp_scsi(const struct seabios_host_pci_info *info)
{
    return info->vendor_id == PCI_VENDOR_ID_AMD &&
        info->device_id == PCI_DEVICE_ID_AMD_SCSI;
}

static int match_megasas(const struct seabios_host_pci_info *info)
{
    if (info->vendor_id != PCI_VENDOR_ID_LSI_LOGIC &&
        info->vendor_id != PCI_VENDOR_ID_DELL)
        return 0;
    switch (info->device_id) {
    case PCI_DEVICE_ID_LSI_SAS1064R:
    case PCI_DEVICE_ID_LSI_SAS1078:
    case PCI_DEVICE_ID_LSI_SAS1078DE:
    case PCI_DEVICE_ID_LSI_SAS2108:
    case PCI_DEVICE_ID_LSI_SAS2108E:
    case PCI_DEVICE_ID_LSI_SAS2004:
    case PCI_DEVICE_ID_LSI_SAS2008:
    case PCI_DEVICE_ID_LSI_VERDE_ZCR:
    case PCI_DEVICE_ID_DELL_PERC5:
    case PCI_DEVICE_ID_LSI_SAS2208:
    case PCI_DEVICE_ID_LSI_SAS3108:
        return 1;
    default:
        return 0;
    }
}

static int match_mpt_scsi(const struct seabios_host_pci_info *info)
{
    return info->vendor_id == PCI_VENDOR_ID_LSI_LOGIC &&
        (info->device_id == PCI_DEVICE_ID_LSI_53C1030 ||
         info->device_id == PCI_DEVICE_ID_LSI_SAS1068 ||
         info->device_id == PCI_DEVICE_ID_LSI_SAS1068E);
}

static int match_pvscsi(const struct seabios_host_pci_info *info)
{
    return info->vendor_id == PCI_VENDOR_ID_VMWARE &&
        info->device_id == PCI_DEVICE_ID_VMWARE_PVSCSI;
}

static int match_sdcard(const struct seabios_host_pci_info *info)
{
    /* sdcard.c sdcard_setup: SDHCI class, spec-conforming interface. */
    return info->class_code == 0x08 && info->subclass == 0x05 &&
        info->prog_if < 2;
}

static int match_nvme(const struct seabios_host_pci_info *info)
{
    /* nvme.c nvme_setup: NVMe class, NVM Express 1.0e interface. */
    return info->class_code == 0x01 && info->subclass == 0x08 &&
        info->prog_if == 0x02;
}

static const struct glue_driver glue_drivers[] = {
    { "ahci", "src/hw/ahci.c", match_ahci, ahci_setup },
    { "ata", "src/hw/ata.c", match_ata, ata_setup },
    { "virtio-blk", "src/hw/virtio-blk.c", match_virtio_blk,
      virtio_blk_setup },
    { "virtio-scsi", "src/hw/virtio-scsi.c", match_virtio_scsi,
      virtio_scsi_setup },
    { "lsi-scsi", "src/hw/lsi-scsi.c", match_lsi_scsi, lsi_scsi_setup },
    { "esp-scsi", "src/hw/esp-scsi.c", match_esp_scsi, esp_scsi_setup },
    { "megasas", "src/hw/megasas.c", match_megasas, megasas_setup },
    { "mpt-scsi", "src/hw/mpt-scsi.c", match_mpt_scsi, mpt_scsi_setup },
    { "pvscsi", "src/hw/pvscsi.c", match_pvscsi, pvscsi_setup },
    { "sdcard", "src/hw/sdcard.c", match_sdcard, sdcard_setup },
    { "nvme", "src/hw/nvme.c", match_nvme, nvme_setup },
    /* ISA: bound by a pass of its own, never matched against PCI. */
    { "floppy", "src/hw/floppy.c", NULL, floppy_setup },
};

#define GLUE_FLOPPY_IRQ 6

size_t seabios_glue_driver_count(void)
{
    return ARRAY_SIZE(glue_drivers);
}

const char *seabios_glue_driver_name(size_t index)
{
    return index < ARRAY_SIZE(glue_drivers) ? glue_drivers[index].name : NULL;
}

const char *seabios_glue_driver_path(size_t index)
{
    return index < ARRAY_SIZE(glue_drivers) ? glue_drivers[index].path : NULL;
}

int seabios_glue_match(const struct seabios_host_pci_info *info)
{
    size_t index;

    if (info == NULL || info->header_type != 0)
        return -1;
    for (index = 0; index < ARRAY_SIZE(glue_drivers); index++) {
        if (glue_drivers[index].match && glue_drivers[index].match(info))
            return (int)index;
    }
    return -1;
}

/****************************************************************
 * Diagnostics (output.c)
 ****************************************************************/

static int at_line_start = 1;

static void glue_puts(const char *text)
{
    char line[GLUE_PRINT_BUFFER + 16];
    size_t used = 0;

    /* Prefix each new line so SeaBIOS's messages are attributable. */
    while (*text) {
        if (at_line_start) {
            const char *prefix = "SeaBIOS: ";
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

int openrfs_seabios_vsnprintf(char *buffer, size_t size, const char *fmt,
                              va_list args);

void __dprintf(const char *fmt, ...)
{
    char text[GLUE_PRINT_BUFFER];
    va_list args;

    va_start(args, fmt);
    openrfs_seabios_vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    glue_puts(text);
}

void printf(const char *fmt, ...)
{
    char text[GLUE_PRINT_BUFFER];
    va_list args;

    va_start(args, fmt);
    openrfs_seabios_vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    glue_puts(text);
}

int snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list args;
    int length;

    va_start(args, fmt);
    length = openrfs_seabios_vsnprintf(str, size, fmt, args);
    va_end(args);
    return length;
}

char *znprintf(size_t size, const char *fmt, ...)
{
    char *str = malloc_tmp(size);
    va_list args;

    if (!str) {
        warn_noalloc();
        return NULL;
    }
    va_start(args, fmt);
    openrfs_seabios_vsnprintf(str, size, fmt, args);
    va_end(args);
    return str;
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

void hexdump(const void *d, int len)
{
    const u8 *bytes = d;
    int index;

    for (index = 0; index < len; index++) {
        if (index % 16 == 0)
            dprintf(1, "%s%08x:", index ? "\n" : "", index);
        dprintf(1, " %02x", bytes[index]);
    }
    dprintf(1, "\n");
}

/****************************************************************
 * Memory (malloc.c)
 ****************************************************************/

struct zone_s ZoneLow, ZoneHigh, ZoneFSeg, ZoneTmpLow, ZoneTmpHigh;

void *openrfs_seabios_memalign(u32 align, u32 size)
{
    if (align < MALLOC_MIN_ALIGN)
        align = MALLOC_MIN_ALIGN;
    return seabios_host_alloc(size, align);
}

void *_malloc(struct zone_s *zone, u32 size, u32 align)
{
    (void)zone;
    return openrfs_seabios_memalign(align, size);
}

void free(void *data)
{
    if (data)
        seabios_host_free(data);
}

int create_bounce_buf(void)
{
    if (bounce_buf_fl)
        return 0;
    bounce_buf_fl = memalign_high(CDROM_SECTOR_SIZE, CDROM_SECTOR_SIZE);
    if (!bounce_buf_fl) {
        warn_noalloc();
        return -1;
    }
    return 0;
}

/****************************************************************
 * Threads (stacks.c with CONFIG_THREADS off)
 ****************************************************************/

/*
 * SeaBIOS's yield, in its main thread, briefly enables interrupts so the BIOS
 * interrupt handlers can run; floppy.c depends on that to see IRQ 6. Here the
 * kernel handler records the IRQ and this does what handle_0e does with it,
 * less the 8259 EOI, which the kernel's interrupt dispatch has already sent.
 */
static void service_irqs(void)
{
    u32 pending = seabios_host_poll_irqs();

    if (pending & (1U << GLUE_FLOPPY_IRQ)) {
        u8 frs = GET_BDA(floppy_recalibration_status);
        SET_BDA(floppy_recalibration_status, frs | FRS_IRQ);
    }
}

void yield(void)
{
    cpu_relax();
    service_irqs();
}

void yield_toirq(void)
{
    cpu_relax();
    service_irqs();
}

void run_thread(void (*func)(void*), void *data)
{
    func(data);
}

void wait_threads(void)
{
}

void mutex_lock(struct mutex_s *mutex)
{
    mutex->isLocked = 1;
}

void mutex_unlock(struct mutex_s *mutex)
{
    mutex->isLocked = 0;
}

void start_preempt(void)
{
}

void finish_preempt(void)
{
}

int wait_preempt(void)
{
    return 0;
}

void check_preempt(void)
{
}

int in_post(void)
{
    return binding;
}

/****************************************************************
 * Timers (hw/timer.c): a wrapping microsecond clock
 ****************************************************************/

static u32 now_usec(void)
{
    return (u32)(seabios_host_now_ns() / 1000ULL);
}

u32 timer_calc(u32 msecs)
{
    return now_usec() + msecs * 1000U;
}

u32 timer_calc_usec(u32 usecs)
{
    return now_usec() + usecs;
}

int timer_check(u32 end)
{
    return (s32)(now_usec() - end) > 0;
}

u32 irqtimer_calc(u32 msecs)
{
    return timer_calc(msecs);
}

int irqtimer_check(u32 end)
{
    return timer_check(end);
}

void ndelay(u32 count)
{
    seabios_host_delay_ns(count);
}

void udelay(u32 count)
{
    seabios_host_delay_ns((u64)count * 1000ULL);
}

void mdelay(u32 count)
{
    seabios_host_delay_ns((u64)count * 1000000ULL);
}

void nsleep(u32 count)
{
    ndelay(count);
}

void usleep(u32 count)
{
    udelay(count);
}

void msleep(u32 count)
{
    mdelay(count);
}

/****************************************************************
 * Platform (fw/paravirt.c, hw/rtc.c, hw/pic.c)
 ****************************************************************/

static struct glue_pci *glue_find_bdf(u16 bdf);

static int qemu_detected = -1;

int openrfs_seabios_running_on_qemu(void)
{
    size_t index;

    /*
     * fw/paravirt.c qemu_detect: QEMU's host bridge at 00:00.0 carries the
     * subsystem 1af4:1100. Only configuration reads are involved.
     */
    if (qemu_detected >= 0)
        return qemu_detected;
    qemu_detected = 0;
    for (index = 0; index < seabios_host_pci_count(); index++) {
        struct seabios_host_pci_info info;
        uint32_t subsystem = 0;

        if (!seabios_host_pci_info(index, &info) || info.segment != 0 ||
            info.bus != 0 || info.device != 0 || info.function != 0)
            continue;
        if (seabios_host_function_read(index, 0x2c, 4, &subsystem) &&
            subsystem == 0x11001af4U)
            qemu_detected = 1;
        break;
    }
    return qemu_detected;
}

u8 rtc_read(u8 index)
{
    index |= NMI_DISABLE_BIT;
    outb(index, PORT_CMOS_INDEX);
    return inb(PORT_CMOS_DATA);
}

void enable_hwirq(int hwirq, struct segoff_s func)
{
    /* OpenRFS owns interrupt routing; the drivers here are polled. */
    (void)hwirq;
    (void)func;
}

/****************************************************************
 * Boot order and firmware files (boot.c, romfile.c)
 ****************************************************************/

u8 is_bootprio_strict(void)
{
    return 0;
}

int bootprio_find_pci_device(struct pci_device *pci)
{
    (void)pci;
    return -1;
}

int bootprio_find_mmio_device(void *mmio)
{
    (void)mmio;
    return -1;
}

int bootprio_find_scsi_device(struct pci_device *pci, int target, int lun)
{
    (void)pci; (void)target; (void)lun;
    return -1;
}

int bootprio_find_scsi_mmio_device(void *mmio, int target, int lun)
{
    (void)mmio; (void)target; (void)lun;
    return -1;
}

int bootprio_find_ata_device(struct pci_device *pci, int chanid, int slave)
{
    (void)pci; (void)chanid; (void)slave;
    return -1;
}

int bootprio_find_fdc_device(struct pci_device *pci, int port, int fdid)
{
    (void)pci; (void)port; (void)fdid;
    return -1;
}

int bootprio_find_named_rom(const char *name, int instance)
{
    (void)name; (void)instance;
    return -1;
}

int bootprio_find_usb(struct usbdevice_s *usbdev, int lun)
{
    (void)usbdev; (void)lun;
    return -1;
}

int boot_lchs_find_pci_device(struct pci_device *pci, struct chs_s *chs)
{
    (void)pci; (void)chs;
    return -1;
}

int boot_lchs_find_scsi_device(struct pci_device *pci, int target, int lun,
                               struct chs_s *chs)
{
    (void)pci; (void)target; (void)lun; (void)chs;
    return -1;
}

int boot_lchs_find_ata_device(struct pci_device *pci, int chanid, int slave,
                              struct chs_s *chs)
{
    (void)pci; (void)chanid; (void)slave; (void)chs;
    return -1;
}

struct romfile_s *romfile_findprefix(const char *prefix, struct romfile_s *prev)
{
    (void)prefix; (void)prev;
    return NULL;
}

struct romfile_s *romfile_find(const char *name)
{
    (void)name;
    return NULL;
}

u64 romfile_loadint(const char *name, u64 defval)
{
    (void)name;
    return defval;
}

struct acpi_device *acpi_dsdt_find_string(struct acpi_device *prev,
                                          const char *hid)
{
    (void)prev; (void)hid;
    return NULL;
}

char *acpi_dsdt_name(struct acpi_device *dev)
{
    (void)dev;
    return NULL;
}

int acpi_dsdt_find_mem(struct acpi_device *dev, u64 *min, u64 *max)
{
    (void)dev; (void)min; (void)max;
    return -1;
}

int acpi_dsdt_find_irq(struct acpi_device *dev, u64 *irq)
{
    (void)dev; (void)irq;
    return -1;
}

/****************************************************************
 * PCI configuration access (hw/pci.c)
 ****************************************************************/

static struct glue_pci *glue_find_bdf(u16 bdf)
{
    int index;

    for (index = 0; index < glue_pci_used; index++) {
        if (glue_pci[index].claimed && glue_pci[index].pci.bdf == bdf)
            return &glue_pci[index];
    }
    return NULL;
}

static u32 config_read(u16 bdf, u32 addr, unsigned int width)
{
    struct glue_pci *entry = glue_find_bdf(bdf);
    uint32_t value = 0xffffffffU;

    /* A driver sees only the function it was given; others read as absent. */
    if (entry == NULL ||
        !seabios_host_config_read(entry->handle, addr, width, &value))
        return width == 1 ? 0xff : width == 2 ? 0xffff : 0xffffffffU;
    return value;
}

static void config_write(u16 bdf, u32 addr, unsigned int width, u32 val)
{
    struct glue_pci *entry = glue_find_bdf(bdf);

    if (entry == NULL ||
        !seabios_host_config_write(entry->handle, addr, width, val))
        dprintf(1, "config write %04x:%02x refused\n", bdf, addr);
}

void pci_config_writel(u16 bdf, u32 addr, u32 val)
{
    config_write(bdf, addr, 4, val);
}

void pci_config_writew(u16 bdf, u32 addr, u16 val)
{
    config_write(bdf, addr, 2, val);
}

void pci_config_writeb(u16 bdf, u32 addr, u8 val)
{
    config_write(bdf, addr, 1, val);
}

u32 pci_config_readl(u16 bdf, u32 addr)
{
    return config_read(bdf, addr, 4);
}

u16 pci_config_readw(u16 bdf, u32 addr)
{
    return (u16)config_read(bdf, addr, 2);
}

u8 pci_config_readb(u16 bdf, u32 addr)
{
    return (u8)config_read(bdf, addr, 1);
}

void pci_config_maskw(u16 bdf, u32 addr, u16 off, u16 on)
{
    u16 val = pci_config_readw(bdf, addr);

    val = (val & ~off) | on;
    pci_config_writew(bdf, addr, val);
}

u8 pci_find_capability(u16 bdf, u8 cap_id, u8 cap)
{
    int count;

    if (!(pci_config_readw(bdf, PCI_STATUS) & PCI_STATUS_CAP_LIST))
        return 0;
    if (cap == 0)
        cap = pci_config_readb(bdf, PCI_CAPABILITY_LIST);
    else
        cap = pci_config_readb(bdf, cap + PCI_CAP_LIST_NEXT);
    /* A malformed list could loop; 48 entries fill configuration space. */
    for (count = 0; cap >= 0x40 && count < 48; count++) {
        if (pci_config_readb(bdf, cap + PCI_CAP_LIST_ID) == cap_id)
            return cap;
        cap = pci_config_readb(bdf, cap + PCI_CAP_LIST_NEXT);
    }
    return 0;
}

/****************************************************************
 * PCI devices (hw/pcidevice.c)
 ****************************************************************/

struct pci_device *pci_find_device(u16 vendid, u16 devid)
{
    struct pci_device *pci;

    foreachpci(pci) {
        if (pci->vendor == vendid && pci->device == devid)
            return pci;
    }
    return NULL;
}

struct pci_device *pci_find_class(u16 classid)
{
    struct pci_device *pci;

    foreachpci(pci) {
        if (pci->class == classid)
            return pci;
    }
    return NULL;
}

int pci_init_device(const struct pci_device_id *ids, struct pci_device *pci,
                    void *arg)
{
    while (ids->vendid || ids->class_mask) {
        if ((ids->vendid == PCI_ANY_ID || ids->vendid == pci->vendor) &&
            (ids->devid == PCI_ANY_ID || ids->devid == pci->device) &&
            !((ids->class ^ pci->class) & ids->class_mask)) {
            if (ids->func)
                ids->func(pci, arg);
            return 0;
        }
        ids++;
    }
    return -1;
}

struct pci_device *pci_find_init_device(const struct pci_device_id *ids,
                                        void *arg)
{
    struct pci_device *pci;

    foreachpci(pci) {
        if (pci_init_device(ids, pci, arg) == 0)
            return pci;
    }
    return NULL;
}

static struct glue_pci *glue_entry(struct pci_device *pci)
{
    return container_of(pci, struct glue_pci, pci);
}

static int bar_index(u32 addr)
{
    if (addr < PCI_BASE_ADDRESS_0 || addr > PCI_BASE_ADDRESS_5 ||
        (addr & 3) != 0)
        return -1;
    return (int)((addr - PCI_BASE_ADDRESS_0) / 4);
}

void pci_enable_busmaster(struct pci_device *pci)
{
    if (!seabios_host_enable_bus_master(glue_entry(pci)->handle))
        dprintf(1, "bus mastering refused for %pP\n", pci);
}

u16 pci_enable_iobar(struct pci_device *pci, u32 addr)
{
    struct seabios_host_bar bar;
    int index = bar_index(addr);

    if (index < 0 ||
        !seabios_host_bar(glue_entry(pci)->handle, (unsigned int)index,
                          &bar) ||
        !bar.implemented || !bar.io || bar.base == 0 || bar.base > 0xffff) {
        warn_internalerror();
        return 0;
    }
    if (!seabios_host_enable_io(glue_entry(pci)->handle)) {
        warn_internalerror();
        return 0;
    }
    return (u16)bar.base;
}

void *pci_enable_membar(struct pci_device *pci, u32 addr)
{
    struct seabios_host_bar bar;
    int index = bar_index(addr);

    if (index < 0 ||
        !seabios_host_bar(glue_entry(pci)->handle, (unsigned int)index,
                          &bar) ||
        !bar.implemented || bar.io) {
        warn_internalerror();
        return NULL;
    }
    return seabios_host_map_bar(glue_entry(pci)->handle, (unsigned int)index);
}

/****************************************************************
 * Disk requests (block.c)
 ****************************************************************/

int default_process_op(struct disk_op_s *op)
{
    switch (op->command) {
    case CMD_FORMAT:
    case CMD_RESET:
    case CMD_ISREADY:
    case CMD_VERIFY:
    case CMD_SEEK:
        return DISK_RET_SUCCESS;
    default:
        return DISK_RET_EPARAM;
    }
}

/* block.c's process_op_16 and process_op_32 dispatch, in one table. */
static int dispatch_op(struct disk_op_s *op)
{
    switch (op->drive_fl->type) {
    case DTYPE_FLOPPY:
        return floppy_process_op(op);
    case DTYPE_ATA:
        return ata_process_op(op);
    case DTYPE_ATA_ATAPI:
        return ata_atapi_process_op(op);
    case DTYPE_AHCI:
        return ahci_process_op(op);
    case DTYPE_AHCI_ATAPI:
        return ahci_atapi_process_op(op);
    case DTYPE_VIRTIO_BLK:
        return virtio_blk_process_op(op);
    case DTYPE_VIRTIO_SCSI:
        return virtio_scsi_process_op(op);
    case DTYPE_LSI_SCSI:
        return lsi_scsi_process_op(op);
    case DTYPE_ESP_SCSI:
        return esp_scsi_process_op(op);
    case DTYPE_MEGASAS:
        return megasas_process_op(op);
    case DTYPE_MPT_SCSI:
        return mpt_scsi_process_op(op);
    case DTYPE_PVSCSI:
        return pvscsi_process_op(op);
    case DTYPE_SDCARD:
        return sdcard_process_op(op);
    case DTYPE_NVME:
        return nvme_process_op(op);
    default:
        return DISK_RET_EPARAM;
    }
}

int process_op(struct disk_op_s *op)
{
    int ret, origcount = op->count;

    if (origcount * op->drive_fl->blksize > 64*1024) {
        op->count = 0;
        return DISK_RET_EBOUNDARY;
    }
    ret = dispatch_op(op);
    if (ret && op->count == origcount)
        op->count = 0;
    return ret;
}

/****************************************************************
 * Media registration (boot.c)
 ****************************************************************/

static int optical_capacity(struct drive_s *drive, u64 *blocks, u32 *blksize)
{
    struct cdb_read_capacity cmd;
    struct cdbres_read_capacity capacity;
    struct disk_op_s op;
    u32 size;

    /* The same readiness wait SeaBIOS's CD boot path performs. */
    memset(&op, 0, sizeof(op));
    op.drive_fl = drive;
    if (scsi_is_ready(&op) != 0)
        return 0;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = CDB_CMD_READ_CAPACITY;
    memset(&op, 0, sizeof(op));
    op.drive_fl = drive;
    op.command = CMD_SCSI;
    op.count = 1;
    op.buf_fl = &capacity;
    op.cdbcmd = &cmd;
    op.blocksize = sizeof(capacity);
    if (process_op(&op) != DISK_RET_SUCCESS)
        return 0;
    size = be32_to_cpu(capacity.blksize);
    if (size != CDROM_SECTOR_SIZE)
        return 0;
    *blocks = (u64)be32_to_cpu(capacity.sectors) + 1;
    *blksize = size;
    return 1;
}

static void publish(struct drive_s *drive, const char *desc,
                    enum seabios_host_medium medium)
{
    struct seabios_host_drive host;
    char instance[16];

    if (!binding || bind_driver == NULL || drive == NULL)
        return;
    memset(&host, 0, sizeof(host));
    host.glue_drive = drive;
    host.driver = bind_driver->name;
    host.description = desc ? desc : bind_driver->name;
    host.source_path = bind_driver->path;
    host.medium = medium;
    host.block_size = drive->blksize;
    host.block_count = drive->sectors;
    host.removable = drive->removable != 0;
    if (drive->type == DTYPE_SDCARD)
        host.medium = SEABIOS_HOST_MEDIUM_FLASH;
    if (medium == SEABIOS_HOST_MEDIUM_OPTICAL) {
        u64 blocks = 0;
        u32 blksize = 0;

        host.read_only = 1;
        host.removable = 1;
        if (!optical_capacity(drive, &blocks, &blksize)) {
            dprintf(1, "%s: no readable medium\n", host.description);
            return;
        }
        host.block_count = blocks;
        host.block_size = blksize;
    }
    if (medium == SEABIOS_HOST_MEDIUM_FLOPPY) {
        host.removable = 1;
        host.block_count = (u64)drive->lchs.cylinder * drive->lchs.head *
            drive->lchs.sector;
    }
    if (seabios_host_publish(bind_handle, &host, instance, sizeof(instance)))
        bind_published++;
}

void boot_add_hd(struct drive_s *drive_g, const char *desc, int prio)
{
    (void)prio;
    publish(drive_g, desc, SEABIOS_HOST_MEDIUM_DISK);
}

void boot_add_cd(struct drive_s *drive_g, const char *desc, int prio)
{
    (void)prio;
    publish(drive_g, desc, SEABIOS_HOST_MEDIUM_OPTICAL);
}

void boot_add_floppy(struct drive_s *drive_g, const char *desc, int prio)
{
    (void)prio;
    publish(drive_g, desc, SEABIOS_HOST_MEDIUM_FLOPPY);
}

/****************************************************************
 * Entry from the kernel
 ****************************************************************/

static void bind_pci(struct seabios_call *call)
{
    const struct seabios_host_pci_info *info = call->info;
    struct glue_pci *entry;
    int which = seabios_glue_match(info);
    void *handle;

    call->result = 0;
    if (which < 0 || glue_pci_used >= GLUE_MAX_PCI)
        return;
    handle = seabios_host_claim(call->function_index);
    if (handle == NULL)
        return;
    entry = &glue_pci[glue_pci_used++];
    memset(entry, 0, sizeof(*entry));
    entry->pci.bdf = pci_to_bdf(info->bus, info->device, info->function);
    entry->pci.vendor = info->vendor_id;
    entry->pci.device = info->device_id;
    entry->pci.class = ((u16)info->class_code << 8) | info->subclass;
    entry->pci.prog_if = info->prog_if;
    entry->pci.revision = info->revision;
    entry->pci.header_type = info->header_type;
    entry->handle = handle;
    entry->index = call->function_index;
    entry->claimed = 1;

    /*
     * SeaBIOS's POST enables I/O decoding for every storage function before
     * any driver runs, and ata.c relies on it for the fixed legacy ports of
     * a compatibility-mode IDE controller, which no BAR describes.
     */
    if (match_ata(info))
        (void)seabios_host_enable_io(handle);

    PCIDevices.first = NULL;
    hlist_add_head(&entry->pci.node, &PCIDevices);
    bind_handle = handle;
    bind_driver = &glue_drivers[which];
    bind_published = 0;
    binding = 1;
    bind_driver->setup();
    binding = 0;
    PCIDevices.first = NULL;
    call->result = bind_published;
    bind_handle = NULL;
    bind_driver = NULL;
    if (call->result == 0) {
        /* Nothing attached: give the function back untouched by us. */
        seabios_host_release(handle);
        entry->claimed = 0;
    }
}

static void bind_isa(struct seabios_call *call)
{
    call->result = 0;
    bind_handle = NULL;
    bind_driver = &glue_drivers[ARRAY_SIZE(glue_drivers) - 1];
    bind_published = 0;
    if (!seabios_host_isa_irq_enable(GLUE_FLOPPY_IRQ)) {
        dprintf(1, "floppy: IRQ %d is not available\n", GLUE_FLOPPY_IRQ);
        bind_driver = NULL;
        return;
    }
    binding = 1;
    /* POST resets the 8237 before any driver programs a channel. */
    dma_setup();
    bind_driver->setup();
    binding = 0;
    call->result = bind_published;
    bind_driver = NULL;
    if (call->result == 0)
        seabios_host_isa_irq_disable(GLUE_FLOPPY_IRQ);
}

static void transfer(struct seabios_call *call, int write)
{
    struct drive_s *drive = call->glue_drive;
    struct disk_op_s op;
    u8 *buffer = call->buffer;
    u64 lba = call->lba;
    u32 count = call->count;

    /*
     * An int13 caller never asks a floppy for sectors on two tracks at once,
     * and floppy.c relies on that (the FDC command ends at one track's last
     * sector). Requests from the block layer are split the same way.
     */
    while (count) {
        u32 blocks = count;

        if (drive->type == DTYPE_FLOPPY && drive->lchs.sector) {
            u32 left = drive->lchs.sector - (u32)(lba % drive->lchs.sector);
            if (blocks > left)
                blocks = left;
        }
        memset(&op, 0, sizeof(op));
        op.drive_fl = drive;
        op.command = write ? CMD_WRITE : CMD_READ;
        op.count = (u16)blocks;
        op.lba = lba;
        op.buf_fl = buffer;
        call->result = process_op(&op);
        if (call->result)
            return;
        buffer += blocks * drive->blksize;
        lba += blocks;
        count -= blocks;
    }
}

void seabios_glue_dispatch(void *argument)
{
    struct seabios_call *call = argument;

    switch (call->kind) {
    case SEABIOS_CALL_BIND_PCI:
        bind_pci(call);
        break;
    case SEABIOS_CALL_READ:
        transfer(call, 0);
        break;
    case SEABIOS_CALL_WRITE:
        transfer(call, 1);
        break;
    case SEABIOS_CALL_BIND_ISA:
        bind_isa(call);
        break;
    default:
        call->result = -1;
        break;
    }
}
