# SPDX-License-Identifier: GPL-3.0-only
# Pinned iPXE sources compiled into the kernel, and the OpenRFS glue that
# gives them their runtime. Every vendored path is byte-for-byte upstream;
# vendor/ipxe/SOURCE-MANIFEST.sha256 records the digests.

IPXE_VENDOR_SOURCES := \
	vendor/ipxe/src/core/iobuf.c \
	vendor/ipxe/src/core/list.c \
	vendor/ipxe/src/net/iobpad.c \
	vendor/ipxe/src/drivers/bitbash/bitbash.c \
	vendor/ipxe/src/drivers/bitbash/spi_bit.c \
	vendor/ipxe/src/drivers/nvs/nvs.c \
	vendor/ipxe/src/drivers/nvs/threewire.c \
	vendor/ipxe/src/drivers/net/mii.c \
	vendor/ipxe/src/drivers/net/legacy.c \
	vendor/ipxe/src/drivers/net/intel.c \
	vendor/ipxe/src/drivers/net/eepro100.c \
	vendor/ipxe/src/drivers/net/realtek.c \
	vendor/ipxe/src/drivers/net/pcnet32.c \
	vendor/ipxe/src/drivers/net/vmxnet3.c \
	vendor/ipxe/src/drivers/net/tulip.c \
	vendor/ipxe/src/drivers/net/ns8390.c \
	vendor/ipxe/src/drivers/net/ne2k_isa.c

IPXE_GLUE_SOURCES := \
	ports/ipxe/ipxe_glue.c \
	ports/ipxe/libc.c

# iPXE's USB stack: the USB core, three host controller drivers, the hub
# driver and two network function drivers, with the scheduler and table
# machinery they rely on. They link, with ports/ipxe/usb_glue.c, into one
# object (see ports/ipxe/usb-layer.ld) whose only global symbols are listed
# in ports/ipxe/usb-exports.txt.
IPXE_USB_VENDOR_SOURCES := \
	vendor/ipxe/src/core/process.c \
	vendor/ipxe/src/core/base16.c \
	vendor/ipxe/src/drivers/bus/usb.c \
	vendor/ipxe/src/drivers/bus/cdc.c \
	vendor/ipxe/src/drivers/usb/xhci.c \
	vendor/ipxe/src/drivers/usb/ehci.c \
	vendor/ipxe/src/drivers/usb/uhci.c \
	vendor/ipxe/src/drivers/usb/usbhub.c \
	vendor/ipxe/src/drivers/usb/usbnet.c \
	vendor/ipxe/src/drivers/net/ecm.c \
	vendor/ipxe/src/drivers/net/acm.c \
	vendor/ipxe/src/net/rndis.c

IPXE_USB_GLUE_SOURCES := \
	ports/ipxe/usb_glue.c
