# SPDX-License-Identifier: GPL-3.0-only
# SeaBIOS compatibility layer: the vendored drivers and the glue that gives
# them SeaBIOS's POST environment. Everything here links into one object
# whose only global symbols are listed in ports/seabios/exports.txt.
SEABIOS_VENDOR_SOURCES := \
	vendor/seabios/src/hw/ahci.c \
	vendor/seabios/src/hw/ata.c \
	vendor/seabios/src/hw/blockcmd.c \
	vendor/seabios/src/hw/dma.c \
	vendor/seabios/src/hw/esp-scsi.c \
	vendor/seabios/src/hw/floppy.c \
	vendor/seabios/src/hw/lsi-scsi.c \
	vendor/seabios/src/hw/megasas.c \
	vendor/seabios/src/hw/mpt-scsi.c \
	vendor/seabios/src/hw/nvme.c \
	vendor/seabios/src/hw/pvscsi.c \
	vendor/seabios/src/hw/sdcard.c \
	vendor/seabios/src/hw/usb.c \
	vendor/seabios/src/hw/usb-ehci.c \
	vendor/seabios/src/hw/usb-hid.c \
	vendor/seabios/src/hw/usb-hub.c \
	vendor/seabios/src/hw/usb-msc.c \
	vendor/seabios/src/hw/usb-ohci.c \
	vendor/seabios/src/hw/usb-uas.c \
	vendor/seabios/src/hw/usb-xhci.c \
	vendor/seabios/src/hw/virtio-blk.c \
	vendor/seabios/src/hw/virtio-mmio.c \
	vendor/seabios/src/hw/virtio-pci.c \
	vendor/seabios/src/hw/virtio-ring.c \
	vendor/seabios/src/hw/virtio-scsi.c
# Vendored drivers compiled through a wrapper that supplies an LP64-correct
# copy of one hardware header (see ports/seabios/lp64/).
SEABIOS_LP64_SOURCES := \
	ports/seabios/lp64/usb-uhci.c
SEABIOS_GLUE_SOURCES := \
	ports/seabios/seabios_glue.c \
	ports/seabios/libc.c
SEABIOS_EXPORTS := ports/seabios/exports.txt
