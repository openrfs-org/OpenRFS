# OpenRFS SeaBIOS driver boundary

The files in this directory are byte-for-byte SeaBIOS sources (see
`UPSTREAM-COMMIT.txt` and `SOURCE-MANIFEST.sha256`). OpenRFS does not patch
them. They are compiled for x86-64 in the equivalent of SeaBIOS's 32-bit flat
mode (`MODE16=0`, `MODESEGMENT=0`) with SeaBIOS's own code-generation choices
(`-fno-strict-aliasing`, `-fno-delete-null-pointer-checks`), then partially
linked into one object whose only global symbols are the six
`seabios_glue_*` entry points (`ports/seabios/exports.txt`). The VGA
drivers are built separately, once per card type (see "VGA drivers").

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
* **USB.** The four controller drivers bind through `usb_setup()`, which
  runs every controller type's setup over the one-function list and sets
  the attach timeout as POST would. Controllers bind in SeaBIOS's order -
  storage first, then xHCI, EHCI, and last UHCI and OHCI - so EHCI can
  route full- and low-speed ports to its companions before they enumerate.
  A controller that attaches nothing was already shut down by its own setup
  code and is released. Keyboard reports reach OpenRFS as the set 1
  scancodes `usb-hid.c` hands to `process_key()`, and mouse reports as the
  three PS/2 bytes it hands to `process_mouse()`; the kernel decodes them
  in its own keyboard queue and pointer decoder (`keyboard_submit_scancode`,
  `pointer_submit_packet`). SeaBIOS polls HID devices from its timer
  interrupt; OpenRFS polls them when the keyboard, pointer or UI queues are
  read, at most once every 4 ms (`hwdrv_poll_input`).
* **One LP64 header.** `struct uhci_td` in `src/hw/usb-uhci.h` declares the
  descriptor's hardware buffer pointer as `void *`, which is four bytes only
  in SeaBIOS's 32-bit builds. On x86-64 it made each descriptor 20 bytes,
  so a transfer's second descriptor started inside the first one's buffer
  field and the controller saw garbage. `ports/seabios/lp64/usb-uhci.c`
  compiles the unchanged `usb-uhci.c` after defining that header's guard
  through `ports/seabios/lp64/usb-uhci.h`, a copy whose only change is
  `u32 buffer`. No other vendored hardware structure holds a pointer; the
  one other structure whose i386 layout matters is ramfb's (below).
* **Platform.** `runningOnQEMU()` answers from the QEMU host-bridge
  subsystem ID (1af4:1100), the test SeaBIOS's coreboot build uses, so the
  three drivers SeaBIOS enables only on QEMU (lsi-scsi, esp-scsi, mpt-scsi)
  decline elsewhere. Boot-order and firmware-file lookups miss.

## TPM interface drivers

`hw/tpm_drivers.c` holds two interface drivers for a TPM at the PC Client
platform's address, 0xFED40000: the TIS/FIFO interface and the Command
Response Buffer. `tpmhw_probe()` tries TIS and then CRB, selects and locks
the one the TPM offers; `tpmhw_transmit()` sends one command at locality 0
and reads the response. The glue calls exactly those two, and reports which
interface was chosen from the interface identifier register both keep at
offset 0x30. SeaBIOS's TCG BIOS (`tcgbios.c`), which measures the boot, is
not compiled: the kernel speaks TPM commands itself.

The drivers reach the registers through the identity map. OpenRFS maps that
range write-back; on hardware the firmware's MTRRs mark the chipset window
uncacheable, which is what the effective type becomes, and QEMU does not
model caching. They bind only when `tpm-tis` or `tpm-crb` is named, and the
binding is recorded under the interface actually found.

## VGA drivers

SeaBIOS builds one VGA BIOS per card type; `vgahw.h` dispatches on Kconfig
constants, so a build holds one card's driver and the standard VGA code it
falls back on. OpenRFS compiles `vgasrc/` the same way, once per card type
(`ports/seavga/sources.mk`: stdvga, bochsvga, cirrus, ati, bochs-display,
ramfb), each selected by one `SEAVGA_VARIANT_*` definition in
`ports/seavga/include/config.h` and partially linked into its own object
whose only global symbol is `seavga_<card>_dispatch`.

* **What runs.** SeaBIOS's `vgainit.c`, `vgabios.c` (INT 10h) and `vbe.c`
  are not compiled. The kernel calls the card driver directly:
  `vgahw_setup()` when it binds the adapter, and `vgahw_list_modes()`,
  `vgahw_find_mode()` and `vgahw_set_mode()` (with `MF_LINEARFB` for VBE
  modes) when it asks for an exact width, height and depth. The pitch
  reported is `vgahw_minimum_linelength()`, the `bytes_per_scanline` that
  `vbe.c` puts in the mode information block. `ports/seavga/seavga_glue.c`
  supplies the globals `vgainit.c` and `vbe.c` own, the `vgabios.c`
  helpers the drivers call (`vga_bpp`, `calc_page_size`) and refuses the
  INT 10h-only paths (`bda_save_restore`, `handle_gfx_op`).
* **Segments.** The drivers address real-mode memory through
  `GET_FARVAR`/`SET_FARVAR` and the `*_far` string routines. Built with
  `OPENRFS_SEABIOS_FAR_SEGMENTS`, these resolve segment:offset at run time:
  segment 0 is flat, segment 0x40 is the build's private BIOS Data Area
  (where the VGA BIOS keeps `VGA_CUSTOM_BDA`), segments A000-BFFF are the
  VGA windows at physical 0xA0000-0xBFFFF, and anything else stops the
  kernel. No other real-mode memory is reachable.
* **PCI.** A build drives the one adapter the kernel claimed, and
  configuration access reaches only it. `atiext.c` sizes its framebuffer
  BAR by writing all ones; the kernel does not let a driver rewrite a BAR
  of a decoding device, so the glue answers that sequence from the claim's
  own probe of the BAR. The kernel maps the adapter's register BARs before
  setup and the BAR holding `VBE_framebuffer` after it. The legacy VGA
  ports have no BAR; a build that uses them binds only if the firmware
  left I/O decode on.
* **ramfb.** `ramfb.c` talks to QEMU's fw_cfg through its DMA interface
  and waits for each transfer without a timeout. The glue binds it only
  after the legacy fw_cfg interface answers with QEMU's signature and
  reports the DMA feature. Its framebuffer comes from a dedicated
  identity-mapped allocation below 4 GiB (`allocate_pmm`), and its
  `struct QemuRAMFBCfg` is compiled with its i386 layout
  (`ports/seavga/lp64/ramfb.c`): padded to 32 bytes on x86-64, the fw_cfg
  write is refused by QEMU and the adapter keeps showing the old picture.
* **When they bind.** Only when named on the command line
  (`openrfs.drivers=bochsvga`), never under `auto`: the primary adapter is
  the one the loader set a mode on and the kernel's screen console draws
  to. The display test releases that console and the VGA text mirror
  before it sets a mode.

## Evidence and its limits

`tools/run_driver_tests.py` attaches each controller in QEMU with a medium
the runner fills with a position-dependent fixture. The guest must find the
medium through the upstream driver, verify the first blocks, a 160 KiB run
spanning several transfers, and the last block, and on writable media
rewrite a region and read it back with its neighbours intact; the runner
then checks the rewritten units in the image file itself. QEMU models the
register interfaces of the real parts; a pass is evidence about those
models, not about physical hardware.

TPM scenarios attach QEMU's `tpm-tis` or `tpm-crb` model backed by swtpm
(a TPM 2.0 emulator built on the TCG reference code). The guest reads the
family and manufacturer properties, draws random bytes twice, requires an
undefined command to be refused with `TPM_RC_COMMAND_CODE`, then resets
PCR 16, extends it with a fixed digest and reads it back; the runner
computes SHA-256 over the reset value and that digest itself and requires
the guest's reading to equal it.

Display scenarios set a mode through the card driver, draw a four-colour
quadrant pattern with the reported pitch, read every pixel back, and then
the runner captures what QEMU's display model scans out (QMP `screendump`)
and checks the colours on a grid and on both sides of every quadrant edge,
where a wrong pitch would shear the picture. The standard VGA and Cirrus
scenarios attach the adapter without an option ROM, so the driver brings
the card up from reset: OpenRFS cannot boot on a Cirrus card with its VGA
BIOS (it offers no 32-bit mode, and OpenRFS refuses other Multiboot2
framebuffers), and a standard VGA left in a Bochs VBE mode has its CRTC
programming overridden by QEMU's VGA core, which the standard VGA build -
written for plain VGA hardware - has no reason to know about.
