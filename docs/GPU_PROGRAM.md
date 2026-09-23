# GPU program decision record (draft, 2026-09-23)

Target: `openrfs-org/OpenRFS` `main` at `f78d25d4ac43f05875bce17d80fbba738428d61e`.
Security PR #91 is open and blocked against this head. This work starts from
`main`, not PR #91. Its entropy boundary must be retested before integration;
display DMA must never acquire entropy or native process capabilities.
The blue terminal/login picture produced by `openrfs-proof` is the modern
Stage 1 visual target identified by the owner. The Files desktop in the
`native-sdl` scenario is a legacy UI regression, not the release screenshot.
PR #91 also edits the Makefile, native process path and several test helpers;
merge interaction requires rebase and regression review after that PR
stabilizes.

Evidence labels below are **observed** (source at the target head or a named
primary reference), **inferred** (the stated source implies a consequence), or
**unknown** (a measurement or review remains necessary). No QEMU result is
evidence for a physical GPU.

## Current production paths and ownership

**Observed.** The loader supplies a physical framebuffer. `framebuffer_initialize`
validates it against paging's device window and stores the mode
(`src/kernel/framebuffer.c:66-136`); `prove_framebuffer` exercises it before the
desktop (`src/kernel/boot_proofs.c:1469-1560`). `screen_initialize` creates a
CPU surface (`src/kernel/screen.c:295-375`). `ui_construct` takes that same
surface as its canvas (`src/kernel/ui.c:558-637`), and `ui_activate` and
`ui_flush` call `surface_present` (`src/kernel/ui.c:650-670,1001-1017`).
Authenticated `starty` selects the minimal desktop before construction
(`src/kernel/shell.c:1489-1529`, `src/kernel/ui.c:643-650`). The bridge gives
the imported Trait-UI a CPU pixel surface and calls its draw functions
(`src/kernel/minimal_de.c:16-55`). `render_desktop` then draws native windows,
overlays and a software cursor and converts every row into the screen surface
(`src/kernel/ui.c:426-538`). The old desktop's glyph code
(`src/kernel/ui_font.c:122-300`) is a historical path, while the modern
desktop uses the imported bitmap font. `surface_present` copies damaged rows
to the loader framebuffer and issues an SFENCE
(`src/kernel/surface.c:450-512`). The CPU produces pixels and performs this
transfer; the loader's display scans out the final bytes. The existing
`display_register`/`display_set_mode` registry
(`include/openrfs/display.h:23-75`, `src/kernel/display.c:75-172`) is for
upstream framebuffer adapters and is not selected by the UI.

**Observed.** Native `syscall_surface_present` copies a bounded ABI request and
rectangles from the process, validates each row, copies user XRGB8888 pixels
into a kernel shadow, then calls `ui_native_window_damage`
(`src/kernel/native_process.c:4897-5002`). The UI marks a redraw and composites
the shadow on the next flush (`src/kernel/ui.c:474-538,1090-1106`). Thus:

`native app → validated present syscall → kernel shadow → render_desktop →`
`CPU screen surface → surface_present → loader framebuffer → display`.

With Stage 1 active, the final arrow becomes `surface_present → validated
CPU copy into pinned VirtIO resource → fenced TRANSFER_TO_HOST_2D and FLUSH →
scanout`. The native window composition is shared between the modern and
historical desktops (`src/kernel/ui.c:426-538`), but only the authenticated
modern capture is Stage 1 visual acceptance.

The process owns its mapped source until the syscall copy; the kernel owns the
shadow and screen surface; the loader framebuffer remains mapped. No native
process receives physical addresses, queue descriptors, or MMIO authority.
Without a GPU the path above remains intact. The boot text screen also uses
the screen surface and `surface_present` when present; headless boot retains
serial output. `boot_plan_start_desktop` gates ordinary desktop activation on
the installed boot ledger (`src/kernel/boot_plan.c:2095-2124`).

## Source and license ledger

All target paths below are at the target commit and carry GPL-3.0-only headers
unless noted. The listed production activity is from callers, not filenames.

| Component | Activity; owner and lifetime | Hostile boundary; present check | Proposed change and risk |
|---|---|---|---|
| `src/kernel/framebuffer.c`, `include/openrfs/framebuffer.h` | Production loader mapping; paging owns window, kernel retains it through boot | Loader geometry; bounds and framebuffer self test | Retain as fallback; actual visibility after VirtIO modeset depends on QEMU VGA layout |
| `src/kernel/display.c`, `include/openrfs/display.h` | Registry for named upstream adapters; no automatic UI handoff | Driver returned mode; `display_set_mode` validation | Do not force VirtIO's non-MMIO scanout into a framebuffer-address ABI |
| `src/kernel/surface.c`, `src/kernel/screen.c` | Production CPU surface, allocated/released via heap and screen lifetime; no concurrent present lock | Damage geometry and source bounds; surface tests | Add private output dispatch; preserve CPU source and WC fallback; prevent waiting under UI/IRQ lock |
| `src/kernel/ui.c`, `src/kernel/ui_font.c`, `src/kernel/pointer.c` | Production CPU desktop, glyphs and software cursor; UI event loop owns state | Window geometry, pointer packets, font asset; UI and pointer self tests | All redraws must reach output dispatch; resize remains bounded by initial screen in Stage 1 |
| `src/kernel/minimal_de.c`, `src/kernel/trait_*`, `include/trait/*` | Active authenticated `starty` desktop; UI event loop owns its fixed surface; imported Trait-UI at provenance commit | UI events and source assets; `minimal-de-host-test` and authenticated QEMU proof | Preserve modern pixel output; Stage 1 scanout sits below the shared screen surface |
| `src/kernel/native_process.c` | Production native ABI and per-process shadow, freed on close/exit | User pointers, handles, rectangles; native QEMU tests | No Stage 1 ABI change; its damaged shadow reaches compositor normally |
| `src/kernel/pci.c`, `src/kernel/pci_resource.c` | Production enumeration, claims, BAR mappings and bus master authority; claim release on shutdown | PCI config and BARs; PCI/resource self tests | Claim only 1AF4:1050 modern VirtIO GPU, validate capabilities, disable bus master before DMA release |
| `src/kernel/dma.c`, `src/kernel/msix.c`, `src/kernel/virtio_net.c` | Production DMA records and network VirtIO transport; explicit ownership and unbind | Device completions; DMA, MSI-X and network tests | New driver may follow patterns, not borrow a different license; no IOMMU isolation is available |
| `docs/UPSTREAM_DRIVERS.md`, `docs/NVIDIA.md` | Scenario/reference only | Synthetic device models | SeaBIOS VGA and NVIDIA probes do not constitute production GPU display |

No Linux/BSD driver source is copied. The VirtIO protocol is implemented from
the OASIS specification; its text is a reference, not a vendored dependency.
Existing vendored SeaBIOS and other license notices remain untouched.
The new C sources and headers carry `GPL-3.0-only` SPDX headers. Host test
dependencies are existing C/Python/Clang tools; no Mesa, Linux DRM,
virglrenderer, or QEMU source is linked into the guest.

## Decision and bounded Stage 1 interface

Use the modern PCI VirtIO GPU device ID `0x1050` with `VIRTIO_F_VERSION_1`, a
single split control queue, one scanout and one fixed XRGB8888 resource. Query
display info; accept scanout zero only if enabled and its rectangle admits the
existing screen size. Reject a mismatched mode rather than resize all UI
surfaces during boot. The resource backing is a page-rounded, zeroed,
contiguous kernel DMA allocation under a fixed page cap. Copy only validated
damage from the CPU screen surface to it, then issue transfer-to-host and
resource-flush. The CPU remains the renderer. Command buffers, queue memory,
and backing pages are never user mapped. The only new interface is a private
kernel call pair and a versioned status snapshot in `virtio_gpu.h`; the native
syscall ABI remains unchanged.

The driver owns its queue, command buffers and resource. Its backing pages are
device visible from ATTACH until DETACH/UNREF completes or reset and bus
master disable complete. The compositor owns the source surface. A synchronous
single-command queue gives one outstanding request and an unambiguous used
descriptor; response type, fixed length and fence ID still require checks.
The device may write only the response buffer and used ring. After timeout or
bad completion, reset the device, disable bus mastering, stop selecting the
GPU output, and quarantine DMA pages if teardown cannot prove the device has
stopped. Never free live DMA on an unconfirmed stop. The loader framebuffer
remains mapped for fallback. If QEMU exposes only one physical output and
VirtIO modeset hides the loader VGA, a visible fallback after a GPU failure is
**unknown** until tested; an explicit desktop display refusal is required in
that configuration. Diagnostic status must reflect the selected output, not
merely a command count. No external telemetry is added.

No UI lock may be held across device wait. The Stage 1 driver uses polling on
the desktop thread with a monotonic deadline; it does not register an IRQ, so
there is no interrupt ownership to tear down. This trades throughput for a
smaller checkable lifecycle. Memory barriers surround descriptor publish and
completion. On x86/QEMU the DMA pages are coherent write-back memory; a
noncoherent physical target requires explicit cache maintenance before reuse.
One device and one scanout are a documented Stage 1 limit, not a public ABI
singleton. Scanout identity across reset is not promised. Cursor composition
is software. Fullscreen means the existing desktop surface; device mode
changes beyond its established dimensions are refused in Stage 1.

## Five stages and gates

1. **Device-backed display now.** Implement the bounded 2D protocol, attach a
   real resource and production `surface_present` handoff, QEMU 2D and absent
   tests, native window pixel capture, fault injection and reset accounting.
   Release wording: “VirtIO GPU scanout/display integrated; pixels CPU
   rendered.” No 3D or physical hardware claim.
2. **One 3D slice next.** Compare VirGL/virglrenderer with rutabaga/gfxstream
   on an installed host. Select one small API and render a texture or triangle
   in a normal window. Pin shader compiler location and version, validate
   command/shader inputs, quota resources per process, fence submissions and
   compare a real host GPU run with llvmpipe control. Keep CPU fallback.
3. **Accelerated compositor and process graphics ABI.** Versioned typed handles,
   per-process quotas, mapped-buffer ownership, explicit fences and cleanup.
   Move measured composition operations individually; compare pixels against
   CPU reference and test stale handles, resets and starvation.
4. **Physical target.** Select exact tested card IDs and firmware after
   comparing Intel, AMD and NVIDIA documentation. Prove BAR, MMU/IOMMU, DMA,
   interrupt, reset and power ownership. Without an IOMMU a malicious bus
   master can reach guest physical memory; enabling hardware needs an explicit
   supported configuration and independent observation.
5. **Breadth and release.** Run appropriate Khronos CTS/Piglit/IGT subsets,
   long-run recovery, performance, power, hotplug and multi-display matrices.
   Publish exact supported API/device scope and fallback behavior.

## Research matrix (planning estimates, not implemented features)

| Path | Host/guest requirements and API | Footprint, firmware, isolation and maintenance | Test and fit |
|---|---|---|---|
| VirtIO GPU 2D | QEMU 8.2.2 `virtio-vga,disable-legacy=on` with the default 2D backend; small guest kernel queue and scanout, no GL/Vulkan | CPU raster, guest DMA pages, no GPU firmware; no guest IOMMU isolation; bounded protocol | Modern login capture at 1024×768 tested; smallest coherent Stage 1 path |
| VirGL | QEMU `virgl=on`, host virglrenderer/Mesa GL, guest VirGL/Mesa or deliberately smaller client API | Large guest userspace and shader/command validation, host GL dependency; physical speed depends on actual host renderer | Stage 2 candidate; GL backend and GPU identity unmeasured |
| rutabaga/gfxstream | QEMU rutabaga backend and host gfxstream stack, guest capset/API client | Larger ABI and host dependency; renderer and GPU use must be measured | Stage 2 comparison; backend availability unknown |
| Nouveau/NVK | Supported NVIDIA generation, firmware, modeset and command streams; guest OS port | Large kernel/userspace integration, DMA/MMU/power burden, GPL/MIT components require per-file audit | Stage 4 research; no actual card tested |
| Intel/AMDGPU | Exact family, firmware and OS-specific driver port | Large driver/firmware and IOMMU requirements | Stage 4 alternatives; hardware availability unknown |

Primary references: [VirtIO v1.2 Committee Specification 01](https://docs.oasis-open.org/virtio/virtio/v1.2/cs01/virtio-v1.2-cs01.html), sections 2.1-2.7, 4.1, 5.7 (published 2022-07-01); [VirtIO v1.3 Committee Specification **Draft** 01](https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html), sections 2, 4.1, 5.7 (2023-10-06; not a final standard); [QEMU VirtIO GPU documentation](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html) and [QEMU 8.2.2 device implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/display/virtio-gpu.c); [Mesa VirGL](https://docs.mesa3d.org/drivers/virgl.html); [Mesa systems](https://docs.mesa3d.org/systems.html); [Linux DRM/KMS](https://docs.kernel.org/gpu/drm-kms.html); [dma-buf](https://docs.kernel.org/driver-api/dma-buf.html); [Nouveau](https://nouveau.freedesktop.org/); [Mesa NVK](https://docs.mesa3d.org/drivers/nvk.html); [Khronos CTS](https://github.com/KhronosGroup/VK-GL-CTS). These are design inputs, not OpenRFS support evidence.

## Exact-head release gate

Required before merge: build/lint, `make verify`, `make qemu-tests`, native,
desktop, filesystem, network, driver and entropy regressions; production
2D/absent/fault QEMU runs with serial traces, output capture and resource
census; parser sanitizers/fuzz; mutation controls; matching benchmark
workloads; signed commits and independent DMA/queue/teardown review. Record
every unavailable gate as a blocker. PR #91 must be reconciled at its then
current base. If any gate remains open, keep a draft PR and do not merge.
