/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: the subset of
 * SeaBIOS's util.h the vendored drivers call - boot registration, timers,
 * and the entry points of drivers that have no header of their own.
 */
#ifndef OPENRFS_SEABIOS_UTIL_H
#define OPENRFS_SEABIOS_UTIL_H

#include "types.h"

struct drive_s;
struct disk_op_s;
struct pci_device;
struct chs_s;
struct usbdevice_s;

/* boot.c: registration of detected media. */
void boot_add_floppy(struct drive_s *drive_g, const char *desc, int prio);
void boot_add_hd(struct drive_s *drive_g, const char *desc, int prio);
void boot_add_cd(struct drive_s *drive_g, const char *desc, int prio);
u8 is_bootprio_strict(void);
int bootprio_find_pci_device(struct pci_device *pci);
int bootprio_find_mmio_device(void *mmio);
int bootprio_find_scsi_device(struct pci_device *pci, int target, int lun);
int bootprio_find_scsi_mmio_device(void *mmio, int target, int lun);
int bootprio_find_ata_device(struct pci_device *pci, int chanid, int slave);
int bootprio_find_fdc_device(struct pci_device *pci, int port, int fdid);
int bootprio_find_named_rom(const char *name, int instance);
int bootprio_find_usb(struct usbdevice_s *usbdev, int lun);
int boot_lchs_find_pci_device(struct pci_device *pci, struct chs_s *chs);
int boot_lchs_find_scsi_device(struct pci_device *pci, int target, int lun,
                               struct chs_s *chs);
int boot_lchs_find_ata_device(struct pci_device *pci, int chanid, int slave,
                              struct chs_s *chs);

/* fw/dsdt_parser.c: OpenRFS lends the drivers no DSDT, so nothing matches. */
struct acpi_device;
struct acpi_device *acpi_dsdt_find_string(struct acpi_device *prev,
                                          const char *hid);
char *acpi_dsdt_name(struct acpi_device *dev);
int acpi_dsdt_find_mem(struct acpi_device *dev, u64 *min, u64 *max);
int acpi_dsdt_find_irq(struct acpi_device *dev, u64 *irq);
struct acpi_device *acpi_dsdt_find_eisaid(struct acpi_device *prev,
                                          u16 eisaid);

/* misc.c: the diskette parameter table floppy_setup copies into place. */
struct floppy_dbt_s;
extern struct floppy_dbt_s diskette_param_table;

/* cdrom.c */
int cdemu_process_op(struct disk_op_s *op);

/* hw/dma.c */
int dma_floppy(u32 addr, int count, int isWrite);
void dma_setup(void);

/* hw/floppy.c */
void floppy_setup(void);
struct drive_s *init_floppy(int floppyid, int ftype);
int find_floppy_type(u32 size);
int floppy_process_op(struct disk_op_s *op);
void floppy_tick(void);

/* hw/sdcard.c */
int sdcard_process_op(struct disk_op_s *op);
void sdcard_setup(void);

/* hw/timer.c: a microsecond clock that wraps, compared with signed deltas. */
u32 timer_calc(u32 msecs);
u32 timer_calc_usec(u32 usecs);
int timer_check(u32 end);
void ndelay(u32 count);
void udelay(u32 count);
void mdelay(u32 count);
void nsleep(u32 count);
void usleep(u32 count);
void msleep(u32 count);
u32 irqtimer_calc(u32 msecs);
int irqtimer_check(u32 end);
/* SeaBIOS's "ticks" are its 18.2 Hz timer interrupts (PIT period 65536). */
u32 ticks_to_ms(u32 ticks);
u32 ticks_from_ms(u32 ms);

/* kbd.c and mouse.c: where USB HID reports are delivered. */
void process_key(u8 key);
void process_mouse(u8 data);

/* post.c */
int in_post(void);

#endif
