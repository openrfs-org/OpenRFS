# SPDX-License-Identifier: GPL-3.0-only
# SeaBIOS VGA drivers. SeaBIOS builds one VGA BIOS per card type, and so
# does OpenRFS: vendor/seabios/vgasrc is compiled once per card below, each
# build choosing its driver with one SEAVGA_VARIANT_* definition, and each
# linked into its own object whose only global symbol is
# seavga_<card>_dispatch. Every card falls back on the standard VGA code.
SEAVGA_VARIANTS := stdvga bochsvga cirrus ati bochs_display ramfb
SEAVGA_STDVGA_FILES := stdvga stdvgaio stdvgamodes vgafonts
SEAVGA_stdvga_FILES := $(SEAVGA_STDVGA_FILES)
SEAVGA_stdvga_DEFINE := SEAVGA_VARIANT_STDVGA
SEAVGA_bochsvga_FILES := $(SEAVGA_STDVGA_FILES) bochsvga svgamodes
SEAVGA_bochsvga_DEFINE := SEAVGA_VARIANT_BOCHSVGA
SEAVGA_cirrus_FILES := $(SEAVGA_STDVGA_FILES) clext
SEAVGA_cirrus_DEFINE := SEAVGA_VARIANT_CIRRUS
SEAVGA_ati_FILES := $(SEAVGA_STDVGA_FILES) atiext svgamodes
SEAVGA_ati_DEFINE := SEAVGA_VARIANT_ATI
SEAVGA_bochs_display_FILES := $(SEAVGA_STDVGA_FILES) bochsdisplay cbvga \
	svgamodes
SEAVGA_bochs_display_DEFINE := SEAVGA_VARIANT_BOCHSDISPLAY
# ramfb.c is compiled through ports/seavga/lp64/ramfb.c (see there).
SEAVGA_ramfb_FILES := $(SEAVGA_STDVGA_FILES) cbvga svgamodes
SEAVGA_ramfb_LP64_FILES := ramfb
SEAVGA_ramfb_DEFINE := SEAVGA_VARIANT_RAMFB
