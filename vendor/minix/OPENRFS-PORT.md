# OpenRFS MINIX 3 audio driver boundary

The files in this directory are byte-for-byte MINIX 3 sources (see
`UPSTREAM-COMMIT.txt` and `SOURCE-MANIFEST.sha256`). OpenRFS does not patch
them. Each driver is compiled with `ports/minix/audio_glue.c` and partially
linked into its own object whose only global symbol is
`minix_<driver>_dispatch`: both drivers define `drv_init`, `sub_dev`,
`special_file` and `drv`, as every MINIX audio driver does.

## What OpenRFS provides instead of libaudiodriver

A MINIX audio driver implements the `drv_*` interface of
`<minix/audio_fw.h>`; libaudiodriver is the rest of the driver process. The
glue takes its place for the playback channel of minor device 0
(`/dev/audio`) and follows `audio_fw.c` function for function:
`sef_cb_init_fresh()` (`drv_init`, the sub-device table, `drv_init_hw`,
`drv_get_irq`), `open_sub_dev()` and `init_buffers()` (a DMA buffer that
never crosses a 64 KiB boundary, and the extra buffers behind it, then
`drv_set_dma`), `msg_ioctl()` (the DSPIO* requests), `msg_write()` with
`data_from_user()` and `get_started()`, `msg_hardware()` with
`handle_int_write()`, and `close_sub_dev()`. Two things differ, both at the
edge of the process:

* **Writes.** The kernel hands over whole fragments. A write that finds the
  DMA ring and the extra buffers full is refused instead of parked, and the
  kernel services the card before offering the fragment again.
* **Interrupts.** MINIX runs `msg_hardware()` when the IRQ message arrives;
  OpenRFS runs it whenever the kernel services the card (while it waits to
  write, and while it drains). Either way it begins by asking the driver
  (`drv_int_sum()`, `drv_int()`), which reads the card's own interrupt
  status, and acknowledges through `drv_reenable_int()`.

## System library

`ports/minix/include` declares the part of MINIX's system library the
drivers use, with MINIX's names, types and constants: port I/O
(`sys_inb` .. `sys_outl`, `sys_voutb`), which the MINIX kernel performs for
a driver process and which runs directly here; the PCI library
(`pci_first_dev` .. `pci_attr_w16`) over the one function the framework
claimed; `printf` and `panic`; MINIX's negative `_SYSTEM` error codes and
its ioctl encoding.

* **PCI.** The ES1370 driver enables I/O decode and bus mastering itself,
  by writing the command register. That write goes through the framework's
  claim authority, which grants bus mastering with the card's own DMA arena
  as the only memory it may master.
* **ISA.** The Sound Blaster 16 driver is built for base 0x220, IRQ 7 and
  DMA channels 1 and 5, and programs the 8237 itself. Its DMA buffer comes
  from an arena below 16 MiB, aligned so the 16-bit channel never crosses
  a 128 KiB boundary. It binds only when named: finding it means resetting
  a DSP at a fixed port nobody described.

## Evidence and its limits

`tools/run_driver_tests.py` attaches the card in QEMU with a WAV audio
backend. The guest opens the device at 44.1 kHz, 16-bit, stereo (after a
96 kHz open the driver must refuse), plays twelve 32 KiB fragments of a
signal in which one channel's level steps with every fragment, then one
silent fragment, drains, and requires one serviced interrupt per fragment.
The runner then reads QEMU's recording and requires every one of the
98304 signal frames to match what the guest generated: a dropped, repeated
or reordered fragment, a wrong rate or a swapped channel fails. QEMU models
the register interfaces of the real parts; a pass is evidence about those
models, not about physical hardware.
