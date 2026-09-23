/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Compile the vendored SeaBIOS ramfb driver with its fw_cfg structures laid
 * out as QEMU defines them on an LP64 target.
 *
 * vendor/seabios/vgasrc/ramfb.c is included unchanged (by a path that cannot
 * resolve to this file, which has the same name). SeaBIOS compiles it
 * for i386, where a u64 inside a structure is aligned to four bytes, so its
 * struct QemuRAMFBCfg (one u64 and five u32s) is the 28 bytes of QEMU's
 * etc/ramfb file. On x86-64 the same declaration is padded to 32 bytes;
 * QEMU refuses a fw_cfg DMA write whose length is not the file's, and the
 * adapter keeps scanning out whatever it showed before. Packing to four
 * bytes restores the i386 layout for the structures ramfb.c declares - and
 * only those: every header it includes is included first, outside the
 * packed region, so no shared structure changes shape.
 */
#include "biosvar.h"
#include "byteorder.h"
#include "output.h"
#include "string.h"
#include "vgautil.h"
#include "fw/paravirt.h"
#include "std/pmm.h"

#pragma pack(push, 4)
#include "../vgasrc/ramfb.c"
#pragma pack(pop)

_Static_assert(sizeof(struct QemuRAMFBCfg) == 28,
               "etc/ramfb holds 28 bytes (QEMU hw/display/ramfb.c)");
_Static_assert(sizeof(struct QemuCfgFile) == 64,
               "a fw_cfg directory entry is 64 bytes");
