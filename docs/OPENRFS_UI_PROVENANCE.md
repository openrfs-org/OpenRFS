# OpenRFS desktop source provenance

## WVRM `starty` desktop

The authenticated `starty` session runs WVRM. Its imported source came from
the owner's desktop repository
at commit `ff921751b4ba20624d3abac0260641df7c7b65aa`, tree
`5bf6e2ee69b385e615e43a1ea3b2b8508e61c874` (GPL-3.0-only).
Its `include/trait/*.h` headers and `src/*.c` plus generated `src/trait_*.h`
assets are copied without namespace changes into `include/trait/` and the
flat `src/kernel/trait_*` build inputs. `src/kernel/minimal_de.c` is the
OpenRFS-owned bridge to the existing framebuffer, input, and session path.
The upstream image, icon, and font source receipts are retained under
`assets/minimal-de-source/`, with the generator scripts under
`tools/minimal-de-source/`, so the embedded tables are auditable even if the
private source repository is unavailable to a downstream builder.
The imported `trait_shell.c` has production-path corrections: pointer motion
without an active drag no longer moves the first window; reopening the one
real console terminal focuses its existing viewport instead of creating an
unbacked second terminal; and the launcher lists only supported applications.
The bridge's host test checks these boundaries before exercising close and
root-menu relaunch.
`opengatcommandline` is the console source; it references this separate
desktop repository rather than containing the desktop implementation itself.

WVRM uses the source's root weave, fvwm-style frames, bitmap
fonts, root menu, and terminal. The terminal viewport is connected to the
real kernel console, so `gfetch` and other shell commands use the ordinary
OpenRFS command path. WVRM Files uses the imported pixel artwork and reads
mounted System and Data directories through the production VFS after login.
The view is a bounded read-only snapshot, and its in-memory edit controls are
disabled. The package and process models are not connected to the production
services and remain unavailable from the supported root menu and launcher.
The preexisting desktop remains in the repository
for its historical boot proof and native-window integration until those
boundaries are migrated and validated. The v2.4.0 tag is unchanged.

`tools/minimal-de-host-test.c` exercises the imported shell through the same
bridge used by `starty`; `tools/capture-openrfs-proof.py` drives login,
`starty`, `gfetch`, and a second shell command through QEMU and verifies exact
guest framebuffer pixels against `assets/openrfs/proof*.png`.

## Historical desktop

The desktop implementation under `include/openrfs/de/` and
`src/kernel/de_*.c` began as the project owner's UI repository at commit
`eb6d32f8bee63025508af992656a7815fa3285c1`, tree
`93319863b82f22b23d8415f04b733cebed6d29f1`. That repository and the imported
code are GPL-3.0-only.

OpenRFS integration moved public headers under `openrfs/de`, renamed product
symbols, connected the desktop to the kernel's screen, input, package, process,
and native-window boundaries, and made normal boot command-line first. Exact
third-party icon and font origins remain documented because changing a product
name does not change the source or license of an input.

The current logo and wallpaper do not come from that historical import. Their
user-supplied source, generated derivatives, prompts, dimensions, and SHA-256
hashes are recorded in `assets/openrfs/*-SOURCE.txt`. The panel mark is rebuilt
from the current logo by `tools/make-brand-mark.py`.

## Generated asset receipts

`tools/verify-ui-assets.py` pins the panel generator, each original panel PNG, every committed generated header, and the current logo/wallpaper receipts. It also reproduces the OpenRFS menu mark.

| OpenRFS path | SHA-256 | Source and license |
| --- | --- | --- |
| `src/kernel/de_openrfs_files_art.h` | `c543b59b875b84fba71c7ba92d30f2a4efe402650a6cac6106500f8752237da3` | `lxde-icon-theme` 0.5.1-2.1 (nuoveXT2), LGPL-3.0-or-later |
| `src/kernel/de_openrfs_panel_art.h` | `b3f31c7c87205ae0b3c71067921b2b7521bbaa8bc612900f43b35366629abf62` | `lxde-icon-theme` 0.5.1-2.1, LGPL-3.0-or-later; `lxpanel-data` 0.11.1-2, GPL-2.0-or-later |
| `src/kernel/de_openrfs_mark.h` | `abd776cbb57552aff9097e37c4c2301f474f56e1bc6ca4ec8898d543ac598ed5` | generated from `assets/openrfs/logo.png` |
| `src/kernel/de_openrfs_mono.h` | `895019b2324a7d1bde44aac4a027ddf4a092c1321cd0ed0240b86117591a041d` | DejaVu Sans Mono raster output |
| `src/kernel/de_openrfs_font_10.h` | `ebc103ad79dbe8c5c834cd0347a25566ccb6308587e04c3e69a12d70b7d1d013` | DejaVu Sans raster output |
| `src/kernel/de_openrfs_font_11.h` | `945a3d0f572c3f57a5b7d100733783a0fcb143591682acae5b3aa35d9e54237a` | DejaVu Sans raster output |
| `src/kernel/de_openrfs_font_13.h` | `96285cd8b16cd9d47e48917460f43819ab738662000e188a69c174dae52b2069` | DejaVu Sans raster output |

The DejaVu copyright and Bitstream Vera notice remain in
`assets/fonts/DejaVu-COPYRIGHT.txt`. The LGPL text remains in
`docs/third-party/LGPL-3.0-or-later.txt`.
