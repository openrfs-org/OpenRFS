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
	vendor/ipxe/src/drivers/net/ns8390.c

IPXE_GLUE_SOURCES := \
	ports/ipxe/ipxe_glue.c \
	ports/ipxe/libc.c
