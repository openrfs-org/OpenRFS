/* SPDX-License-Identifier: GPL-3.0-only */
/* The legacy Etherboot environment (include/etherboot.h). */
#ifndef OPENRFS_IPXE_ETHERBOOT_H
#define OPENRFS_IPXE_ETHERBOOT_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <byteswap.h>
#include <ipxe/timer.h>
#include <ipxe/io.h>
#include <ipxe/if_ether.h>

typedef unsigned long Address;

#ifndef VALID_LINK_TIMEOUT
#define VALID_LINK_TIMEOUT 100
#endif

#endif
