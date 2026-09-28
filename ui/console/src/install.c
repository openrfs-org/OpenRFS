/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/install.h>
#include <rsd/version.h>

/*
 * One screen per question, in the order the questions have to be asked.
 *
 * The order is not arbitrary and it is not a taste. The keyboard comes
 * first because every answer after it is typed. The disk layout is
 * shown, and confirmed, before anything is written, because the one
 * thing an installer must never do is surprise you with a formatted
 * disk. Passwords come after the writing, because until the writing
 * succeeded there is nothing to set a password on.
 *
 * Every screen can be backed out of. Escape goes back a step, and from
 * the front door it leaves. A machine you cannot get out of the
 * installer on is a machine the installer has taken.
 */

/* bsdinstall's own: --backtitle "$OSNAME Installer", with no
 * release in it. The release is on the welcome screen, which is
 * where you read it once rather than on every screen. */
#define BACKTITLE RSD_SYSTEM " Installer"

/* The tables the screens are built from, and the flags in the installer
 * that remember what was done to them. Names in one place. */
static const char *const COMP_TAG[RSD_COMPONENTS] = {
    "base", "kernel", "sdk", "ports", "src", "tests"
};
static const char *const COMP_DESC[RSD_COMPONENTS] = {
    "The system itself",
    "RSD kernel, GENERIC",
    "Offline C SDK, headers and the porting guide",
    "The ports tree",
    "Kernel and userland sources",
    "The proofs the build runs before it will ship"
};
static const bool COMP_ON[RSD_COMPONENTS] = {
    true, true, true, false, false, false
};

static const char *const START_TAG[RSD_STARTUP] = {
    "ledger", "motd", "network", "pkgverify", "desktop"
};
static const char *const START_DESC[RSD_STARTUP] = {
    "Print the boot ledger before the login prompt",
    "Show the message of the day at login",
    "Bring the interface up at boot",
    "Re-check package signatures at boot",
    "Start the desktop session after login"
};
static const bool START_ON[RSD_STARTUP] = {
    true, true, true, true, false
};

/*
 * Every line of this screen is something RSD already does. It is a
 * list of refusals, not of features: each one is a thing the kernel
 * will not do once it is ticked, and the build already proves it.
 */
static const char *const HARD_TAG[RSD_HARDENING] = {
    "wxorx", "nofpu", "busmaster", "tls12", "abiv1"
};
static const char *const HARD_DESC[RSD_HARDENING] = {
    "Refuse any page that is writable and executable",
    "Refuse floating point and SIMD inside the kernel",
    "Leave bus mastering off while there is no IOMMU",
    "TLS 1.2 only, and only two cipher suites",
    "Hold applications to the Native ABI v1 limits"
};
static const bool HARD_ON[RSD_HARDENING] = {
    true, true, true, true, true
};

struct zone_table {
    const char *region;
    const char *zone[6];
};

static const struct zone_table ZONES[] = {
    { "Africa",    { "Cairo", "Lagos", "Nairobi", NULL, NULL, NULL } },
    { "America",   { "New_York", "Chicago", "Denver", "Los_Angeles",
                     "Sao_Paulo", NULL } },
    { "Asia",      { "Riyadh", "Dubai", "Karachi", "Shanghai", "Tokyo",
                     NULL } },
    { "Australia", { "Sydney", "Perth", NULL, NULL, NULL, NULL } },
    { "Europe",    { "London", "Paris", "Berlin", "Moscow", NULL, NULL } },
    { "Pacific",   { "Auckland", "Honolulu", NULL, NULL, NULL, NULL } },
};

#define REGIONS (sizeof(ZONES) / sizeof(ZONES[0]))

/* ------------------------------------------------------------ helpers */

/*
 * The bottom line says what the highlighted thing is for, and falls
 * back to the keys when it has nothing to add. sysinstall did this, and
 * it is the difference between a list of words and a list you can act
 * on without guessing.
 *
 * Eighty columns is eighty columns. Both of these are centred on the
 * bottom row, and a longer one would lose its tail off the right edge
 * without saying so.
 */
/*
 * THE SCREEN, AND IT IS A DIALOG AGAIN.
 *
 * This drew a transcript: every question and its answer appended down
 * the console, OpenBSD install(8)'s way, with rsd_ui_ask() turning each
 * widget into one line of text and rsd_ui_draw() - the dialog
 * renderer - sitting unused beside it.
 *
 * bsdinstall is not that program. It clears the screen, paints a blue
 * field with its name along the top, and puts ONE question on it in a
 * grey box. You cannot see what you answered three screens ago, and
 * that is the trade: what you get instead is a screen that says what
 * the question is, what the choices are, what the highlighted one does,
 * and which keys move you - all at once, which a transcript cannot do
 * because it has already scrolled.
 *
 * Three things are on screen and only one of them is the box:
 *
 *   row 0     the backtitle, "RSD Installer", with a rule under it
 *   the box   centred, with the title in its top border and the key
 *             legend in its bottom one
 *   row 24    what the highlighted item is for, white on black
 *
 * dialog(1) draws all three and bsdinstall passes it all three:
 * --backtitle, --hline and --item-help.
 */
/*
 * The key legend, which says what the keys do on THIS screen.
 *
 * bsdinstall has two and picks by widget: a checklist adds SPACE
 * because a checklist is the only thing the space bar does anything
 * to. They are its own strings, out of bsdinstall/scripts/auto:
 *
 *     hline_arrows_tab_enter="Press arrows, TAB or ENTER"
 *     hline_arrows_tab_space_enter="Press arrows, TAB, SPACE or ENTER"
 */
static const char *legend(const struct rsd_install *in)
{
    switch (in->ui.kind) {
    case RSD_UI_CHECK:
    case RSD_UI_RADIO:
        return "Press arrows, TAB, SPACE or ENTER";
    case RSD_UI_GAUGE:
        return "";
    default:
        return "Press arrows, TAB or ENTER";
    }
}

static void redraw(struct rsd_install *in)
{
    rsd_ui_hline(&in->ui, legend(in));
    rsd_ui_backdrop(&in->term, BACKTITLE, rsd_ui_hint(&in->ui));
    rsd_ui_draw(&in->ui, &in->term);
}

/* A plain console screen, for the two places the installer stops being
 * an installer and hands the machine back. */
static void handover(struct rsd_install *in, const char *first,
                     const char *second)
{
    rsd_term_reset(&in->term);
    rsd_term_puts(&in->term, first);
    rsd_term_newline(&in->term);
    if (second != NULL) {
        rsd_term_puts(&in->term, second);
        rsd_term_newline(&in->term);
    }
    in->term.cursor = true;
}

static void checklist(struct rsd_install *in, const char *title,
                      const char *text, const char *const *tag,
                      const char *const *desc, const bool *on,
                      uint32_t count)
{
    rsd_ui_begin(&in->ui, RSD_UI_CHECK, title, text);
    for (uint32_t i = 0U; i < count; ++i) {
        rsd_ui_item(&in->ui, tag[i], desc[i], on[i]);
    }
}

static void save_flags(const struct rsd_install *in, bool *on,
                       uint32_t count)
{
    for (uint32_t i = 0U; i < count && i < in->ui.items; ++i) {
        on[i] = in->ui.item[i].on;
    }
}

static uint32_t region_index(const struct rsd_install *in)
{
    for (uint32_t i = 0U; i < REGIONS; ++i) {
        if (rsd_streq(ZONES[i].region, in->region)) {
            return i;
        }
    }
    return 0U;
}

/*
 * What F1 says. One paragraph per screen, about what the screen is
 * asking rather than about how a list works - a help key that explains
 * the arrow keys is a help key nobody presses twice.
 */
static const char *help_for(enum rsd_step step)
{
    switch (step) {
    case RSD_STEP_KEYMAP:
        return "Sets what the keys produce. Written to /ETC/KEYMAP and "
               "changeable afterwards. Everything typed after this "
               "screen uses it, including the root password.";
    case RSD_STEP_COMPONENTS:
        return "base is the system and kernel is what boots it; both "
               "are required. sdk is the offline C compiler and "
               "headers. ports is the tree upstream software is built "
               "from. tests is the build's own test suite.";
    case RSD_STEP_METHOD:
        return "Auto: 1 MiB boot, swap, and the rest as root. Edit: the "
               "same, with a swap size you choose. Shell: a command "
               "line, and you partition the disk yourself.";
    case RSD_STEP_REVIEW:
        return "The layout that will be written. Nothing has been "
               "written yet. Finish goes to the confirmation screen, "
               "which is the last stop before the disk is changed.";
    case RSD_STEP_COMMIT:
        return "Commit writes the partition table and formats the "
               "partitions, erasing the disk. Back returns to the "
               "layout. Revert leaves without writing anything.";
    case RSD_STEP_ROOTPW:
        return "Stored as an iterated SHA-256 digest with a 128-bit "
               "salt. There is no recovery mode and no second account "
               "that can reset it. If you lose it, reinstall.";
    case RSD_STEP_HARDENING:
        return "Each line is a restriction the kernel enforces and the "
               "build verifies. Unticking one removes the check, so "
               "the kernel will then permit what it was blocking.";
    case RSD_STEP_STARTUP:
        return "Runs after the kernel is up. None of it is needed to "
               "boot, and all of it can be changed later in "
               "/ETC/RC.CONF.";
    case RSD_STEP_REGION:
    case RSD_STEP_ZONE:
        return "Sets how the time is displayed. The clock is monotonic "
               "and does not trust the real time clock, so this "
               "changes nothing else.";
    case RSD_STEP_ADDUSER:
        return "A non-root account. Optional, and addable later with "
               "useradd. Skip goes on without one.";
    case RSD_STEP_FINAL:
        return "Each line shows what was chosen and returns to the "
               "screen that chose it. Exit leaves the installer.";
    default:
        return "Nothing to add beyond what is on the screen.";
    }
}

/* ------------------------------------------------------------- screens */

static void enter(struct rsd_install *in, enum rsd_step step);


/* ------------------------------------------------- how it is asked */

/*
 * THE PHRASING LAYER IS GONE, and with it the last of install(8).
 *
 * It sat here and rewrote whatever build() had said into the way
 * OpenBSD's install.sub would have said it: the title and the paragraph
 * were replaced by "Available disks are: sd0 sd1." and a question
 * ending in a default in square brackets. build() was already writing
 * bsdinstall's screens - "Keymap Selection", "Distribution Select",
 * "Select a Disk" are its titles, not ours - and this threw them away
 * on the way to the screen.
 *
 * Taking it out is the whole of putting the screens back: nothing in
 * build() had to change, because build() was never the thing that was
 * wrong.
 */

static void build(struct rsd_install *in)
{
    struct rsd_ui *ui = &in->ui;
    char line[RSD_UI_DESC];

    switch (in->step) {
    case RSD_STEP_KEYMAP:
        rsd_ui_begin(ui, RSD_UI_MENU, "Keymap Selection",
                      "Choose a keyboard layout. Escape keeps the "
                      "first one.");
        rsd_ui_item(ui, "us", "United States, QWERTY", false);
        rsd_ui_help(ui, "The default.");
        rsd_ui_item(ui, "uk", "United Kingdom", false);
        rsd_ui_help(ui, "As us, with \" and @ swapped.");
        rsd_ui_item(ui, "de", "Germany, QWERTZ", false);
        rsd_ui_help(ui, "Y and Z swapped. AltGr reaches the brackets.");
        rsd_ui_item(ui, "fr", "France, AZERTY", false);
        rsd_ui_help(ui, "Digits need Shift.");
        rsd_ui_item(ui, "ar", "Arabic 101", false);
        rsd_ui_help(ui, "Arabic on the second level. Login needs "
                         "Latin.");
        rsd_ui_item(ui, "dvorak", "United States, Dvorak", false);
        rsd_ui_help(ui, "US keys, Dvorak arrangement.");
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_WELCOME:
        rsd_ui_begin(ui, RSD_UI_MSG, "RSD",
                      RSD_SYSTEM " " RSD_RELEASE ".\n"
                      "\n"
                      "Install to a disk, drop to a shell, or run from "
                      "this medium without touching the disk.");
        rsd_ui_buttons(ui, "Install", "Shell", "Live");
        break;

    case RSD_STEP_HOSTNAME:
        rsd_ui_begin(ui, RSD_UI_FORM, "Set Hostname",
                      "Enter a hostname: a single word, or a fully "
                      "qualified name.");
        rsd_ui_field(ui, "Hostname", in->hostname, 28U, false);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_COMPONENTS:
        checklist(in, "Distribution Select",
                  "Choose what to install. base and kernel are "
                  "required.",
                  COMP_TAG, COMP_DESC, in->comp_on, RSD_COMPONENTS);
        rsd_ui_lock(ui, 0U);
        rsd_ui_lock(ui, 1U);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_METHOD:
        rsd_ui_begin(ui, RSD_UI_MENU, "Partitioning",
                      "How should the disk be partitioned?");
        rsd_ui_item(ui, "Auto", "Lay the whole disk out for RSD",
                     false);
        rsd_ui_help(ui, "1 MiB boot, swap, and the rest as root on "
                         "FAT32.");
        rsd_ui_item(ui, "Edit", "Choose the swap size yourself", false);
        rsd_ui_help(ui, "The same layout. You set the swap size.");
        rsd_ui_item(ui, "Shell", "A command line, and do it yourself",
                     false);
        rsd_ui_help(ui, "Type exit to return to the installer.");
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_SWAP:
        rsd_ui_begin(ui, RSD_UI_FORM, "Swap Size",
                      "Swap size in MiB. 1 MiB goes to boot and the "
                      "rest to root.");
        (void)rsd_u32(line, sizeof(line), in->swap_mib);
        rsd_ui_field(ui, "Swap (MiB)", line, 8U, false);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_DISK:
        rsd_ui_begin(ui, RSD_UI_MENU, "Select a Disk",
                      "Choose the disk to install on.");
        (void)rsd_strcopy(line, sizeof(line), "");
        (void)rsd_u32(line, sizeof(line), in->disk_mib);
        (void)rsd_strcat(line, sizeof(line),
                          " MiB   NVMe namespace 1");
        rsd_ui_item(ui, in->disk, line, false);
        rsd_ui_item(ui, "rescan", "Look for disks again", false);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_SCHEME:
        rsd_ui_begin(ui, RSD_UI_RADIO, "Partition Scheme",
                      "Choose a partition scheme. Either will boot; "
                      "GPT unless the firmware is old.");
        rsd_ui_item(ui, "GPT", "GUID partition table",
                     rsd_streq(in->scheme, "GPT"));
        rsd_ui_item(ui, "MBR", "Master boot record",
                     rsd_streq(in->scheme, "MBR"));
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_REVIEW:
        rsd_ui_begin(ui, RSD_UI_MENU, "Review Partitions",
                      "Review the layout. Nothing has been written "
                      "to the disk.");
        (void)rsd_u32(line, sizeof(line), in->disk_mib);
        (void)rsd_strcat(line, sizeof(line), " MiB    ");
        (void)rsd_strcat(line, sizeof(line), in->scheme);
        rsd_ui_item(ui, in->disk, line, false);
        rsd_ui_help(ui, in->by_hand
                     ? "Sizes as you set them."
                     : "Sizes chosen from the size of the disk.");
        rsd_ui_item(ui, "  p1", " 1 MiB    boot", false);
        rsd_ui_help(ui, "Boot partition. Holds the loader.");
        /* Root is whatever is left. Written this way round, the three
         * partitions add up to the disk by construction rather than by
         * three numbers agreeing with each other. */
        (void)rsd_u32_pad(line, sizeof(line),
                           in->disk_mib - in->swap_mib - 1U, 2U, ' ');
        (void)rsd_strcat(line, sizeof(line), " MiB    fat32     /");
        rsd_ui_item(ui, "  p2", line, false);
        rsd_ui_help(ui, "Root: the system, the packages, your "
                         "files.");
        (void)rsd_u32_pad(line, sizeof(line), in->swap_mib, 2U, ' ');
        (void)rsd_strcat(line, sizeof(line), " MiB    swap");
        rsd_ui_item(ui, "  p3", line, false);
        rsd_ui_help(ui, "Not encrypted. Swap encryption is not "
                         "implemented, so paged-out memory is in the "
                         "clear.");
        rsd_ui_buttons(ui, "Finish", "Revert", NULL);
        rsd_ui_select(ui, 2U);
        break;

    case RSD_STEP_COMMIT:
        rsd_ui_begin(ui, RSD_UI_MSG, "Confirmation",
                      "Your changes will now be written to the disk. "
                      "Everything already on it will be PERMANENTLY "
                      "ERASED.\n"
                      "\n"
                      "Nothing has been written yet.");
        rsd_ui_buttons(ui, "Commit", "Back", "Revert");
        ui->on_buttons = true;
        ui->chosen = 1U;      /* the safe one is under the cursor */
        break;

    case RSD_STEP_WRITE:
        rsd_ui_begin(ui, RSD_UI_GAUGE, "Writing", NULL);
        rsd_ui_buttons(ui, NULL, NULL, NULL);
        rsd_ui_gauge(ui, 0U, "Writing the partition table");
        break;

    case RSD_STEP_VERIFY:
        rsd_ui_begin(ui, RSD_UI_GAUGE, "Verifying", NULL);
        rsd_ui_buttons(ui, NULL, NULL, NULL);
        rsd_ui_gauge(ui, 0U, "Hashing the installed tree");
        break;

    case RSD_STEP_ROOTPW:
        /* The one thing worth saying before somebody types into what
         * looks like a dead field. dialog's password box shows nothing
         * at all - not even asterisks - and a person who has not seen
         * one before will type it twice thinking the first went
         * nowhere. */
        rsd_ui_begin(ui, RSD_UI_FORM, "Root Password",
                      "Set a password for root (will not echo). It is "
                      "stored in RSD/LOGIN.DAT as an iterated "
                      "SHA-256 digest with a 128-bit salt, never in "
                      "the clear.");
        rsd_ui_field(ui, "Password", "", 24U, true);
        rsd_ui_field(ui, "Again", "", 24U, true);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_NETIF:
        rsd_ui_begin(ui, RSD_UI_MENU, "Network Configuration",
                      "Choose a network interface. The network is not "
                      "required to finish.");
        rsd_ui_item(ui, "em0", "Intel Gigabit Ethernet", false);
        rsd_ui_item(ui, "none", "Do not configure the network", false);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_DHCP:
        rsd_ui_begin(ui, RSD_UI_MSG, "IPv4",
                      "Configure em0 with DHCP, or by hand?");
        rsd_ui_buttons(ui, "DHCP", "Static", NULL);
        ui->on_buttons = true;
        break;

    case RSD_STEP_STATIC:
        rsd_ui_begin(ui, RSD_UI_FORM, "IPv4 Static Configuration",
                      NULL);
        rsd_ui_field(ui, "IP Address", in->ip, 18U, false);
        rsd_ui_field(ui, "Subnet Mask", in->mask, 18U, false);
        rsd_ui_field(ui, "Default Router", in->router, 18U, false);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_DNS:
        rsd_ui_begin(ui, RSD_UI_FORM, "DNS Configuration", NULL);
        rsd_ui_field(ui, "Nameserver 1", in->dns1, 18U, false);
        rsd_ui_field(ui, "Nameserver 2", in->dns2, 18U, false);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_REGION:
        rsd_ui_begin(ui, RSD_UI_MENU, "Select a Region",
                      "Choose a region. This sets how the time is "
                      "displayed. The clock itself is monotonic and "
                      "does not trust the RTC.");
        for (uint32_t i = 0U; i < REGIONS; ++i) {
            rsd_ui_item(ui, ZONES[i].region, "", false);
        }
        rsd_ui_item(ui, "UTC", "Keep it in UTC", false);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_ZONE: {
        uint32_t r = region_index(in);

        rsd_ui_begin(ui, RSD_UI_MENU, "Select a Time Zone", NULL);
        for (uint32_t i = 0U; i < 6U; ++i) {
            if (ZONES[r].zone[i] == NULL) {
                break;
            }
            rsd_ui_item(ui, ZONES[r].zone[i], "", false);
        }
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;
    }

    case RSD_STEP_STARTUP:
        checklist(in, "Startup",
                  "Choose what runs at boot. All of it can be "
                  "changed later in /ETC/RC.CONF.",
                  START_TAG, START_DESC, in->startup_on, RSD_STARTUP);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_HARDENING:
        checklist(in, "Hardening",
                  "Restrictions the kernel enforces. Unticking one "
                  "removes the check, not just the message.",
                  HARD_TAG, HARD_DESC, in->harden_on, RSD_HARDENING);
        rsd_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case RSD_STEP_ADDUSER:
        rsd_ui_begin(ui, RSD_UI_FORM, "Add a User",
                      "Add a user account for ordinary work. "
                      "Optional.");
        rsd_ui_field(ui, "Username", in->user, 20U, false);
        rsd_ui_field(ui, "Full name", "", 24U, false);
        rsd_ui_field(ui, "Password", "", 20U, true);
        rsd_ui_field(ui, "Again", "", 20U, true);
        rsd_ui_buttons(ui, "OK", "Skip", NULL);
        break;

    case RSD_STEP_FINAL: {
        uint32_t on;

        rsd_ui_begin(ui, RSD_UI_MENU, "Final Configuration",
                      "The installation is finished. Choose an item "
                      "to change it, or Exit.");
        rsd_ui_item(ui, "Exit", "Finish and leave the installer", false);
        rsd_ui_help(ui, "Nothing further is written.");

        rsd_ui_item(ui, "Hostname", in->hostname, false);
        rsd_ui_help(ui, "The name of this machine.");

        if (rsd_streq(in->iface, "none")) {
            (void)rsd_strcopy(line, sizeof(line), "not configured");
        } else {
            (void)rsd_strcopy(line, sizeof(line), in->iface);
            (void)rsd_strcat(line, sizeof(line),
                              in->dhcp ? ", DHCP" : ", ");
            if (!in->dhcp) {
                (void)rsd_strcat(line, sizeof(line), in->ip);
            }
        }
        rsd_ui_item(ui, "Network", line, false);
        rsd_ui_help(ui, "Interface and address.");

        rsd_ui_item(ui, "Time", in->zone, false);
        rsd_ui_help(ui, "How the time is displayed.");

        on = 0U;
        for (uint32_t i = 0U; i < RSD_STARTUP; ++i) {
            on += in->startup_on[i] ? 1U : 0U;
        }
        (void)rsd_u32(line, sizeof(line), on);
        (void)rsd_strcat(line, sizeof(line), " of 5 at boot");
        rsd_ui_item(ui, "Startup", line, false);
        rsd_ui_help(ui, "What runs at boot.");

        on = 0U;
        for (uint32_t i = 0U; i < RSD_HARDENING; ++i) {
            on += in->harden_on[i] ? 1U : 0U;
        }
        (void)rsd_u32(line, sizeof(line), on);
        (void)rsd_strcat(line, sizeof(line), " of 5 refusals kept");
        rsd_ui_item(ui, "Kernel", line, false);
        rsd_ui_help(ui, on == RSD_HARDENING
                     ? "All restrictions enabled."
                     : "One or more restrictions were turned off.");

        rsd_ui_item(ui, "Root", in->rootpw[0] != '\0' ? "set" : "NOT SET",
                     false);
        rsd_ui_help(ui, "Cannot be shown. It can only be set "
                         "again.");

        rsd_ui_item(ui, "User",
                     in->user[0] != '\0' ? in->user : "none", false);
        rsd_ui_help(ui, in->user[0] != '\0'
                     ? "A user account exists."
                     : "No account but root.");

        rsd_ui_buttons(ui, "OK", NULL, NULL);
        break;
    }

    case RSD_STEP_HELP:
        rsd_ui_begin(ui, RSD_UI_MSG, "Help", help_for(in->help_from));
        rsd_ui_buttons(ui, "OK", NULL, NULL);
        break;

    case RSD_STEP_DONE:
        rsd_ui_begin(ui, RSD_UI_MSG, "Complete",
                      "Installation complete. Remove the medium "
                      "before rebooting.\n"
                      "\n"
                      RSD_SYSTEM " boots to a command line and stops "
                      "at a login prompt.");
        rsd_ui_buttons(ui, "Reboot", "Shell", "Live");
        ui->on_buttons = true;
        break;

    default:
        break;
    }
}

static void enter(struct rsd_install *in, enum rsd_step step)
{
    in->step = step;
    if (step == RSD_STEP_SHELL) {
        handover(in, "The installer is still running. Type exit to "
                      "return to it.", "rsd#");
        return;
    }
    if (step == RSD_STEP_REBOOT) {
        handover(in, "Syncing disks... done.",
                 "The operating system has halted. Remove the medium "
                 "and press any key to reboot.");
        in->term.cursor = false;
        return;
    }
    if (step == RSD_STEP_ABANDONED) {
        handover(in, "Nothing was written to the disk.", "rsd#");
        return;
    }
    build(in);
    redraw(in);
}

/* ------------------------------------------------------------ the flow */

/* Where escape goes. Every screen has somewhere to go back to, and the
 * first one goes out of the installer altogether. */
static enum rsd_step back_from(enum rsd_step step)
{
    switch (step) {
    case RSD_STEP_KEYMAP:      return RSD_STEP_WELCOME;
    case RSD_STEP_WELCOME:     return RSD_STEP_ABANDONED;
    case RSD_STEP_HOSTNAME:    return RSD_STEP_WELCOME;
    case RSD_STEP_COMPONENTS:  return RSD_STEP_HOSTNAME;
    case RSD_STEP_METHOD:      return RSD_STEP_COMPONENTS;
    case RSD_STEP_DISK:        return RSD_STEP_METHOD;
    case RSD_STEP_SWAP:        return RSD_STEP_DISK;
    case RSD_STEP_SCHEME:      return RSD_STEP_DISK;
    case RSD_STEP_REVIEW:      return RSD_STEP_SCHEME;
    case RSD_STEP_COMMIT:      return RSD_STEP_REVIEW;
    case RSD_STEP_ROOTPW:      return RSD_STEP_ROOTPW;
    case RSD_STEP_NETIF:       return RSD_STEP_ROOTPW;
    case RSD_STEP_DHCP:        return RSD_STEP_NETIF;
    case RSD_STEP_STATIC:      return RSD_STEP_DHCP;
    case RSD_STEP_DNS:         return RSD_STEP_DHCP;
    case RSD_STEP_REGION:      return RSD_STEP_DNS;
    case RSD_STEP_ZONE:        return RSD_STEP_REGION;
    case RSD_STEP_STARTUP:     return RSD_STEP_REGION;
    case RSD_STEP_HARDENING:   return RSD_STEP_STARTUP;
    case RSD_STEP_ADDUSER:     return RSD_STEP_HARDENING;
    case RSD_STEP_FINAL:       return RSD_STEP_FINAL;
    case RSD_STEP_DONE:        return RSD_STEP_FINAL;
    default:                    return RSD_STEP_WELCOME;
    }
}

/*
 * What a finished screen does next. Everything that reads an answer out
 * of the widget does it here, once, at the moment the screen is left -
 * so there is exactly one place where a question becomes a setting.
 */
static void accept(struct rsd_install *in)
{
    struct rsd_ui *ui = &in->ui;
    const char *pressed = rsd_ui_button(ui);
    char line[RSD_UI_DESC];

    switch (in->step) {
    case RSD_STEP_HELP:
        enter(in, in->help_from);
        return;

    case RSD_STEP_KEYMAP:
        (void)rsd_strcopy(in->keymap, sizeof(in->keymap),
                           rsd_ui_tag(ui));
        enter(in, RSD_STEP_WELCOME);
        return;

    case RSD_STEP_WELCOME:
        if (rsd_streq(pressed, "Shell")) {
            enter(in, RSD_STEP_SHELL);
        } else if (rsd_streq(pressed, "Live")) {
            enter(in, RSD_STEP_ABANDONED);
        } else {
            enter(in, RSD_STEP_HOSTNAME);
        }
        return;

    case RSD_STEP_HOSTNAME:
        (void)rsd_strcopy(in->hostname, sizeof(in->hostname),
                           rsd_ui_value(ui, 0U));
        if (in->hostname[0] == '\0') {
            (void)rsd_strcopy(in->hostname, sizeof(in->hostname),
                               "rsd");
        }
        enter(in, RSD_STEP_COMPONENTS);
        return;

    case RSD_STEP_COMPONENTS:
        save_flags(in, in->comp_on, RSD_COMPONENTS);
        enter(in, RSD_STEP_METHOD);
        return;

    case RSD_STEP_METHOD:
        if (rsd_streq(rsd_ui_tag(ui), "Shell")) {
            enter(in, RSD_STEP_SHELL);
            return;
        }
        in->by_hand = rsd_streq(rsd_ui_tag(ui), "Edit");
        enter(in, RSD_STEP_DISK);
        return;

    case RSD_STEP_SWAP: {
        /* A swap that leaves no room for the system is not a layout,
         * it is a way of losing the disk. One MiB for the loader, and
         * at least sixteen for everything else. */
        uint32_t want = rsd_atou(rsd_ui_value(ui, 0U));
        uint32_t most = in->disk_mib > 17U ? in->disk_mib - 17U : 0U;

        if (want == 0U || want > most) {
            rsd_ui_begin(ui, RSD_UI_FORM, "Swap Size",
                          "That leaves nothing for the system. Swap has "
                          "to be at least 1 MiB and small enough to "
                          "leave 16 MiB of root behind it.");
            (void)rsd_u32(line, sizeof(line), in->swap_mib);
            rsd_ui_field(ui, "Swap (MiB)", line, 8U, false);
            rsd_ui_buttons(ui, "OK", "Cancel", NULL);
            redraw(in);
            return;
        }
        in->swap_mib = want;
        enter(in, RSD_STEP_SCHEME);
        return;
    }

    case RSD_STEP_DISK:
        /* Rescanning is a thing you do on this screen, not a way off
         * it. Pressing it has to leave you here or it is a lie. */
        if (rsd_streq(rsd_ui_tag(ui), "rescan")) {
            enter(in, RSD_STEP_DISK);
            return;
        }
        (void)rsd_strcopy(in->disk, sizeof(in->disk), rsd_ui_tag(ui));
        enter(in, in->by_hand ? RSD_STEP_SWAP : RSD_STEP_SCHEME);
        return;

    case RSD_STEP_SCHEME:
        for (uint32_t i = 0U; i < ui->items; ++i) {
            if (ui->item[i].on) {
                (void)rsd_strcopy(in->scheme, sizeof(in->scheme),
                                   ui->item[i].tag);
            }
        }
        enter(in, RSD_STEP_REVIEW);
        return;

    case RSD_STEP_REVIEW:
        if (rsd_streq(pressed, "Revert")) {
            enter(in, RSD_STEP_METHOD);
        } else {
            enter(in, RSD_STEP_COMMIT);
        }
        return;

    case RSD_STEP_COMMIT:
        if (rsd_streq(pressed, "Commit")) {
            enter(in, RSD_STEP_WRITE);
        } else if (rsd_streq(pressed, "Revert")) {
            enter(in, RSD_STEP_ABANDONED);
        } else {
            enter(in, RSD_STEP_REVIEW);
        }
        return;

    case RSD_STEP_ROOTPW:
        /* Two boxes that do not match is the whole reason there are
         * two boxes. Say so and ask again rather than taking one. */
        if (!rsd_streq(rsd_ui_value(ui, 0U), rsd_ui_value(ui, 1U))) {
            rsd_ui_begin(ui, RSD_UI_FORM, "Root Password",
                          "The passwords did not match. Nothing has "
                          "been set. Try again.");
            rsd_ui_field(ui, "Password", "", 24U, true);
            rsd_ui_field(ui, "Again", "", 24U, true);
            rsd_ui_buttons(ui, "OK", "Cancel", NULL);
            redraw(in);
            return;
        }
        (void)rsd_strcopy(in->rootpw, sizeof(in->rootpw),
                           rsd_ui_value(ui, 0U));
        enter(in, RSD_STEP_NETIF);
        return;

    case RSD_STEP_NETIF:
        (void)rsd_strcopy(in->iface, sizeof(in->iface), rsd_ui_tag(ui));
        enter(in, rsd_streq(in->iface, "none") ? RSD_STEP_REGION
                                                : RSD_STEP_DHCP);
        return;

    case RSD_STEP_DHCP:
        in->dhcp = rsd_streq(pressed, "DHCP");
        enter(in, in->dhcp ? RSD_STEP_DNS : RSD_STEP_STATIC);
        return;

    case RSD_STEP_STATIC:
        (void)rsd_strcopy(in->ip, sizeof(in->ip), rsd_ui_value(ui, 0U));
        (void)rsd_strcopy(in->mask, sizeof(in->mask),
                           rsd_ui_value(ui, 1U));
        (void)rsd_strcopy(in->router, sizeof(in->router),
                           rsd_ui_value(ui, 2U));
        enter(in, RSD_STEP_DNS);
        return;

    case RSD_STEP_DNS:
        (void)rsd_strcopy(in->dns1, sizeof(in->dns1),
                           rsd_ui_value(ui, 0U));
        (void)rsd_strcopy(in->dns2, sizeof(in->dns2),
                           rsd_ui_value(ui, 1U));
        enter(in, RSD_STEP_REGION);
        return;

    case RSD_STEP_REGION:
        (void)rsd_strcopy(in->region, sizeof(in->region),
                           rsd_ui_tag(ui));
        if (rsd_streq(in->region, "UTC")) {
            (void)rsd_strcopy(in->zone, sizeof(in->zone), "UTC");
            enter(in, RSD_STEP_STARTUP);
        } else {
            enter(in, RSD_STEP_ZONE);
        }
        return;

    case RSD_STEP_ZONE:
        (void)rsd_strcopy(in->zone, sizeof(in->zone), in->region);
        (void)rsd_strcat(in->zone, sizeof(in->zone), "/");
        (void)rsd_strcat(in->zone, sizeof(in->zone), rsd_ui_tag(ui));
        enter(in, RSD_STEP_STARTUP);
        return;

    case RSD_STEP_STARTUP:
        save_flags(in, in->startup_on, RSD_STARTUP);
        enter(in, RSD_STEP_HARDENING);
        return;

    case RSD_STEP_HARDENING:
        save_flags(in, in->harden_on, RSD_HARDENING);
        enter(in, RSD_STEP_ADDUSER);
        return;

    case RSD_STEP_ADDUSER:
        if (rsd_streq(pressed, "Skip")) {
            enter(in, RSD_STEP_FINAL);
            return;
        }
        if (!rsd_streq(rsd_ui_value(ui, 2U), rsd_ui_value(ui, 3U))) {
            rsd_ui_begin(ui, RSD_UI_FORM, "Add a User",
                          "The passwords did not match. The account "
                          "has not been created.");
            rsd_ui_field(ui, "Username", rsd_ui_value(ui, 0U), 20U,
                          false);
            rsd_ui_field(ui, "Full name", "", 24U, false);
            rsd_ui_field(ui, "Password", "", 20U, true);
            rsd_ui_field(ui, "Again", "", 20U, true);
            rsd_ui_buttons(ui, "OK", "Skip", NULL);
            redraw(in);
            return;
        }
        (void)rsd_strcopy(in->user, sizeof(in->user),
                           rsd_ui_value(ui, 0U));
        (void)rsd_strcopy(in->userpw, sizeof(in->userpw),
                           rsd_ui_value(ui, 2U));
        enter(in, RSD_STEP_FINAL);
        return;

    case RSD_STEP_FINAL: {
        const char *what = rsd_ui_tag(ui);

        if (rsd_streq(what, "Hostname")) {
            enter(in, RSD_STEP_HOSTNAME);
        } else if (rsd_streq(what, "Network")) {
            enter(in, RSD_STEP_NETIF);
        } else if (rsd_streq(what, "Time")) {
            enter(in, RSD_STEP_REGION);
        } else if (rsd_streq(what, "Startup")) {
            enter(in, RSD_STEP_STARTUP);
        } else if (rsd_streq(what, "Kernel")) {
            enter(in, RSD_STEP_HARDENING);
        } else if (rsd_streq(what, "Root")) {
            enter(in, RSD_STEP_ROOTPW);
        } else if (rsd_streq(what, "User")) {
            enter(in, RSD_STEP_ADDUSER);
        } else {
            enter(in, RSD_STEP_DONE);
        }
        return;
    }

    case RSD_STEP_DONE:
        if (rsd_streq(pressed, "Shell")) {
            enter(in, RSD_STEP_SHELL);
        } else if (rsd_streq(pressed, "Live")) {
            enter(in, RSD_STEP_ABANDONED);
        } else {
            enter(in, RSD_STEP_REBOOT);
        }
        return;

    default:
        return;
    }
}

/* --------------------------------------------------------------- entry */

void rsd_install_begin(struct rsd_install *in)
{
    if (in == NULL) {
        return;
    }
    rsd_term_reset(&in->term);
    (void)rsd_strcopy(in->keymap, sizeof(in->keymap), "us");
    (void)rsd_strcopy(in->hostname, sizeof(in->hostname), "rsd");
    (void)rsd_strcopy(in->disk, sizeof(in->disk), "nvme0");
    in->disk_mib = 64U;
    (void)rsd_strcopy(in->scheme, sizeof(in->scheme), "GPT");
    in->rootpw[0] = '\0';
    in->user[0] = '\0';
    in->userpw[0] = '\0';
    (void)rsd_strcopy(in->ip, sizeof(in->ip), "192.168.1.40");
    (void)rsd_strcopy(in->mask, sizeof(in->mask), "255.255.255.0");
    (void)rsd_strcopy(in->router, sizeof(in->router), "192.168.1.1");
    (void)rsd_strcopy(in->dns1, sizeof(in->dns1), "9.9.9.9");
    (void)rsd_strcopy(in->dns2, sizeof(in->dns2), "1.1.1.1");
    (void)rsd_strcopy(in->iface, sizeof(in->iface), "em0");
    (void)rsd_strcopy(in->region, sizeof(in->region), "Asia");
    (void)rsd_strcopy(in->zone, sizeof(in->zone), "UTC");
    in->dhcp = true;
    in->swap_mib = 16U;
    in->by_hand = false;
    in->help_from = RSD_STEP_WELCOME;
    in->written = false;
    in->verified = false;
    for (uint32_t i = 0U; i < RSD_COMPONENTS; ++i) {
        in->comp_on[i] = COMP_ON[i];
    }
    for (uint32_t i = 0U; i < RSD_STARTUP; ++i) {
        in->startup_on[i] = START_ON[i];
    }
    for (uint32_t i = 0U; i < RSD_HARDENING; ++i) {
        in->harden_on[i] = HARD_ON[i];
    }
    enter(in, RSD_STEP_KEYMAP);
}

void rsd_install_key(struct rsd_install *in, int key)
{
    enum rsd_ui_result r;

    if (in == NULL) {
        return;
    }
    /* A gauge is not waiting for you. Keys pressed at one go nowhere,
     * There is nothing to answer yet. */
    if (in->step == RSD_STEP_WRITE || in->step == RSD_STEP_VERIFY) {
        return;
    }
    if (in->step == RSD_STEP_SHELL || in->step == RSD_STEP_REBOOT
        || in->step == RSD_STEP_ABANDONED) {
        return;
    }
    r = rsd_ui_key(&in->ui, key);
    if (r == RSD_UI_ACCEPT) {
        accept(in);
        return;
    }
    if (r == RSD_UI_REJECT) {
        /* Leaving help puts you back where you pressed F1, not one
         * screen further back from it. */
        enter(in, in->step == RSD_STEP_HELP ? in->help_from
                                             : back_from(in->step));
        return;
    }
    if (r == RSD_UI_ASKED && in->step != RSD_STEP_HELP) {
        in->help_from = in->step;
        enter(in, RSD_STEP_HELP);
        return;
    }
    redraw(in);
}

void rsd_install_type(struct rsd_install *in, const char *text)
{
    for (uint32_t at = 0U; text != NULL && text[at] != '\0'; ++at) {
        rsd_install_key(in, (int)(unsigned char)text[at]);
    }
}

void rsd_install_tick(struct rsd_install *in)
{
    static const char *const WRITING[5] = {
        "Writing the partition table",
        "Formatting nvme0p2 as FAT32",
        "Copying the kernel",
        "Copying the base system",
        "Copying the SDK and the headers"
    };
    static const char *const CHECKING[4] = {
        "Hashing the installed tree",
        "Checking Ed25519 signatures against the trust roots",
        "Writing the boot ledger",
        "Recording what was installed"
    };
    uint32_t pc;

    if (in == NULL) {
        return;
    }
    if (in->step != RSD_STEP_WRITE && in->step != RSD_STEP_VERIFY) {
        return;
    }
    pc = in->ui.percent + 5U;
    if (pc >= 100U) {
        if (in->step == RSD_STEP_WRITE) {
            in->written = true;
            enter(in, RSD_STEP_VERIFY);
        } else {
            in->verified = true;
            enter(in, RSD_STEP_ROOTPW);
        }
        return;
    }
    if (in->step == RSD_STEP_WRITE) {
        rsd_ui_gauge(&in->ui, pc, WRITING[(pc * 5U) / 100U]);
    } else {
        rsd_ui_gauge(&in->ui, pc, CHECKING[(pc * 4U) / 100U]);
    }
    redraw(in);
}

void rsd_install_settle(struct rsd_install *in)
{
    enum rsd_step started;
    uint32_t guard = 0U;

    if (in == NULL) {
        return;
    }
    started = in->step;
    while (in->step == started && guard < 64U) {
        rsd_install_tick(in);
        ++guard;
    }
}

const char *rsd_install_step_name(const struct rsd_install *in)
{
    static const char *const NAMES[RSD_INSTALL_STEPS] = {
        "keymap", "welcome", "hostname", "components", "method",
        "disk", "swap", "scheme", "review", "commit", "write", "verify",
        "rootpw", "netif", "dhcp", "static", "dns", "region", "zone",
        "startup", "hardening", "adduser", "final", "done", "shell",
        "reboot", "abandoned", "help"
    };

    if (in == NULL || (uint32_t)in->step >= RSD_INSTALL_STEPS) {
        return "?";
    }
    return NAMES[in->step];
}

bool rsd_install_selected(const struct rsd_install *in,
                           enum rsd_step step, const char *tag)
{
    const char *const *names;
    const bool *flags;
    uint32_t count;

    if (in == NULL) {
        return false;
    }
    if (step == RSD_STEP_COMPONENTS) {
        names = COMP_TAG;
        flags = in->comp_on;
        count = RSD_COMPONENTS;
    } else if (step == RSD_STEP_STARTUP) {
        names = START_TAG;
        flags = in->startup_on;
        count = RSD_STARTUP;
    } else if (step == RSD_STEP_HARDENING) {
        names = HARD_TAG;
        flags = in->harden_on;
        count = RSD_HARDENING;
    } else {
        return false;
    }
    for (uint32_t i = 0U; i < count; ++i) {
        if (rsd_streq(names[i], tag)) {
            return flags[i];
        }
    }
    return false;
}

void rsd_install_row(const struct rsd_install *in, uint32_t row,
                      char *out, uint32_t capacity)
{
    uint32_t n = 0U;

    if (out == NULL || capacity == 0U) {
        return;
    }
    out[0] = '\0';
    if (in == NULL || row >= RSD_ROWS) {
        return;
    }
    for (uint32_t c = 0U; c < RSD_COLS && n + 1U < capacity; ++c) {
        out[n++] = rsd_term_at(&in->term, row, c);
    }
    while (n != 0U && out[n - 1U] == ' ') {
        --n;
    }
    out[n] = '\0';
}
