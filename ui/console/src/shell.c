/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/shell.h>
#include <rsd/version.h>

#include "rsd_mark.h"

bool rsd_builtin(struct rsd_shell *sh, const struct rsd_argv *av);

static void say(struct rsd_shell *sh, const char *s)
{
    rsd_term_puts(&sh->term, s);
    rsd_term_newline(&sh->term);
}

/*
 * The loader menu, copied from FreeBSD's: a picture, four numbered
 * choices, and return takes the first. The other three matter when the
 * first one does not work, which is when you are least able to go
 * looking for them.
 *
 * AND THE PICTURE IS THE MARK, not a drawing of one.  It was eight rows
 * of hand-placed slashes that suggested a mark; it is the owner's
 * drawing itself now, and it lives in rsd_mark.h because the panic
 * screen draws it too.
 *
 * It is monochrome here because the loader is.  rsdfetch keeps the
 * colours, and the panic screen reads the gradient as brightness; a
 * boot menu that needs a colour terminal to be legible is a boot menu
 * that fails on the console you fall back to.
 */

static const char *const MENU[] = {
    "1. Boot " RSD_SYSTEM " [default]",
    "2. Boot single user",
    "3. Escape to loader prompt",
    "4. Reboot",
};

#define MENU_ROWS (sizeof(MENU) / sizeof(MENU[0]))

/* Where the mark starts, and the one place that number lives. */
#define MARK_INDENT 4U

static void loader_menu(struct rsd_shell *sh)
{
    rsd_term_reset(&sh->term);
    say(sh, RSD_SYSTEM " loader " RSD_RELEASE);
    rsd_term_newline(&sh->term);

    for (uint32_t r = 0U; r < RSD_MARK_ROWS; ++r) {
        for (uint32_t pad = 0U; pad < MARK_INDENT; ++pad) {
            rsd_term_putc(&sh->term, ' ');
        }
        rsd_term_puts(&sh->term, RSD_MARK[r]);
        /*
         * The menu sits beside the mark, not under it, and where it
         * starts is WORKED OUT rather than written down. It was 36,
         * with a note saying that was the widest row of the mark plus
         * its indent plus two columns of air. The mark was 30 columns
         * wide; the RSD wordmark is 45, so 36 put the boot line through
         * the middle of the S.
         *
         * RSD_MARK_COLS is what the generator wrote into the header, so
         * this follows the art instead of remembering a measurement of
         * art that has been replaced.
         */
        if (r >= 2U && r - 2U < MENU_ROWS) {
            while (sh->term.col < MARK_INDENT + RSD_MARK_COLS + 2U) {
                rsd_term_putc(&sh->term, ' ');
            }
            rsd_term_puts(&sh->term, MENU[r - 2U]);
        }
        rsd_term_newline(&sh->term);
    }
    rsd_term_newline(&sh->term);
    say(sh, "Press a number, or return for the default.");
    rsd_term_newline(&sh->term);
    sh->term.cursor = true;
}

/*
 * THE BOOT.
 *
 * An OpenBSD console tells you what it found and then gets out of the
 * way. No progress bar, no logo, no "starting services" spinner. The
 * kernel line, the memory it counted, the devices it probed, the root
 * it mounted. If you can read it you can diagnose it, and if you cannot
 * read it fast enough it is still on the screen - and in dmesg, which
 * is why dmesg exists.
 *
 * The one line that is RSD's own is the ledger summary. Every other
 * system says it booted; this one says how many of its declared stages
 * were checked before it got here.
 */
static void kernel_say(struct rsd_shell *sh, const char *s)
{
    rsd_dmesg_add(s);
    say(sh, s);
}

static void copyright(struct rsd_shell *sh)
{
    /* Where FreeBSD puts its own, ahead of the kernel line. */
    kernel_say(sh, "Copyright (c) 2026 Saud Aljuaid.");
    kernel_say(sh, "Copyright (c) 2026 The " RSD_SYSTEM
                   " Project.  All rights reserved.");
    rsd_term_newline(&sh->term);
}

/*
 * THE PROBE, AND IT IS MEANT TO BE A LOT.
 *
 * This was eleven lines: the kernel, the memory, the processor, four
 * devices and a mount. Tidy, readable, and nothing like the thing it is
 * copying. A BSD console at boot is DENSE - every driver that probes
 * prints whether or not it found anything, with the bus address it
 * looked at, the model string it read back and the resources it took,
 * and the ones that found nothing print that too. Nothing is hidden
 * behind a progress bar and nothing is summarised. It looks like a
 * machine talking to itself, because it is.
 *
 * Both idioms are here because the brief was both:
 *
 *   OpenBSD    dev0 at parent0 ...: what it is
 *              "name" at parent0 dev N function M not configured
 *   FreeBSD    CPU feature lists in one unbroken run
 *              Timecounter "X" frequency N Hz quality Q
 *
 * IT DOES NOT FIT ON THE SCREEN AND IT IS NOT SUPPOSED TO. There used
 * to be a note here saying one more line would scroll the copyright off
 * the top, and a check asserting that it had not. That is backwards: a
 * boot you can read in full on an 80x25 console is a boot that is not
 * telling you very much. It scrolls, the buffer keeps all of it, and
 * dmesg is the reason the buffer exists.
 *
 * THE COUNT IS NOT TYPED IN TWICE. pci0's line says how many devices
 * were identified and the lines under it are those devices; the harness
 * parses the number out of the first and counts the rest, so a device
 * added here without the number moving fails the build.
 */
static const char *const PROBE[] = {
    "random: good seed from the boot blocks",
    "mainbus0 at root",
    "bios0 at mainbus0: SMBIOS rev. 2.8 @ 0x000f5a10 (9 entries)",
    "bios0: vendor SeaBIOS version \"1.16.3\" date 04/01/2014",
    "bios0: QEMU Standard PC (i440FX + PIIX, 1996)",
    "acpi0 at bios0: ACPI 1.0",
    "acpi0: tables DSDT FACP SSDT APIC HPET WAET",
    "acpi0: wakeup devices PCI0(S3)",
    "acpitimer0 at acpi0: 3579545 Hz, 24 bits",
    "acpimadt0 at acpi0 addr 0xfee00000: PC-AT compat",
    "cpu0 at mainbus0: apid 0 (boot processor)",
    "cpu0: QEMU Virtual CPU version 2.5+, 2400.11 MHz, 06-06-03",
    "cpu0: FPU,VME,DE,PSE,TSC,MSR,PAE,MCE,CX8,APIC,SEP,MTRR,PGE,MCA,"
        "CMOV,PAT,PSE36,CFLUSH,MMX,FXSR,SSE,SSE2,SSE3,CX16,x2APIC,HV,"
        "NXE,LONG,LAHF",
    "cpu0: 512KB 64b/line 16-way L2 cache",
    "cpu0: no SMP, no FPU in kernel: soft float only",
    "cpu0: mwait refused by the hypervisor, idling with hlt",
    "Timecounter \"TSC\" frequency 2400110000 Hz quality 800",
    "Timecounter \"i8254\" frequency 1193182 Hz quality 0",
    "ioapic0 at mainbus0: apid 0 pa 0xfec00000, version 11, 24 pins",
    "pci0 at mainbus0 bus 0: 10 devices identified",
    "pchb0 at pci0 dev 0 function 0 \"Intel 82441FX\" rev 0x02",
    "pcib0 at pci0 dev 1 function 0 \"Intel 82371SB ISA\" rev 0x00",
    "pciide0 at pci0 dev 1 function 1 \"Intel 82371SB IDE\" rev 0x00: DMA",
    "pciide0: channel 0 ignored (disabled)",
    "pciide0: channel 1 ignored (disabled)",
    "uhci0 at pci0 dev 1 function 2 \"Intel 82371SB USB\" rev 0x01: "
        "apic 0 int 11",
    "usb0 at uhci0: USB revision 1.0",
    "uhub0 at usb0 configuration 1 interface 0 \"Intel UHCI root hub\" "
        "rev 1.00/1.00 addr 1",
    "\"Intel 82371AB Power\" rev 0x03 at pci0 dev 1 function 3 "
        "not configured",
    "vga1 at pci0 dev 2 function 0 \"QEMU Bochs VGA\" rev 0x02",
    "wsdisplay0 at vga1 mux 1: console (80x25, vt100 emulation)",
    "virtio0 at pci0 dev 3 function 0 \"Qumranet Virtio Network\" "
        "rev 0x00",
    "vio0 at virtio0: address 52:54:00:12:34:56",
    "virtio0: msix shared, 3 queues, IPv4 only",
    "nvme0 at pci0 dev 4 function 0 \"QEMU NVM Express\" rev 0x02: "
        "apic 0 int 10",
    "nvme0: QEMU NVMe Ctrl, firmware 1.0, serial nvme-0001",
    "nvme0: 1 namespace, 512 byte sectors",
    "sd0 at scsibus0 targ 0 lun 0: <NVMe, QEMU NVMe Ctrl, 1.0>",
    "sd0: 64MB, 512 bytes/sector, 131072 sectors",
    "hda0 at pci0 dev 5 function 0 \"Intel 82801FB HD Audio\" rev 0x01: "
        "apic 0 int 11",
    "azalia0 at hda0: no supported codecs, disabled",
    "\"Red Hat Virtio Balloon\" rev 0x00 at pci0 dev 6 function 0 "
        "not configured",
    "\"Red Hat Virtio RNG\" rev 0x00 at pci0 dev 7 function 0 "
        "not configured",
    "\"Red Hat Virtio Console\" rev 0x00 at pci0 dev 8 function 0 "
        "not configured",
    "\"Red Hat Virtio Memory\" rev 0x00 at pci0 dev 9 function 0 "
        "not configured",
    "isa0 at pcib0",
    "isadma0 at isa0",
    "pckbc0 at isa0 port 0x60/5 irq 1 irq 12",
    "pckbd0 at pckbc0 (kbd slot)",
    "wskbd0 at pckbd0: console keyboard, using wsdisplay0",
    "pms0 at pckbc0 (aux slot) lost interrupt",
    "pcppi0 at isa0 port 0x61",
    "spkr0 at pcppi0: PC speaker",
    "com0 at isa0 port 0x3f8/8 irq 4: ns16550a, 16 byte fifo",
    "com0: console",
    "iommu: absent, bus mastering disabled",
    "vscsi0 at root",
    "scsibus1 at vscsi0: 256 targets",
    "softraid0 at root",
    "scsibus2 at softraid0: 256 targets",
};

#define PROBE_LINES (sizeof(PROBE) / sizeof(PROBE[0]))

static void devices(struct rsd_shell *sh)
{
    char b[16];

    kernel_say(sh, RSD_BANNER ": Tue Sep 16 10:22:41 UTC 2026");
    kernel_say(sh, "    saud@rsd:/usr/src/sys/arch/amd64/compile/"
                   RSD_KERNEL);
    kernel_say(sh, "real mem  = 268435456 (256MB)");
    kernel_say(sh, "avail mem = 251658240 (240MB)");
    for (uint32_t i = 0U; i < PROBE_LINES; ++i) {
        kernel_say(sh, PROBE[i]);
    }

    /*
     * The one line on this screen that is RSD's own. Every system
     * says it booted; this one says how many of its declared stages
     * were checked on the way.
     */
    {
        char line[RSD_DMESG_COLS];

        (void)rsd_strcopy(line, sizeof(line), "boot ledger: ");
        (void)rsd_u32(b, sizeof(b), rsd_sys_verified_count());
        (void)rsd_strcat(line, sizeof(line), b);
        (void)rsd_strcat(line, sizeof(line), " of ");
        (void)rsd_u32(b, sizeof(b), rsd_sys_stage_count());
        (void)rsd_strcat(line, sizeof(line), b);
        (void)rsd_strcat(line, sizeof(line), " stages verified");
        kernel_say(sh, line);
    }
    kernel_say(sh, "root on nvme0p2 (fat32) swap on nvme0p3 "
                   "dump on nvme0p3");
    kernel_say(sh, "Trying to mount root from fat32:/dev/nvme0p2 [rw]...");
}

/*
 * The rc lines, in OpenBSD's wording. Each one corresponds to something
 * this system does - no "setting tty flags", because there is one
 * console and nothing is set on it, and no "pf enabled", because the
 * motd says there is no firewall and it would be a line claiming one.
 */
static void rc(struct rsd_shell *sh)
{
    kernel_say(sh, "Automatic boot in progress: checking the filesystem.");
    kernel_say(sh, "/dev/nvme0p2: clean, 38112 of 48128 blocks free");
    kernel_say(sh, "/dev/nvme0p2: 4096 files, 10016 used, 38112 free");
    kernel_say(sh, "starting network");
    kernel_say(sh, "vio0: 52:54:00:12:34:56, link state up, 1000baseT "
                   "full-duplex");
    kernel_say(sh, "verifying packages: 5 signed, 2 trust roots");
    kernel_say(sh, "clearing /tmp");
    kernel_say(sh, "savecore: no core dump");
    rsd_term_newline(&sh->term);
}

static void login_banner(struct rsd_shell *sh)
{
    say(sh, RSD_SYSTEM "/amd64 (rsd) (console)");
    rsd_term_newline(&sh->term);
}

static void motd(struct rsd_shell *sh)
{
    rsd_term_newline(&sh->term);
    say(sh, RSD_BANNER);
    rsd_term_newline(&sh->term);
    say(sh, "Development software.  One account, no multi-user");
    say(sh, "permissions, no firewall.  Not a privacy product.");
    rsd_term_newline(&sh->term);
    say(sh, "help for the commands, man intro for the manual,");
    say(sh, "starty for the desktop.");
    rsd_term_newline(&sh->term);
}

const char *rsd_prompt(const struct rsd_shell *sh)
{
    /* # for root, $ for a user - the convention sh set. */
    if (sh != NULL && sh->single) {
        return "# ";
    }
    /* Otherwise the prompt RSD's own documentation already shows. */
    return "rsd$ ";
}

uint32_t rsd_prompt_len(const struct rsd_shell *sh)
{
    return rsd_strlen(rsd_prompt(sh));
}

static void prompt(struct rsd_shell *sh)
{
    rsd_term_puts(&sh->term, rsd_prompt(sh));
    rsd_line_begin(&sh->line, rsd_prompt_len(sh));
}

/* --------------------------------------------------------- config(8) */

/*
 * The kernel config editor, drawn with the installer's toolkit.
 *
 * Two levels: a list of groups, and a checklist inside each one. A
 * toggle writes straight through to the option table, so backing out of
 * a group with Escape keeps what you changed - there is no third copy
 * of the state to get out of step.
 */
static const char *const CONF_HINT =
    "Arrows move  Space toggles  Enter opens  Esc back  F1 help";

static void conf_draw(struct rsd_shell *sh)
{
    const char *help = rsd_ui_hint(&sh->ui);

    rsd_ui_backdrop(&sh->term, RSD_SYSTEM " " RSD_RELEASE
                     " kernel configuration",
                     help != NULL ? help : CONF_HINT);
    rsd_ui_draw(&sh->ui, &sh->term);
}

static void conf_groups(struct rsd_shell *sh)
{
    char line[RSD_UI_DESC];
    char n[12];

    sh->conf_group = -1;
    rsd_ui_begin(&sh->ui, RSD_UI_MENU, "Configuration",
                  "Choose a group. Space toggles options inside it.");
    for (uint32_t g = 0U; g < RSD_CONF_GROUPS; ++g) {
        (void)rsd_u32(line, sizeof(line), rsd_conf_group_on(g));
        (void)rsd_strcat(line, sizeof(line), " of ");
        (void)rsd_u32(n, sizeof(n), rsd_conf_group_count(g));
        (void)rsd_strcat(line, sizeof(line), n);
        (void)rsd_strcat(line, sizeof(line), " enabled");
        rsd_ui_item(&sh->ui, rsd_conf_group(g), line, false);
    }
    rsd_ui_buttons(&sh->ui, "Save", "Exit", NULL);
    conf_draw(sh);
}

static void conf_open(struct rsd_shell *sh, uint32_t group)
{
    sh->conf_group = (int32_t)group;
    rsd_ui_begin(&sh->ui, RSD_UI_CHECK, rsd_conf_group(group), NULL);
    for (uint32_t i = 0U; ; ++i) {
        struct rsd_opt *opt = rsd_conf_in(group, i);

        if (opt == NULL) {
            break;
        }
        rsd_ui_item(&sh->ui, opt->name, opt->desc, opt->on);
        rsd_ui_help(&sh->ui, opt->help);
        if (opt->locked) {
            rsd_ui_lock(&sh->ui, i);
        }
    }
    rsd_ui_buttons(&sh->ui, "Back", NULL, NULL);
    conf_draw(sh);
}

/* The widget is the thing being typed into; the table is the thing that
 * matters. Copy one to the other after every key. */
static void conf_flush(struct rsd_shell *sh)
{
    if (sh->conf_group < 0) {
        return;
    }
    for (uint32_t i = 0U; i < sh->ui.items; ++i) {
        (void)rsd_conf_set(sh->ui.item[i].tag, sh->ui.item[i].on);
    }
}

static void conf_leave(struct rsd_shell *sh, bool save)
{
    char n[12];

    rsd_term_reset(&sh->term);
    if (save) {
        rsd_conf_commit();
        say(sh, "config: wrote RSD/KERNEL.CONF");
        say(sh, "config: run 'make' to build a kernel from it");
    } else {
        (void)rsd_u32(n, sizeof(n), rsd_conf_changed());
        rsd_term_puts(&sh->term, "config: left without saving, ");
        rsd_term_puts(&sh->term, n);
        say(sh, " option(s) changed");
    }
    sh->phase = RSD_PHASE_SHELL;
    prompt(sh);
}

void rsd_config_enter(struct rsd_shell *sh)
{
    sh->phase = RSD_PHASE_CONFIG;
    sh->conf_help = false;
    conf_groups(sh);
}

static void conf_key(struct rsd_shell *sh, int key)
{
    enum rsd_ui_result r = rsd_ui_key(&sh->ui, key);

    /*
     * Help sits on top of whichever level was showing. Without knowing
     * that, its OK button reached the code below, did not match "Save",
     * and quietly left the editor.
     */
    if (sh->conf_help) {
        if (r == RSD_UI_ACCEPT || r == RSD_UI_REJECT) {
            sh->conf_help = false;
            if (sh->conf_group < 0) {
                conf_groups(sh);
            } else {
                conf_open(sh, (uint32_t)sh->conf_group);
            }
        }
        return;
    }
    if (r == RSD_UI_ASKED) {
        sh->conf_help = true;
        rsd_ui_begin(&sh->ui, RSD_UI_MSG, "Help",
                      "Options the build can leave out. The locked ones "
                      "cannot go: root is on nvme0 and formatted FAT32, "
                      "and W^X and the no-FP rule are verified by make "
                      "verify rather than chosen here.\n"
                      "\n"
                      "Save writes RSD/KERNEL.CONF. Nothing is "
                      "compiled until you run make.");
        rsd_ui_buttons(&sh->ui, "OK", NULL, NULL);
        conf_draw(sh);
        return;
    }
    if (r == RSD_UI_EDITING) {
        conf_flush(sh);
        conf_draw(sh);
        return;
    }
    if (sh->conf_group < 0) {
        if (r == RSD_UI_REJECT) {
            conf_leave(sh, false);
            return;
        }
        /*
         * Which button is under the cursor only means anything when the
         * cursor is on the buttons. rsd_ui_button() answers either way,
         * so asking it first made Enter on a group read as Save and
         * leave the editor.
         */
        if (sh->ui.on_buttons) {
            conf_leave(sh, rsd_streq(rsd_ui_button(&sh->ui), "Save"));
            return;
        }
        for (uint32_t g = 0U; g < RSD_CONF_GROUPS; ++g) {
            if (rsd_streq(rsd_conf_group(g), rsd_ui_tag(&sh->ui))) {
                conf_open(sh, g);
                return;
            }
        }
        return;
    }
    conf_flush(sh);
    conf_groups(sh);
}

/* ---------------------------------------------------------- config -e */

/*
 * OpenBSD edits a kernel from a prompt rather than a menu. Same table,
 * one line at a time, for when the screen is not available.
 */
static void ukc_prompt(struct rsd_shell *sh)
{
    rsd_term_puts(&sh->term, "ukc> ");
    rsd_line_begin(&sh->line, 5U);
}

static void ukc_show(struct rsd_shell *sh, const struct rsd_opt *opt)
{
    char row[RSD_DMESG_COLS];

    (void)rsd_strcopy(row, sizeof(row), opt->on ? "  " : "  ");
    (void)rsd_strcat(row, sizeof(row), opt->name);
    while (rsd_strlen(row) < 14U) {
        (void)rsd_strcat(row, sizeof(row), " ");
    }
    (void)rsd_strcat(row, sizeof(row), opt->on ? "enabled " : "disabled");
    (void)rsd_strcat(row, sizeof(row), opt->locked ? "  locked  " : "  ");
    (void)rsd_strcat(row, sizeof(row), opt->desc);
    say(sh, row);
}

static void ukc_command(struct rsd_shell *sh, const char *text)
{
    struct rsd_argv av;

    rsd_parse(text, &av);
    if (av.count == 0U) {
        ukc_prompt(sh);
        return;
    }
    if (rsd_streq(av.arg[0], "quit") || rsd_streq(av.arg[0], "exit")) {
        say(sh, "ukc: not saved, run config without -e to write");
        sh->phase = RSD_PHASE_SHELL;
        prompt(sh);
        return;
    }
    if (rsd_streq(av.arg[0], "?") || rsd_streq(av.arg[0], "help")) {
        say(sh, "list              every option");
        say(sh, "show NAME         one option");
        say(sh, "enable NAME       compile it in");
        say(sh, "disable NAME      leave it out");
        say(sh, "quit              leave without saving");
    } else if (rsd_streq(av.arg[0], "list")) {
        for (uint32_t i = 0U; i < rsd_conf_count(); ++i) {
            ukc_show(sh, rsd_conf_at(i));
        }
    } else if (rsd_streq(av.arg[0], "show") && av.count >= 2U) {
        struct rsd_opt *opt = rsd_conf_find(av.arg[1]);

        if (opt == NULL) {
            rsd_term_puts(&sh->term, "ukc: no such option: ");
            say(sh, av.arg[1]);
        } else {
            ukc_show(sh, opt);
            rsd_term_puts(&sh->term, "  ");
            say(sh, opt->help);
        }
    } else if ((rsd_streq(av.arg[0], "enable")
                || rsd_streq(av.arg[0], "disable")) && av.count >= 2U) {
        bool on = rsd_streq(av.arg[0], "enable");
        struct rsd_opt *opt = rsd_conf_find(av.arg[1]);

        if (opt == NULL) {
            rsd_term_puts(&sh->term, "ukc: no such option: ");
            say(sh, av.arg[1]);
        } else if (opt->locked) {
            rsd_term_puts(&sh->term, "ukc: ");
            rsd_term_puts(&sh->term, opt->name);
            say(sh, " is required and cannot be changed");
        } else {
            (void)rsd_conf_set(opt->name, on);
            ukc_show(sh, opt);
        }
    } else {
        rsd_term_puts(&sh->term, "ukc: unknown command: ");
        say(sh, av.arg[0]);
    }
    ukc_prompt(sh);
}

void rsd_ukc_enter(struct rsd_shell *sh)
{
    say(sh, "config: editing the running kernel. ? for commands.");
    sh->phase = RSD_PHASE_UKC;
    ukc_prompt(sh);
}

void rsd_boot(struct rsd_shell *sh)
{
    if (sh == NULL) {
        return;
    }
    rsd_line_reset(&sh->line);
    rsd_dmesg_reset();
    sh->cwd = rsd_fs_root();
    sh->desktop = false;
    sh->single = false;
    sh->status = 0U;
    sh->phase = RSD_PHASE_LOADER;
    loader_menu(sh);
}

/* Option 1, and what return does. */
static void boot_multiuser(struct rsd_shell *sh)
{
    rsd_term_reset(&sh->term);
    copyright(sh);
    devices(sh);
    rc(sh);
    login_banner(sh);
    sh->phase = RSD_PHASE_LOGIN;
    rsd_term_puts(&sh->term, "login: ");
    rsd_line_begin(&sh->line, 7U);
}

/*
 * Option 2. No login, no network, and the filesystem is not checked
 * because checking it is very often the thing you came here to do by
 * hand. FreeBSD asks which shell; the answer is almost always return.
 */
static void boot_single(struct rsd_shell *sh)
{
    rsd_term_reset(&sh->term);
    copyright(sh);
    devices(sh);
    rsd_term_newline(&sh->term);
    say(sh, "Enter full pathname of shell or RETURN for /bin/rsd:");
    sh->phase = RSD_PHASE_SINGLE_ASK;
    rsd_line_begin(&sh->line, 0U);
}

/*
 * Option 3. A prompt at the loader, before there is a kernel to have a
 * shell on. It knows four things, and says so when asked.
 */
static void loader_prompt(struct rsd_shell *sh)
{
    rsd_term_puts(&sh->term, "boot> ");
    rsd_line_begin(&sh->line, 6U);
}

static void loader_escape(struct rsd_shell *sh)
{
    rsd_term_reset(&sh->term);
    say(sh, "Type ? for a list of commands, boot to start "
            RSD_SYSTEM ".");
    rsd_term_newline(&sh->term);
    sh->phase = RSD_PHASE_LOADER_CMD;
    loader_prompt(sh);
}

static void halt(struct rsd_shell *sh, const char *why)
{
    rsd_term_newline(&sh->term);
    say(sh, why);
    sh->phase = RSD_PHASE_HALTED;
    sh->term.cursor = false;
}

/* What the boot> prompt understands. Anything else it says it does
 * not, which is the whole contract of a prompt this small. */
static void loader_command(struct rsd_shell *sh, const char *line)
{
    if (rsd_streq(line, "boot") || line[0] == '\0') {
        boot_multiuser(sh);
        return;
    }
    if (rsd_streq(line, "boot -s") || rsd_streq(line, "-s")) {
        boot_single(sh);
        return;
    }
    if (rsd_streq(line, "?") || rsd_streq(line, "help")) {
        say(sh, "boot        start " RSD_SYSTEM " multi user");
        say(sh, "boot -s     start it single user");
        say(sh, "machine     what the loader found");
        say(sh, "reboot      start the machine again");
    } else if (rsd_streq(line, "machine")) {
        say(sh, "amd64, 256MB, 1 disk, multiboot2");
    } else if (rsd_streq(line, "reboot")) {
        halt(sh, "rebooting...");
        return;
    } else {
        rsd_term_puts(&sh->term, "unknown command: ");
        say(sh, line);
    }
    loader_prompt(sh);
}

void rsd_run(struct rsd_shell *sh, const char *command)
{
    struct rsd_argv av;

    if (sh == NULL || command == NULL) {
        return;
    }
    rsd_parse(command, &av);
    if (av.count == 0U) {
        return;
    }
    if (rsd_streq(av.arg[0], "exit")) {
        sh->phase = RSD_PHASE_HALTED;
        return;
    }
    if (rsd_builtin(sh, &av)) {
        return;
    }
    /*
     * ksh's own wording. Not "command not found, did you mean", not a
     * suggestion: the shell's name, what you typed, and the fact.
     */
    rsd_term_puts(&sh->term, "rsd: ");
    rsd_term_puts(&sh->term, av.arg[0]);
    rsd_term_puts(&sh->term, ": not found");
    rsd_term_newline(&sh->term);
    sh->status = 127U;
}

void rsd_key(struct rsd_shell *sh, int key)
{
    enum rsd_line_result r;

    if (sh == NULL || sh->phase == RSD_PHASE_HALTED) {
        return;
    }

    /*
     * One key, one choice. No line editing here - the screen is full
     * and there is nowhere to print an error without scrolling the
     * menu away, so keys that are not on the list do nothing.
     */
    if (sh->phase == RSD_PHASE_CONFIG) {
        conf_key(sh, key);
        return;
    }

    if (sh->phase == RSD_PHASE_LOADER) {
        switch (key) {
        case '1':
        case '\r':
        case '\n':
            boot_multiuser(sh);
            break;
        case '2':
            boot_single(sh);
            break;
        case '3':
            loader_escape(sh);
            break;
        case '4':
            halt(sh, "rebooting...");
            break;
        default:
            break;
        }
        return;
    }

    /* The password is not echoed and cannot be recalled from history,
     * so it does not go through the line editor at all. */
    if (sh->phase == RSD_PHASE_PASSWORD) {
        if (key == '\r' || key == '\n') {
            rsd_term_newline(&sh->term);
            motd(sh);
            sh->phase = RSD_PHASE_SHELL;
            prompt(sh);
        }
        return;
    }

    r = rsd_line_key(&sh->line, &sh->term, key);
    if (r == RSD_LINE_EDITING) {
        return;
    }

    if (sh->phase == RSD_PHASE_UKC) {
        if (r == RSD_LINE_DONE) {
            char text[RSD_LINE_MAX];

            (void)rsd_strcopy(text, sizeof(text),
                               rsd_line_text(&sh->line));
            ukc_command(sh, text);
        } else {
            ukc_prompt(sh);
        }
        return;
    }

    if (sh->phase == RSD_PHASE_LOADER_CMD) {
        if (r == RSD_LINE_DONE) {
            char line[RSD_LINE_MAX];

            (void)rsd_strcopy(line, sizeof(line),
                               rsd_line_text(&sh->line));
            loader_command(sh, line);
        } else {
            loader_prompt(sh);
        }
        return;
    }

    /* Single user: whatever is typed here would be the shell to run,
     * and there is one shell. Return is the answer either way. */
    if (sh->phase == RSD_PHASE_SINGLE_ASK) {
        rsd_term_newline(&sh->term);
        say(sh, "single user: the filesystem is mounted read-only.");
        say(sh, "mount -uw / to write to it, exit to go multi user.");
        rsd_term_newline(&sh->term);
        sh->single = true;
        sh->phase = RSD_PHASE_SHELL;
        prompt(sh);
        return;
    }

    if (sh->phase == RSD_PHASE_LOGIN) {
        if (r == RSD_LINE_DONE) {
            const char *name = rsd_line_text(&sh->line);

            if (name[0] == '\0') {
                rsd_term_puts(&sh->term, "login: ");
                rsd_line_begin(&sh->line, 7U);
                return;
            }
            if (!rsd_user_exists()) {
                (void)rsd_user_add(name);
            }
            sh->phase = RSD_PHASE_PASSWORD;
            rsd_term_puts(&sh->term, "Password: ");
        } else {
            rsd_term_puts(&sh->term, "login: ");
            rsd_line_begin(&sh->line, 7U);
        }
        return;
    }

    if (r == RSD_LINE_EOF) {
        rsd_term_newline(&sh->term);
        sh->phase = RSD_PHASE_HALTED;
        return;
    }
    if (r == RSD_LINE_DONE) {
        char command[RSD_LINE_MAX];

        (void)rsd_strcopy(command, sizeof(command),
                           rsd_line_text(&sh->line));
        rsd_line_remember(&sh->line, command);
        rsd_run(sh, command);
    }
    if (sh->phase != RSD_PHASE_HALTED) {
        prompt(sh);
    }
}

void rsd_type(struct rsd_shell *sh, const char *text)
{
    uint32_t at = 0U;

    while (text != NULL && text[at] != '\0') {
        rsd_key(sh, (int)(unsigned char)text[at]);
        ++at;
    }
}

void rsd_screen_row(const struct rsd_shell *sh, uint32_t row, char *out,
                     uint32_t capacity)
{
    uint32_t c;
    uint32_t last = 0U;
    uint32_t at = 0U;

    if (out == NULL || capacity == 0U) {
        return;
    }
    out[0] = '\0';
    if (sh == NULL || row >= RSD_ROWS) {
        return;
    }
    for (c = 0U; c < RSD_COLS; ++c) {
        if (sh->term.cell[row][c] != ' ') {
            last = c + 1U;
        }
    }
    for (c = 0U; c < last && at + 1U < capacity; ++c) {
        out[at++] = sh->term.cell[row][c];
    }
    out[at] = '\0';
}
