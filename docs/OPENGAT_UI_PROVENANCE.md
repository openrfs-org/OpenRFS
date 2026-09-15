# OpenGAT desktop source provenance

The desktop implementation under `include/opengat/de/` and
`src/kernel/de_*.c` began as the project owner's UI repository at commit
`eb6d32f8bee63025508af992656a7815fa3285c1`, tree
`93319863b82f22b23d8415f04b733cebed6d29f1`. That repository and the imported
code are GPL-3.0-only.

OpenGAT integration moved public headers under `opengat/de`, renamed product
symbols, connected the desktop to the kernel's screen, input, package, process,
and native-window boundaries, and made normal boot command-line first. Exact
third-party icon and font origins remain documented because changing a product
name does not change the source or license of an input.

The current logo and wallpaper do not come from that historical import. Their
user-supplied source, generated derivatives, prompts, dimensions, and SHA-256
hashes are recorded in `assets/opengat/*-SOURCE.txt`. The panel mark is rebuilt
from the current logo by `tools/make-brand-mark.py`.

## Generated asset receipts

`tools/verify-ui-assets.py` pins the panel generator, each original panel PNG, every committed generated header, and the current logo/wallpaper receipts. It also reproduces the OpenGAT menu mark.

| OpenGAT path | SHA-256 | Source and license |
| --- | --- | --- |
| `src/kernel/de_opengat_files_art.h` | `c543b59b875b84fba71c7ba92d30f2a4efe402650a6cac6106500f8752237da3` | `lxde-icon-theme` 0.5.1-2.1 (nuoveXT2), LGPL-3.0-or-later |
| `src/kernel/de_opengat_panel_art.h` | `b3f31c7c87205ae0b3c71067921b2b7521bbaa8bc612900f43b35366629abf62` | `lxde-icon-theme` 0.5.1-2.1, LGPL-3.0-or-later; `lxpanel-data` 0.11.1-2, GPL-2.0-or-later |
| `src/kernel/de_opengat_mark.h` | `abd776cbb57552aff9097e37c4c2301f474f56e1bc6ca4ec8898d543ac598ed5` | generated from `assets/opengat/logo.png` |
| `src/kernel/de_opengat_mono.h` | `895019b2324a7d1bde44aac4a027ddf4a092c1321cd0ed0240b86117591a041d` | DejaVu Sans Mono raster output |
| `src/kernel/de_opengat_font_10.h` | `ebc103ad79dbe8c5c834cd0347a25566ccb6308587e04c3e69a12d70b7d1d013` | DejaVu Sans raster output |
| `src/kernel/de_opengat_font_11.h` | `945a3d0f572c3f57a5b7d100733783a0fcb143591682acae5b3aa35d9e54237a` | DejaVu Sans raster output |
| `src/kernel/de_opengat_font_13.h` | `96285cd8b16cd9d47e48917460f43819ab738662000e188a69c174dae52b2069` | DejaVu Sans raster output |

The DejaVu copyright and Bitstream Vera notice remain in
`assets/fonts/DejaVu-COPYRIGHT.txt`. The LGPL text remains in
`docs/third-party/LGPL-3.0-or-later.txt`.
