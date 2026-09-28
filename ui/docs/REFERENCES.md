# What this is copying, and where it was read

RSD — **Root Software Distribution**, release 2.5. The name, mark,
wallpaper, and release labels are consistent across the desktop,
command line, and installer.

Everything below was read off a primary source, not remembered. Where a
number appears in the code it should appear here too, with the file it
came from.

The rule this repo runs on: **copy the idiom, write our own words and
draw none of the artwork ourselves.** Where somebody else's pixels are
on screen, they are there byte for byte with a licence and a credit —
see `assets/icons/SOURCE.txt`. Where somebody else's *brand* would be,
there is RSD instead.

---

## The desktop — KDE 4 / Oxygen, around 2010

Read from `kstyle/oxygenstylehelper.cpp` in KDE's own Oxygen style
(LGPL). The things worth taking are the gradient constructions, because
they are what makes the surface look lit rather than tinted.

**The window background** is a vertical gradient over the whole window,
not a flat fill — `renderWindowBackground()` with a `y_shift`, so a
widget's gradient lines up with the window it sits in rather than
restarting at its own top edge. That single decision is most of why
Oxygen looks like one sheet of glass instead of a pile of boxes.

**Menus split.** The gradient runs from the top down to

```
splitY = min(300, (3 * height) / 4)
```

and below that line it is a flat `backgroundBottomColor`. So a tall menu
is lit at the top and settles to a solid colour — it does not keep
getting darker forever.

**Buttons (the "slab").** Two gradients, chosen by state:

```
raised:  from y = top - 0.2*h  to  y = bottom + 0.4*h
         stop 0.0 = calcLightColor(color)
         stop 0.6 = color
sunken:  from y = top  to  y = bottom + h
         stop 0.0 = color
         stop 1.0 = calcLightColor(color)
```

Both gradients are taller than the button they fill. The visible part is
a slice out of the middle of a bigger curve, which is why the highlight
never reads as a stripe.

Corner radii in that file sit at **3.5, 2.5 and 4.0** px, and shadows
scale by `size / 18.0`.

Oxygen's stated aim was a break from the cartoon look of KDE 3 toward a
**photorealistic** one, and it implements freedesktop.org's Icon Naming
and Icon Theme specifications — which is why our vendored names
(`utilities-terminal`, `folder`, `go-up`) are the names they are.

### The window background, read properly the second time

The first pass took the shape of `renderWindowBackground()` and guessed
its colours, scaling every channel by a factor. That is wrong in a way
you can see: `0xED * 1.12` is 265, which clips to 255, so the top of
every window came out pure white and the gradient had nothing left to run
from. The real construction, from `liboxygen/oxygenhelper.cpp`:

```
verticalGradient(color, height, offset):
    gradient.setColorAt(0.0, backgroundTopColor(color));
    gradient.setColorAt(0.5, color);              // the base itself
    gradient.setColorAt(1.0, backgroundBottomColor(color));

renderWindowBackground():
    splitY = min(300, (3 * windowRect.height()) / 4)
    ...then a flat backgroundBottomColor below it
    radialW = min(600, windowRect.width())
    radialRect = ((w - radialW) / 2, y, radialW, 64 + yShift)

radialGradient(color, width, height):
    QRadialGradient(64, height - 64, 64)   // in a 128-wide space
    alpha 255 at 0, 101 at 0.5, 37 at 0.75, 0 at 1
```

Two things that were missing: the **middle stop is the window's own
colour**, so the top half reads lit and the bottom half shaded rather
than the whole thing reading tilted; and there is a **pool of light on
the top edge** — an elliptical highlight 600 wide and 64 tall, centred
on the middle of the window's top — laid over the vertical gradient.
Without it a window is evenly lit, which nothing in a room ever is.

And the derived colours are luma arithmetic in HCY, not channel scaling.
From `kcolorscheme.cpp`:

```
y = luma(color); yi = 1 - y;
lightAmount = (0.05 + y * 0.55) * (0.25 + contrast * 0.75);
darkAmount  = (-y) * (0.55 + contrast * 0.35);
LightShade    -> shade(color, lightAmount)
MidShade      -> shade(color, (0.35 + 0.15 * y) * darkAmount)
ShadowShade   -> darken(shade(color, darkAmount), 0.5 + 0.3 * y)
```

with the default `contrast` 0.7 (`contrastF()` reads `KDE/contrast`,
default 7, and returns a tenth of it), and from `oxygenhelper.cpp`
`_bgcontrast = min(1.0, 0.9 * _contrast / 0.7)` = 0.9. `KHCY`'s luma is
`0.2126 r + 0.7152 g + 0.0722 b` on gamma-expanded channels
(`kcolorspaces.cpp`). `tools/make-glass.py` implements all of it and
writes `desktop/src/rsd_glass.h`.

### The shadow, which is a blue glow

The most recognisable single thing about a KDE 4 desktop, and it was
missing entirely. From `liboxygen/oxygenactiveshadowconfigdata.kcfg`:

```
ShadowSize     40        InnerColor     112,239,255
VerticalOffset 0         OuterColor     84,167,240
                         UseOuterColor  true
```

— the focused window is haloed in **cyan over blue**, not shadowed in
grey. The inactive one (`oxygeninactiveshadowconfigdata.kcfg`) is black
at the same size with `VerticalOffset 0.2`, so it hangs slightly below
the window.

The falloff, from `liboxygen/oxygenshadowcache.cpp`:

```
static const qreal fixedSize = 25.5;   const int overlap = 4;
Gaussian(a, w):  a * exp(-(x / w)^2 - 0.05)
Parabolic(a, w): a * (1 - (x / w)^2)

active   inner  radius min(S, (S + fixedSize) / 2)  Gaussian(0.85, 0.17)
         outer  radius S                            Gaussian(0.46, 0.34)
idle     inner  radius min(S, fixedSize)            Parabolic(1.0, 0.22)
         mid    radius min(S, (S + 2*fixedSize)/3)  Gaussian(0.54, 0.21)
         outer  radius S                            Gaussian(0.155, 0.445)
```

`overlap` matters: the decoration sits four pixels over its own shadow
tile, so the innermost four pixels of every one of those curves are never
seen. Read from zero instead and the inactive shadow starts at the
parabolic's full amplitude, which draws a hard black line round the
window. `tools/make-shadow.py` writes `desktop/src/rsd_shadow.h`.

### The title bar

From `kdecoration/oxygendecoration.cpp` `renderTitleText()`, the caption
is painted **twice**:

```
painter->setPen(contrast); painter->translate(0, 1);  drawText(...);
painter->translate(0, -1); painter->setPen(color);    drawText(...);
```

One pixel, downwards, whatever the font size. It is not a drop shadow —
it is a pixel of lift, and it is why a KDE 4 caption reads as heavy
without being bold.

The buttons are round. From `kdecoration/oxygendecohelper.cpp`
`windecoButton()`: inside an 18-unit box, a circle 12.33 across with its
top at 1.665, filled with a vertical gradient from `calcLightColor` to
`calcDarkColor` and rung with a 0.7-unit outline, over a shadow drawn
first. The marks, from `oxygenbutton.cpp` `drawIcon()`, are chevrons in a
21-unit space:

```
Minimize  polyline (7.5, 9.5) (10.5, 12.5) (13.5, 9.5)
Maximize  polyline (7.5, 11.5) (10.5, 8.5) (13.5, 11.5)
  maximised: polygon (7.5,10.5) (10.5,7.5) (13.5,10.5) (10.5,13.5)
Close     line (7.5, 7.5) (13.5, 13.5) and (13.5, 7.5) (7.5, 13.5)
```

and each is drawn twice as well — once in `calcLightColor` translated
`(0, 1.5)`, then in the foreground colour — so the mark looks cut into
the orb rather than drawn on it.

Borders: `Decoration::borderSize()` gives `BorderNormal = smallSpacing *
2`, which is 4 pixels. The old frame had 7, which is fvwm's.

### The type — the console's

The console and the installer are the **VGA character ROM** at 8x16, out of `share/vt/fonts/vgarom-8x16.hex` in FreeBSD's own
tree — the font its `vt(4)` console draws in, converted (per the commit
that added it) from FreeBSD's `syscons(4)` CP437 fonts.

It was DejaVu Sans Mono at 15px thresholded into a 9x18 cell. An outline
face is drawn for a rasteriser that can put grey on the edge of a stem;
without the grey the stems come out two pixels wide in one letter and
one in the next. A console font has to be *cut* at one bit per pixel,
and the one a PC console actually uses is the character generator in the
VGA ROM.

CP437's box characters come with it, which is what the frames are drawn
from now — they tile exactly because that is what they were cut for.

There is no bold. A VGA text console has one character generator and the
intensity bit changes the colour.

### The type — the desktop's

KDE 4's default interface font was `Sans Serif 9`, resolved by fontconfig
to **DejaVu Sans** on every distribution that shipped it; 9 points at 96
dpi is a 12-pixel em. Konsole's default was DejaVu Sans Mono at the same
size. The desktop's faces were Misc-Fixed bitmaps — right for an X
session of 1995, and the loudest thing on the screen saying 1995.
`tools/make-font.py` rasterises both, antialiased;
`assets/fonts/SOURCE.txt` records the files and the Bitstream Vera
licence they carry.

## The terminal — Konsole

Colours are Konsole's own `Linux.colorscheme`, quoted exactly:

| | normal | intense | faint |
|---|---|---|---|
| black | 0,0,0 | 104,104,104 | 24,24,24 |
| red | 178,24,24 | 255,84,84 | 101,0,0 |
| green | 24,178,24 | 84,255,84 | 0,101,0 |
| yellow | 178,104,24 | 255,255,84 | 101,94,0 |
| blue | 24,24,178 | 84,84,255 | 0,0,101 |
| magenta | 178,24,178 | 255,84,255 | 101,0,101 |
| cyan | 24,178,178 | 84,255,255 | 0,101,101 |
| white | 178,178,178 | 255,255,255 | 101,101,101 |

Background `0,0,0`, foreground `178,178,178`.

Note the yellow: `178,104,24` is a **brown**, not a yellow. That is the
real VGA behaviour and every faithful terminal keeps it. Ours does too.

From the reference screenshot, the chrome is: a title reading
`Shell - Konsole`, a menu bar of **Session, Edit, View, Bookmarks,
Settings, Help**, one short toolbar under it, and then black. `ls`
output is coloured by type, directories in blue.

## The file manager — GNOME 2.30 / Nautilus

Read off the reference screenshot, since the release notes have moved.
What is structural, in order down the window:

- Menu bar: **File, Edit, View, Bookmarks, Help**
- Toolbar: Back, Forward, Up, Stop, Reload, then Home, then a zoom
  control showing **100%**, then a view chooser reading **Icon View**,
  then Search
- A breadcrumb row of **path buttons**, not an editable text field —
  the current directory is the pressed button at the right
- A **Places** sidebar with a close button on its header
- The main area split in two: a tree list on the left with
  **Name / Size / Type** columns, an icon view on the right
- A status bar reading item count and free space

The places list in the reference: Desktop, File System, Network, cdrom1,
Trash, Documents, Music, Pictures, Videos, Downloads. Ours is the same
idea against RSD's own filesystem.

## The installer — FreeBSD

`bsdinstall` is a sequence of `dialog(1)` screens: a blue field, a grey
box centred on it, a title in the top border, a one-line hint along the
bottom, and `< OK >` / `< Cancel >` in angle brackets. It asks one
question per screen and it never asks a question it can answer itself.

### The colours, from dialog's `dlg_colors.h`

Not a palette chosen to look about right — these are the defaults every
unconfigured `dialog` comes up in, and `bsdinstall` sets no `DIALOGRC`:

```
SCREEN          CYAN on BLUE, bold      SHADOW    BLACK on BLACK, bold
DIALOG          BLACK on WHITE          TITLE     BLUE on WHITE, bold
BORDER          WHITE on WHITE, bold    BORDER2   = DIALOG
BUTTON_ACTIVE   WHITE on BLUE, bold     BUTTON_INACTIVE  BLACK on WHITE
BUTTON_KEY_INACTIVE   RED on WHITE      BUTTON_LABEL_ACTIVE  YELLOW on BLUE
ITEM            BLACK on WHITE          ITEM_SELECTED    WHITE on BLUE, bold
TAG_KEY         RED on WHITE            GAUGE     BLUE on WHITE, bold
ITEMHELP        WHITE on BLACK
```

On a sixteen-colour console curses' `COLOR_WHITE` without bold is the
light grey `0xAAAAAA`, and bold sets bit 3 — so "white on white, bold"
is bright white on grey, which is the **lit** top-left edge of a raised
box, and `BORDER2` is black on grey, the shaded one. That pair is the
whole 3D look. `ITEMHELP` is the only thing on a bsdinstall screen that
is not on the blue field.

### The geometry

```
dialog.h        SHADOW_ROWS 1, SHADOW_COLS 2
util.c          dlg_draw_box2(): borderchar top and left,
                borderchar2 bottom and right, boxchar for the fill
                dlg_draw_title(): centred on row 0 of the box
                dlg_put_backtitle(): row 0 col 1, then a rule on row 1
                dlg_attr_clear(): the field is SPACES, not a speckle
buttons.c       print_button(): "<", the label with its hotkey lit, ">"
guage.c         repaint_text(): a box round the bar, the trough filled
                with spaces in gauge_attr, the percentage printed
                centred, then the first x cells redrawn REVERSED
```

That last one is why the number on a dialog gauge sits *on* the bar: it
is one string, drawn once, and the bar passing through it flips its
colours a character at a time.

### The words

From `usr.sbin/bsdinstall/scripts/`:

```
auto      --backtitle "$OSNAME Installer"
          hline_arrows_tab_enter="Press arrows, TAB or ENTER"
          hline_arrows_tab_space_enter="Press arrows, TAB, SPACE or ENTER"
keymap    msg_keymap_selection="Keymap Selection"
hostname  msg_set_hostname="Set Hostname"
netconfig --title 'Network Configuration'
services  --title "System Configuration"
time      --title 'Time & Date'
```

The backtitle carries no release number; the release is on the welcome
screen, where you read it once.

## The console — FreeBSD and OpenBSD

The brief is "messy", and it is the right brief. A BSD console at boot
is **dense**: every driver that probes prints, whether or not it found
anything, and it prints the bus address, the model string and the
resources it took. Nothing is hidden behind a progress bar and nothing
is summarised. It looks like a machine talking to itself, because it is.

That is the opposite of where the console had drifted — clean, spaced,
one tidy line per stage — and it is what is being undone.

### Read from real ones

Two dmesgs off [dmesgd.nycbug.org](https://dmesgd.nycbug.org/), which is
where people post theirs: an OpenBSD 8.0 on a ThinkPad X13s and a
FreeBSD 15.1 on a Celeron. Not copied — what is taken is the *shape* of
a line, which is different in each:

```
OpenBSD    cpu0 at mainbus0 mpidr 0: ARM Cortex-A78C r0p0
           qcscm0 at mainbus0
           "reserved-region" at mainbus0 not configured

FreeBSD    CPU: Intel(R) Celeron(R) 7305 (1113.60-MHz K8-class CPU)
             Origin="GenuineIntel"  Id=0x906a4  Family=0x6  Model=0x9a
             Features=0xbfebfbff<FPU,VME,DE,PSE,TSC,MSR,PAE,MCE,...>
           Timecounter "HPET" frequency 19200000 Hz quality 950
           hpet0: <High Precision Event Timer> iomem 0xfed00000-... on acpi0
```

OpenBSD's `"name" at parent not configured` is the one people remember —
it prints the things it found and *could not drive*, by name, over and
over. FreeBSD's contribution is the unbroken feature list and the
`Timecounter` lines. The brief was "freebsd + openbsd", so both are in
there.

**It does not fit on one screen, and that is the point.** There was a
note in `shell.c` warning that one more line of probe would scroll the
copyright off the top, and a check asserting it had not. A boot you can
read in full on an 80×25 console is a boot that is not telling you much.
It scrolls; the message buffer keeps it; `dmesg` is what that buffer is
for. The buffer is 204 lines because a BSD `msgbuf` is 16 KB, which at
eighty columns is 204 lines.

**One number, three places, checked.** `pci0` says how many devices it
identified, the probe lines under it are those devices, and the boot
ledger's `pci` stage says it again. The harness parses the first, counts
the second and reads the third. A device added to the probe without the
count moving fails the build.

---

## The artwork

| what | where from | licence |
|---|---|---|
| Application and place icons | Oxygen (KDE) | LGPL-3+ |
| Toolbar, mimetype and place icons | Tango (freedesktop.org) | public domain |
| Letters | DejaVu Sans Mono | DejaVu / Bitstream Vera |
| The mark | the owner's own drawing | his |
| The wallpaper | the owner's own | his |

Per-file paths, artists and sha256s are in `assets/icons/SOURCE.txt`,
which `tools/vendor-icons.py` generates and nobody edits by hand.
