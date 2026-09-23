/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Device access for iPXE drivers (include/ipxe/io.h and the x86 I/O API).
 *
 * Addresses are integers or pointers, exactly as iPXE accepts them. Memory
 * accesses are volatile and uncached because every MMIO window the layer
 * hands out comes from an uncached pci_resource BAR mapping. Physical and
 * virtual addresses are equal for every buffer a driver can obtain, because
 * all of them come from the identity-mapped DMA arena.
 */
#ifndef OPENRFS_IPXE_IO_H
#define OPENRFS_IPXE_IO_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned long physaddr_t;

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
#define PAGE_MASK (PAGE_SIZE - 1)

static inline __attribute__((always_inline)) unsigned long
virt_to_phys(volatile const void *address)
{
    return (unsigned long)(uintptr_t)address;
}

static inline __attribute__((always_inline)) void *
phys_to_virt(unsigned long physical)
{
    return (void *)(uintptr_t)physical;
}

static inline __attribute__((always_inline)) unsigned long
virt_to_bus(volatile const void *address)
{
    return virt_to_phys(address);
}

static inline __attribute__((always_inline)) void *
bus_to_virt(unsigned long bus_address)
{
    return phys_to_virt(bus_address);
}

static inline __attribute__((always_inline)) unsigned long
phys_to_bus(unsigned long physical)
{
    return physical;
}

static inline __attribute__((always_inline)) unsigned long
bus_to_phys(unsigned long bus_address)
{
    return bus_address;
}

/* Map a bus address that lies inside one of the bound device's BARs. */
void *ioremap(unsigned long bus_address, size_t length);
void iounmap(volatile const void *io_address);

#define OPENRFS_IPXE_MMIO(type, address) \
    ((volatile type *)(uintptr_t)(address))

#define readb(address) (*OPENRFS_IPXE_MMIO(uint8_t, address))
#define readw(address) (*OPENRFS_IPXE_MMIO(uint16_t, address))
#define readl(address) (*OPENRFS_IPXE_MMIO(uint32_t, address))
#define readq(address) (*OPENRFS_IPXE_MMIO(uint64_t, address))
#define writeb(data, address) \
    (*OPENRFS_IPXE_MMIO(uint8_t, address) = (uint8_t)(data))
#define writew(data, address) \
    (*OPENRFS_IPXE_MMIO(uint16_t, address) = (uint16_t)(data))
#define writel(data, address) \
    (*OPENRFS_IPXE_MMIO(uint32_t, address) = (uint32_t)(data))
#define writeq(data, address) \
    (*OPENRFS_IPXE_MMIO(uint64_t, address) = (uint64_t)(data))

static inline __attribute__((always_inline)) uint8_t openrfs_ipxe_inb(
    uint16_t port)
{
    uint8_t value;

    __asm__ __volatile__ ("inb %w1, %b0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline __attribute__((always_inline)) uint16_t openrfs_ipxe_inw(
    uint16_t port)
{
    uint16_t value;

    __asm__ __volatile__ ("inw %w1, %w0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline __attribute__((always_inline)) uint32_t openrfs_ipxe_inl(
    uint16_t port)
{
    uint32_t value;

    __asm__ __volatile__ ("inl %w1, %k0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline __attribute__((always_inline)) void openrfs_ipxe_outb(
    uint8_t value, uint16_t port)
{
    __asm__ __volatile__ ("outb %b0, %w1" : : "a"(value), "Nd"(port));
}

static inline __attribute__((always_inline)) void openrfs_ipxe_outw(
    uint16_t value, uint16_t port)
{
    __asm__ __volatile__ ("outw %w0, %w1" : : "a"(value), "Nd"(port));
}

static inline __attribute__((always_inline)) void openrfs_ipxe_outl(
    uint32_t value, uint16_t port)
{
    __asm__ __volatile__ ("outl %k0, %w1" : : "a"(value), "Nd"(port));
}

#define OPENRFS_IPXE_PORT(address) ((uint16_t)(uintptr_t)(address))
#define inb(address) openrfs_ipxe_inb(OPENRFS_IPXE_PORT(address))
#define inw(address) openrfs_ipxe_inw(OPENRFS_IPXE_PORT(address))
#define inl(address) openrfs_ipxe_inl(OPENRFS_IPXE_PORT(address))
#define outb(data, address) \
    openrfs_ipxe_outb((uint8_t)(data), OPENRFS_IPXE_PORT(address))
#define outw(data, address) \
    openrfs_ipxe_outw((uint16_t)(data), OPENRFS_IPXE_PORT(address))
#define outl(data, address) \
    openrfs_ipxe_outl((uint32_t)(data), OPENRFS_IPXE_PORT(address))

static inline __attribute__((always_inline)) void openrfs_ipxe_insb(
    uint16_t port, void *data, unsigned int count)
{
    __asm__ __volatile__ ("rep insb" : "+D"(data), "+c"(count)
        : "d"(port) : "memory");
}

static inline __attribute__((always_inline)) void openrfs_ipxe_insw(
    uint16_t port, void *data, unsigned int count)
{
    __asm__ __volatile__ ("rep insw" : "+D"(data), "+c"(count)
        : "d"(port) : "memory");
}

static inline __attribute__((always_inline)) void openrfs_ipxe_insl(
    uint16_t port, void *data, unsigned int count)
{
    __asm__ __volatile__ ("rep insl" : "+D"(data), "+c"(count)
        : "d"(port) : "memory");
}

static inline __attribute__((always_inline)) void openrfs_ipxe_outsb(
    uint16_t port, const void *data, unsigned int count)
{
    __asm__ __volatile__ ("rep outsb" : "+S"(data), "+c"(count)
        : "d"(port) : "memory");
}

static inline __attribute__((always_inline)) void openrfs_ipxe_outsw(
    uint16_t port, const void *data, unsigned int count)
{
    __asm__ __volatile__ ("rep outsw" : "+S"(data), "+c"(count)
        : "d"(port) : "memory");
}

static inline __attribute__((always_inline)) void openrfs_ipxe_outsl(
    uint16_t port, const void *data, unsigned int count)
{
    __asm__ __volatile__ ("rep outsl" : "+S"(data), "+c"(count)
        : "d"(port) : "memory");
}

#define insb(address, data, count) \
    openrfs_ipxe_insb(OPENRFS_IPXE_PORT(address), (data), (count))
#define insw(address, data, count) \
    openrfs_ipxe_insw(OPENRFS_IPXE_PORT(address), (data), (count))
#define insl(address, data, count) \
    openrfs_ipxe_insl(OPENRFS_IPXE_PORT(address), (data), (count))
#define outsb(address, data, count) \
    openrfs_ipxe_outsb(OPENRFS_IPXE_PORT(address), (data), (count))
#define outsw(address, data, count) \
    openrfs_ipxe_outsw(OPENRFS_IPXE_PORT(address), (data), (count))
#define outsl(address, data, count) \
    openrfs_ipxe_outsl(OPENRFS_IPXE_PORT(address), (data), (count))

static inline __attribute__((always_inline)) void iodelay(void)
{
    __asm__ __volatile__ ("outb %%al, $0x80" : : "a"(0));
}

#define inb_p(address) ({ uint8_t __value = inb(address); iodelay(); \
        __value; })
#define inw_p(address) ({ uint16_t __value = inw(address); iodelay(); \
        __value; })
#define inl_p(address) ({ uint32_t __value = inl(address); iodelay(); \
        __value; })
#define outb_p(data, address) do { outb(data, address); iodelay(); } while (0)
#define outw_p(data, address) do { outw(data, address); iodelay(); } while (0)
#define outl_p(data, address) do { outl(data, address); iodelay(); } while (0)

static inline __attribute__((always_inline)) void mb(void)
{
    __asm__ __volatile__ ("mfence" : : : "memory");
}
#define rmb() mb()
#define wmb() mb()

#endif
