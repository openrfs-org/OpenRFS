/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_SEABIOS_HOST_H
#define OPENRFS_SEABIOS_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The boundary between the SeaBIOS compatibility layer and the kernel.
 *
 * The vendored SeaBIOS drivers are compiled in their own environment
 * (ports/seabios/include) and linked as one object whose only global symbols
 * are the seabios_glue_* entry points below; everything else, including the
 * drivers' own malloc, printf and PCI helpers, is local to that object. The
 * two sides share no header except this one, which speaks plain C.
 *
 * SeaBIOS drivers give devices the addresses of their own stack variables -
 * SCSI command blocks, IDENTIFY buffers, status bytes - because in the BIOS
 * the stack is identity-mapped low memory. OpenRFS thread stacks are not.
 * The kernel side therefore runs every glue entry on a stack carved from the
 * layer's DMA arena, with interrupts disabled, so each address a driver
 * hands to a device is an identity-mapped address inside memory that
 * device's claim owns.
 */

struct seabios_host_pci_info {
    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t interrupt_line;
    uint16_t vendor_id;
    uint16_t device_id;
};

struct seabios_host_bar {
    uint64_t base;
    uint64_t size;
    bool implemented;
    bool io;
    bool is_64_bit;
};

enum seabios_host_medium {
    SEABIOS_HOST_MEDIUM_DISK = 0,
    SEABIOS_HOST_MEDIUM_OPTICAL,
    SEABIOS_HOST_MEDIUM_FLOPPY,
    SEABIOS_HOST_MEDIUM_FLASH
};

/* A medium a driver registered through boot_add_hd/cd/floppy. */
struct seabios_host_drive {
    void *glue_drive;
    const char *driver;
    const char *description;
    const char *source_path;
    enum seabios_host_medium medium;
    uint32_t block_size;
    uint64_t block_count;
    bool read_only;
    bool removable;
};

/* One trip into the glue; the kernel runs it on the arena stack. */
enum seabios_call_kind {
    SEABIOS_CALL_BIND_PCI = 0,
    SEABIOS_CALL_BIND_ISA,
    SEABIOS_CALL_READ,
    SEABIOS_CALL_WRITE
};

struct seabios_call {
    enum seabios_call_kind kind;
    size_t function_index;
    const struct seabios_host_pci_info *info;
    void *glue_drive;
    uint64_t lba;
    uint32_t count;
    void *buffer;
    /* Out: media published by a bind, or the SeaBIOS DISK_RET_* code. */
    int result;
};

/* Kernel-side services, implemented in src/kernel/seabios_host.c. */
void *seabios_host_alloc(size_t size, size_t alignment);
void seabios_host_free(void *pointer);
bool seabios_host_arena_contains(const void *pointer, size_t length);
void seabios_host_delay_ns(uint64_t nanoseconds);
uint64_t seabios_host_now_ns(void);
void seabios_host_console_write(const char *text);
_Noreturn void seabios_host_panic(const char *text);

size_t seabios_host_pci_count(void);
bool seabios_host_pci_info(size_t index, struct seabios_host_pci_info *info);
bool seabios_host_driver_enabled(const char *name);
/* True when OpenRFS has its own driver for this class of device. */
bool seabios_host_native_driver_exists(const struct seabios_host_pci_info *info);
void *seabios_host_claim(size_t index);
void seabios_host_release(void *handle);
bool seabios_host_bar(void *handle, unsigned int bar_index,
    struct seabios_host_bar *bar);
void *seabios_host_map_bar(void *handle, unsigned int bar_index);
bool seabios_host_enable_io(void *handle);
bool seabios_host_enable_bus_master(void *handle);
bool seabios_host_config_read(void *handle, unsigned int offset,
    unsigned int width, uint32_t *value);
bool seabios_host_config_write(void *handle, unsigned int offset,
    unsigned int width, uint32_t value);
/* Read-only configuration access to any function, for matching. */
bool seabios_host_function_read(size_t index, unsigned int offset,
    unsigned int width, uint32_t *value);
/*
 * ISA interrupts for drivers that wait on one (floppy.c waits for IRQ 6 the
 * way SeaBIOS's handle_0e reports it). Enabling routes the IRQ to a kernel
 * handler that records it. Polling opens a short window with interrupts
 * enabled - what SeaBIOS's own yield does in its main thread - and returns
 * the IRQs recorded since the last poll. Preemption is held off for the whole
 * glue call, so the window never switches threads on the arena stack.
 */
bool seabios_host_isa_irq_enable(unsigned int irq);
void seabios_host_isa_irq_disable(unsigned int irq);
uint32_t seabios_host_poll_irqs(void);
/*
 * Publish a medium: register it with the block layer and record the
 * framework binding against the claim (NULL for ISA devices). The block
 * device's name ("disk0") is written back.
 */
bool seabios_host_publish(void *handle, const struct seabios_host_drive *drive,
    char *instance, size_t instance_capacity);

/* Glue-side services, implemented in ports/seabios/seabios_glue.c. */
size_t seabios_glue_driver_count(void);
const char *seabios_glue_driver_name(size_t index);
const char *seabios_glue_driver_path(size_t index);
/* Which compiled driver would bind this function, or -1. */
int seabios_glue_match(const struct seabios_host_pci_info *info);
/* Runs on the arena stack with interrupts disabled. */
void seabios_glue_dispatch(void *call);

/* Architecture helper: run function(argument) on another stack. */
void hwdrv_call_on_stack(void (*function)(void *), void *argument,
    void *stack_top);

#endif
