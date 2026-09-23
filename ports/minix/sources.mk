# SPDX-License-Identifier: GPL-3.0-only
# MINIX 3 audio drivers. Each driver is compiled with ports/minix/audio_glue.c
# in libaudiodriver's place and linked into its own object whose only global
# symbol is minix_<driver>_dispatch (the drivers share drv_* names).
MINIX_DRIVERS := es1370 sb16
MINIX_es1370_SOURCES := \
	vendor/minix/minix/drivers/audio/es1370/es1370.c \
	vendor/minix/minix/drivers/audio/es1370/ak4531.c \
	vendor/minix/minix/drivers/audio/es1370/pci_helper.c
MINIX_sb16_SOURCES := \
	vendor/minix/minix/drivers/audio/sb16/sb16.c \
	vendor/minix/minix/drivers/audio/sb16/mixer.c
