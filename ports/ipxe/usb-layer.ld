/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Partial link (ld -r) of the iPXE USB layer. iPXE's linker tables are
 * sections named .tbl.<table>.<order>; iPXE's own scripts gather them with
 * KEEP(*(SORT(.tbl.*))) (arch/x86/scripts/pcbios.lds) so that each table's
 * 00 start marker, its entries and its 99 end marker are contiguous and in
 * order. The same rule here puts them in one writable section, which the
 * kernel's linker.ld places with the rest of .data. Every other section
 * keeps its name and is placed by linker.ld as usual.
 */
SECTIONS
{
    .data.ipxe_usb_tables : { KEEP(*(SORT(.tbl.*))) }
}
