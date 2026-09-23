/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Compile the vendored SeaBIOS UHCI driver with its transfer descriptor laid
 * out as the hardware defines it on an LP64 target.
 *
 * vendor/seabios/src/hw/usb-uhci.c is included unchanged. Its own
 * #include "usb-uhci.h" resolves to the vendored header, whose include guard
 * this file has already defined through the LP64 copy next to it - see that
 * copy for the one field it changes and why. The driver's assignments
 * between that field and pointers are the 32-bit conversions it performs in
 * SeaBIOS; they are exact here because every descriptor and buffer lies in
 * the layer's identity-mapped DMA arena below 4 GiB.
 */
#include "types.h"
#include "usb-uhci.h"
#include "hw/usb-uhci.c"
