/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The legacy Etherboot NIC interface (iPXE include/nic.h, GPL-2.0-or-later),
 * restricted to its PCI and ISA personalities. Legacy drivers fill in a
 * struct nic and iPXE's drivers/net/legacy.c - vendored unmodified - adapts
 * them to a net_device. The DRIVER() and ISA_DRIVER() macros produce the
 * same probe/remove wrappers as upstream's for those two buses.
 */
#ifndef OPENRFS_IPXE_NIC_H
#define OPENRFS_IPXE_NIC_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <byteswap.h>
#include <errno.h>
#include <ipxe/isa.h>
#include <ipxe/pci.h>
#include <ipxe/io.h>

typedef enum {
    DISABLE = 0,
    ENABLE,
    FORCE
} irq_action_t;

typedef enum duplex {
    HALF_DUPLEX = 1,
    FULL_DUPLEX
} duplex_t;

struct nic {
    struct nic_operations *nic_op;
    int flags;
    unsigned char *node_addr;
    unsigned char *packet;
    unsigned int packetlen;
    unsigned int ioaddr;
    unsigned char irqno;
    unsigned int mbps;
    duplex_t duplex;
    void *priv_data;
    void *fake_bss;
    size_t fake_bss_len;
};

#define NIC_FAKE_BSS_PTR(type) ((type *)legacy_nic.fake_bss)
#define NIC_FAKE_BSS(type) (*NIC_FAKE_BSS_PTR(type))
extern struct { } no_fake_bss;

struct nic_operations {
    int (*connect)(struct nic *nic);
    int (*poll)(struct nic *nic, int retrieve);
    void (*transmit)(struct nic *nic, const char *destination,
        unsigned int type, unsigned int size, const char *packet);
    void (*irq)(struct nic *nic, irq_action_t action);
};

extern struct nic legacy_nic;

static inline int eth_poll(int retrieve)
{
    struct nic *nic = &legacy_nic;

    return nic->nic_op->poll(nic, retrieve);
}

static inline void eth_transmit(const char *destination, unsigned int type,
    unsigned int size, const void *packet)
{
    struct nic *nic = &legacy_nic;

    nic->nic_op->transmit(nic, destination, type, size, packet);
}

int dummy_connect(struct nic *nic);
void dummy_irq(struct nic *nic, irq_action_t action);
int legacy_probe(void *hwdev,
    void (*set_drvdata)(void *hwdev, void *priv),
    struct device *dev,
    int (*probe)(struct nic *nic, void *hwdev),
    void (*disable)(struct nic *nic, void *hwdev),
    size_t fake_bss_len);
void legacy_remove(void *hwdev,
    void *(*get_drvdata)(void *hwdev),
    void (*disable)(struct nic *nic, void *hwdev));

#define PCI_DRIVER(_name, _ids, _class) \
    static inline int _name ## _pci_legacy_probe(struct pci_device *pci); \
    static inline void _name ## _pci_legacy_remove(struct pci_device *pci); \
    struct pci_driver _name __pci_driver = { \
        .ids = _ids, \
        .id_count = sizeof(_ids) / sizeof(_ids[0]), \
        .probe = _name ## _pci_legacy_probe, \
        .remove = _name ## _pci_legacy_remove, \
    };

static inline void legacy_pci_set_drvdata(void *hwdev, void *priv)
{
    pci_set_drvdata(hwdev, priv);
}

static inline void *legacy_pci_get_drvdata(void *hwdev)
{
    return pci_get_drvdata(hwdev);
}

#define ISA_DRIVER(_name, _probe_addrs, _probe_addr, _vendor_id, _prod_id) \
    static inline int _name ## _isa_legacy_probe(struct isa_device *isa); \
    static inline int _name ## _isa_legacy_probe_at_addr( \
        struct isa_device *isa) { \
        if (!_probe_addr(isa->ioaddr)) \
            return -ENODEV; \
        return _name ## _isa_legacy_probe(isa); \
    } \
    static inline void _name ## _isa_legacy_remove(struct isa_device *isa); \
    static const char _name ## _text[]; \
    struct isa_driver _name __isa_driver = { \
        .name = _name ## _text, \
        .probe_addrs = _probe_addrs, \
        .addr_count = sizeof(_probe_addrs) / sizeof(_probe_addrs[0]), \
        .vendor_id = _vendor_id, \
        .prod_id = _prod_id, \
        .probe = _name ## _isa_legacy_probe_at_addr, \
        .remove = _name ## _isa_legacy_remove, \
    };

static inline void legacy_isa_set_drvdata(void *hwdev, void *priv)
{
    isa_set_drvdata(hwdev, priv);
}

static inline void *legacy_isa_get_drvdata(void *hwdev)
{
    return isa_get_drvdata(hwdev);
}

#define DRIVER(_name_text, _unused2, _unused3, _name, _probe, _disable, \
        _fake_bss) \
    static __attribute__((unused)) const char _name ## _text[] = \
        _name_text; \
    static inline int _name ## _probe(struct nic *nic, void *hwdev) { \
        return _probe(nic, hwdev); \
    } \
    static inline void _name ## _disable(struct nic *nic, void *hwdev) { \
        _disable(nic, hwdev); \
    } \
    static inline int _name ## _pci_legacy_probe(struct pci_device *pci) { \
        return legacy_probe(pci, legacy_pci_set_drvdata, &pci->dev, \
            _name ## _probe, _name ## _disable, sizeof(_fake_bss)); \
    } \
    static inline void _name ## _pci_legacy_remove(struct pci_device *pci) { \
        legacy_remove(pci, legacy_pci_get_drvdata, _name ## _disable); \
    } \
    static inline __attribute__((unused)) int _name ## _isa_legacy_probe( \
        struct isa_device *isa) { \
        return legacy_probe(isa, legacy_isa_set_drvdata, &isa->dev, \
            _name ## _probe, _name ## _disable, sizeof(_fake_bss)); \
    } \
    static inline __attribute__((unused)) void _name ## _isa_legacy_remove( \
        struct isa_device *isa) { \
        legacy_remove(isa, legacy_isa_get_drvdata, _name ## _disable); \
    }

#endif
