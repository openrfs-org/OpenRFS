# OpenRFS SeaBIOS driver boundary

The files in this directory are byte-for-byte SeaBIOS sources (see
`UPSTREAM-COMMIT.txt` and `SOURCE-MANIFEST.sha256`). OpenRFS does not patch
them. They are compiled for x86-64 in the equivalent of SeaBIOS's 32-bit flat
mode (`MODE16=0`, `MODESEGMENT=0`) with SeaBIOS's own code-generation choices
(`-fno-strict-aliasing`, `-fno-delete-null-pointer-checks`), then partially
linked into one object whose only global symbols are the five
`seabios_glue_*` entry points (`ports/seabios/exports.txt`).

## What OpenRFS provides instead of SeaBIOS's POST

`ports/seabios/include/` supplies the environment headers the drivers include
with SeaBIOS's names; `ports/seabios/seabios_glue.c` implements them and
`src/kernel/seabios_host.c` connects them to the kernel.

* **Binding.** Each driver is bound by running its own `*_setup()` function,
  as SeaBIOS's `device_hardware_setup()` does, with `PCIDevices` holding
  exactly one function: the one the upstream driver framework just claimed
  for it. Configuration reads and writes reach only that function; writes
  that would change decode, bus mastering, BARs or the expansion ROM are
  refused or routed through the claim. `pci_enable_iobar()`,
  `pci_enable_membar()` and `pci_enable_busmaster()` go through the claim.
  A controller on which the driver registers no medium is released.
* **Media.** `boot_add_hd()`, `boot_add_cd()` and `boot_add_floppy()` publish
  the drive to OpenRFS's block layer (`src/kernel/blockdev.c`). CD capacity
  comes from the same TEST UNIT READY / READ CAPACITY sequence SeaBIOS's CD
  boot path uses. Requests reach the drivers through a `process_op()` that
  reproduces `block.c`'s dispatch and its 64 KiB transfer limit.
* **Memory.** Every SeaBIOS allocation zone is one identity-mapped DMA arena
  below 4 GiB that belongs to the devices this layer binds.
* **The stack.** SeaBIOS drivers give devices the addresses of stack
  variables (SCSI CDBs, IDENTIFY and INQUIRY buffers, status bytes, the LSI
  SCRIPTS program itself). In the BIOS the stack is identity-mapped low
  memory; OpenRFS thread stacks are not. Every entry into the layer therefore
  runs on a 64 KiB stack carved from the DMA arena
  (`src/arch/x86_64/stack_call.S`), with interrupts disabled and preemption
  held off, so every address a driver hands a device is an identity-mapped
  address inside memory its claim owns.
* **Threads and time.** The drivers are built with `CONFIG_THREADS` off, a
  configuration SeaBIOS supports: `run_thread()` runs to completion and
  `yield()` relaxes the processor. `timer_calc()`/`timer_check()` use a
  wrapping microsecond clock with SeaBIOS's signed comparison.
* **Interrupts.** Only the floppy driver waits for one. The kernel routes ISA
  IRQ 6 to a handler that records it; `yield()` opens a few-instruction
  window with interrupts enabled - what SeaBIOS's own `yield()` does in its
  main thread - and sets the BDA flag exactly as `handle_0e()` would. The
  BIOS Data Area the drivers use is a private copy; no driver touches
  physical page zero. The 8237 ISA DMA buffer lies below 16 MiB, aligned so
  no transfer crosses a 64 KiB boundary, and block requests are split at
  track boundaries as any int13 caller does.
* **Port I/O barriers.** The environment's `inb`/`outb` family are compiler
  memory barriers. `lsi-scsi.c` polls a status port and then reads status
  and message bytes the controller DMA'd into stack variables whose
  addresses reached the device only as integers. SeaBIOS's whole-program
  build happens not to cache those values; compiled a file at a time, GCC
  did, and the driver saw its own pre-transfer values. The barrier makes
  the port access the synchronisation point the driver assumes.
* **Platform.** `runningOnQEMU()` answers from the QEMU host-bridge
  subsystem ID (1af4:1100), the test SeaBIOS's coreboot build uses, so the
  three drivers SeaBIOS enables only on QEMU (lsi-scsi, esp-scsi, mpt-scsi)
  decline elsewhere. Boot-order and firmware-file lookups miss.

## Evidence and its limits

`tools/run_driver_tests.py` attaches each controller in QEMU with a medium
the runner fills with a position-dependent fixture. The guest must find the
medium through the upstream driver, verify the first blocks, a 160 KiB run
spanning several transfers, and the last block, and on writable media
rewrite a region and read it back with its neighbours intact; the runner
then checks the rewritten units in the image file itself. QEMU models the
register interfaces of the real parts; a pass is evidence about those
models, not about physical hardware.
