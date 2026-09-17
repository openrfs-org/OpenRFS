/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_DIRENT_H
#define OPENRFS_DIRENT_H

#include <stdint.h>
#include <openrfs/abi.h>
struct dirent { char d_name[13]; uint8_t d_type; };
typedef struct { openrfs_handle_t handle; struct dirent entry; } DIR;
#define DT_REG 1
#define DT_DIR 2
DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
#endif
