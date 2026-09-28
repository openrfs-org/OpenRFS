/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * RSD environment for the vendored MINIX 3 audio drivers.
 * <minix/audio_fw.h> includes MINIX's character-driver library header for
 * the IPC types its sub_dev_t records (see <sys/types.h>). The drivers
 * themselves never call the library; RSD's glue takes its place.
 */
#ifndef RSD_MINIX_CHARDRIVER_H
#define RSD_MINIX_CHARDRIVER_H

#include <sys/types.h>

#endif
