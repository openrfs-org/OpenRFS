# Trait OS desktop source provenance

The desktop implementation under `include/trait/de/` and
`src/kernel/de_*.c` was imported from
[`saudaljuaid/Trait-UI`](https://github.com/saudaljuaid/Trait-UI) at:

- commit `eb6d32f8bee63025508af992656a7815fa3285c1`;
- tree `93319863b82f22b23d8415f04b733cebed6d29f1`.

The imported code is GPL-3.0-only, matching Trait OS. Local integration changes
move public headers under `trait/de`, prefix source filenames with `de_`, avoid
header-guard collisions with the kernel, and identify the visible desktop and
package UI as the Trait OS desktop environment. Debian/LXDE-derived geometry,
themes, icons, and their licenses remain documented because product identity
does not change source provenance.

The canonical onion logo and wallpaper have separate first-party receipts in
`assets/trait/`. Generated icon and font headers are retained from the exact
upstream tree so normal builds do not need a font server, image decoder, or
host toolkit.

## Generated asset receipts

These generated headers are copied byte-for-byte from the pinned Trait-UI
tree. `tools/verify-ui-assets.py` checks every digest during `make verify`.

| Trait OS path | SHA-256 | Source and license |
| --- | --- | --- |
| `src/kernel/de_trait_files_art.h` | `a1a98feeb232d62337facad40025a69789c2235e2a449b55bf7580f73c6dd735` | `lxde-icon-theme` 0.5.1-2.1 (nuoveXT2), LGPL-3.0-or-later |
| `src/kernel/de_trait_panel_art.h` | `b454f2600a3801f615f8d62a326c18910414ee99307d1882be5b53dcd8edf707` | `lxde-icon-theme` 0.5.1-2.1, LGPL-3.0-or-later; `lxpanel-data` 0.11.1-2, GPL-2.0-or-later; first-party onion mark |
| `src/kernel/de_trait_mark.h` | `8ed746e1ec49d5cce4d0955f3cb4a9eb2643544976e2fdcc3c7ae999b1c80660` | first-party Trait mark |
| `src/kernel/de_trait_mono.h` | `811d3f4a19fa04c34549f43f62c0dba295080643c16d3b6fb5ddc46925e4f4c8` | DejaVu Sans Mono raster output |
| `src/kernel/de_trait_font_10.h` | `ef5421084d624dca26eed98e5138608e87e8e792fde9350ebefa9deb9a990f5c` | DejaVu Sans raster output |
| `src/kernel/de_trait_font_11.h` | `9381a71b22afc8ef2e277bc21bc7a0c4b6d487e6dcdaed1fc6ea8cad50c624a7` | DejaVu Sans raster output |
| `src/kernel/de_trait_font_13.h` | `9ca3186990bdbdd17ea8858edf871727b9aea7163dd3759f28a2ac52652a886b` | DejaVu Sans raster output |

The icon sources came from Debian's
`lxde-icon-theme` 0.5.1-2.1 and `lxpanel-data` 0.11.1-2 packages, as
recorded in the pinned Trait-UI tree. The former is LGPL-3.0-or-later; the
latter is GPL-2.0-or-later and is distributed here under GPL-3.0. The DejaVu
copyright and Bitstream Vera permission notice are retained in
`assets/fonts/DejaVu-COPYRIGHT.txt`. The LGPL text is retained in
`docs/third-party/LGPL-3.0-or-later.txt`.
