OpenRFS installer UI port
=========================

The terminal grid, dialog toolkit, and installer state machine in
`src/kernel/orfs_term.c`, `src/kernel/orfs_ui.c`, and
`src/kernel/orfs_install.c` were ported from:

    https://github.com/saudaljuaid/opengatcommandline
    commit 694432044116fb2346c31315367baee98b3503a1

The source and OpenRFS are GPL-3.0-only. The source implementation is a
freestanding UI model: it collects answers and simulates write progress, but
does not partition, format, or copy files to a disk. OpenRFS therefore exposes
it as `install`, an installer configuration preview. The commit action remains
disabled until it can be connected to a bounded, transactional storage backend
with read-back verification and recovery.

The real OpenRFS shell remains in control, so existing filesystem, account,
network, process, and package commands are preserved. Extended frame glyphs
are mapped to ASCII by the kernel adapter because the kernel console font does
not currently contain the source project's custom 0x80-0x8e glyphs.
