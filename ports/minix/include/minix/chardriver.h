/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored MINIX 3 audio drivers.
 * <minix/audio_fw.h> includes MINIX's character-driver library header for
 * the IPC types its sub_dev_t records (see <sys/types.h>). The drivers
 * themselves never call the library; OpenRFS's glue takes its place.
 */
#ifndef OPENRFS_MINIX_CHARDRIVER_H
#define OPENRFS_MINIX_CHARDRIVER_H

#include <sys/types.h>

#endif
