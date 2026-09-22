/* SPDX-License-Identifier: GPL-3.0-only */
#include <orfs/install.h>
#include <orfs/version.h>

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

#define BACKTITLE ORFS_SYSTEM " " ORFS_RELEASE " Installer"

/* The tables the screens are built from, and the flags in the installer
 * that remember what was done to them. Names in one place. */
static const char *const COMP_TAG[ORFS_COMPONENTS] = {
    "base", "kernel", "sdk", "ports", "src", "tests"
};
static const char *const COMP_DESC[ORFS_COMPONENTS] = {
    "The system itself",
    "OpenRFS kernel, GENERIC",
    "Offline C SDK, headers and the porting guide",
    "The ports tree",
    "Kernel and userland sources",
    "The proofs the build runs before it will ship"
};
static const bool COMP_ON[ORFS_COMPONENTS] = {
    true, true, true, false, false, false
};

static const char *const START_TAG[ORFS_STARTUP] = {
    "ledger", "motd", "network", "pkgverify", "desktop"
};
static const char *const START_DESC[ORFS_STARTUP] = {
    "Print the boot ledger before the login prompt",
    "Show the message of the day at login",
    "Bring the interface up at boot",
    "Re-check package signatures at boot",
    "Start the desktop session after login"
};
static const bool START_ON[ORFS_STARTUP] = {
    true, true, true, true, false
};

/*
 * Every line of this screen is something OpenRFS already does. It is a
 * list of refusals, not of features: each one is a thing the kernel
 * will not do once it is ticked, and the build already proves it.
 */
static const char *const HARD_TAG[ORFS_HARDENING] = {
    "wxorx", "nofpu", "busmaster", "tls12", "abiv1"
};
static const char *const HARD_DESC[ORFS_HARDENING] = {
    "Refuse any page that is writable and executable",
    "Refuse floating point and SIMD inside the kernel",
    "Leave bus mastering off while there is no IOMMU",
    "TLS 1.2 only, and only two cipher suites",
    "Hold applications to the Native ABI v1 limits"
};
static const bool HARD_ON[ORFS_HARDENING] = {
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
 * THE SCROLLBACK, and one line goes into it per question answered.
 *
 * OpenBSD's installer has no screens.  It prints a question, reads a
 * line, prints the next question underneath, and what you are looking at
 * by the end is every answer you gave.  There is no backdrop to draw, no
 * box to centre, no key legend along the bottom - everything a prompt
 * will accept is written into the prompt, which is why OpenBSD's prompts
 * are as long as they are.
 */
static void say(struct orfs_install *in, const char *text)
{
    if (in->logged >= ORFS_LOG_LINES) {
        /* The screen drops what runs off the top and so does this. */
        for (uint32_t i = 1U; i < ORFS_LOG_LINES; ++i) {
            (void)orfs_strcopy(in->log[i - 1U], ORFS_LOG_COLS,
                               in->log[i]);
        }
        in->logged = ORFS_LOG_LINES - 1U;
    }
    (void)orfs_strcopy(in->log[in->logged], ORFS_LOG_COLS,
                       text != NULL ? text : "");
    ++in->logged;
}

/*
 * The question as it was asked and the answer as it was taken, on one
 * line, exactly as they were on the screen.  orfs_ui_default() is what
 * put the answer in the bracket and it is what reads it back here, so
 * the transcript cannot say one thing and the machine hold another.
 */
static void log_answer(struct orfs_install *in)
{
    char line[ORFS_LOG_COLS];
    char answer[ORFS_UI_VALUE];

    if (in->ui.prompt[0] == '\0') {
        return;
    }
    (void)orfs_strcopy(line, sizeof(line), in->ui.prompt);
    orfs_ui_default(&in->ui, answer, sizeof(answer));
    if (answer[0] != '\0') {
        (void)orfs_strcat(line, sizeof(line), " ");
        (void)orfs_strcat(line, sizeof(line), answer);
    }
    say(in, line);
}

static void redraw(struct orfs_install *in)
{
    orfs_term_reset(&in->term);
    for (uint32_t i = 0U; i < in->logged; ++i) {
        orfs_term_puts(&in->term, in->log[i]);
        orfs_term_newline(&in->term);
    }
    orfs_ui_ask(&in->ui, &in->term);
}

/* A plain console screen, for the two places the installer stops being
 * an installer and hands the machine back. */
static void handover(struct orfs_install *in, const char *first,
                     const char *second)
{
    orfs_term_reset(&in->term);
    orfs_term_puts(&in->term, first);
    orfs_term_newline(&in->term);
    if (second != NULL) {
        orfs_term_puts(&in->term, second);
        orfs_term_newline(&in->term);
    }
    in->term.cursor = true;
}

static void checklist(struct orfs_install *in, const char *title,
                      const char *text, const char *const *tag,
                      const char *const *desc, const bool *on,
                      uint32_t count)
{
    orfs_ui_begin(&in->ui, ORFS_UI_CHECK, title, text);
    for (uint32_t i = 0U; i < count; ++i) {
        orfs_ui_item(&in->ui, tag[i], desc[i], on[i]);
    }
}

static void save_flags(const struct orfs_install *in, bool *on,
                       uint32_t count)
{
    for (uint32_t i = 0U; i < count && i < in->ui.items; ++i) {
        on[i] = in->ui.item[i].on;
    }
}

static uint32_t region_index(const struct orfs_install *in)
{
    for (uint32_t i = 0U; i < REGIONS; ++i) {
        if (orfs_streq(ZONES[i].region, in->region)) {
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
static const char *help_for(enum orfs_step step)
{
    switch (step) {
    case ORFS_STEP_KEYMAP:
        return "Sets what the keys produce. Written to /ETC/KEYMAP and "
               "changeable afterwards. Everything typed after this "
               "screen uses it, including the root password.";
    case ORFS_STEP_COMPONENTS:
        return "base is the system and kernel is what boots it; both "
               "are required. sdk is the offline C compiler and "
               "headers. ports is the tree upstream software is built "
               "from. tests is the build's own test suite.";
    case ORFS_STEP_METHOD:
        return "Auto: 1 MiB boot, swap, and the rest as root. Edit: the "
               "same, with a swap size you choose. Shell: a command "
               "line, and you partition the disk yourself.";
    case ORFS_STEP_REVIEW:
        return "The layout that will be written. Nothing has been "
               "written yet. Finish goes to the confirmation screen, "
               "which is the last stop before the disk is changed.";
    case ORFS_STEP_COMMIT:
        return "Commit writes the partition table and formats the "
               "partitions, erasing the disk. Back returns to the "
               "layout. Revert leaves without writing anything.";
    case ORFS_STEP_ROOTPW:
        return "Stored as an iterated SHA-256 digest with a 128-bit "
               "salt. There is no recovery mode and no second account "
               "that can reset it. If you lose it, reinstall.";
    case ORFS_STEP_HARDENING:
        return "Each line is a restriction the kernel enforces and the "
               "build verifies. Unticking one removes the check, so "
               "the kernel will then permit what it was blocking.";
    case ORFS_STEP_STARTUP:
        return "Runs after the kernel is up. None of it is needed to "
               "boot, and all of it can be changed later in "
               "/ETC/RC.CONF.";
    case ORFS_STEP_REGION:
    case ORFS_STEP_ZONE:
        return "Sets how the time is displayed. The clock is monotonic "
               "and does not trust the real time clock, so this "
               "changes nothing else.";
    case ORFS_STEP_ADDUSER:
        return "A non-root account. Optional, and addable later with "
               "useradd. Skip goes on without one.";
    case ORFS_STEP_FINAL:
        return "Each line shows what was chosen and returns to the "
               "screen that chose it. Exit leaves the installer.";
    default:
        return "Nothing to add beyond what is on the screen.";
    }
}

/* ------------------------------------------------------------- screens */

static void enter(struct orfs_install *in, enum orfs_step step);


/* ------------------------------------------------- how it is asked */

/*
 * EVERY QUESTION IN ONE PLACE, in the shape install.sub asks it.
 *
 * OpenBSD's installer has three forms and no others.  ask() prints a
 * question and a default in square brackets.  ask_yn() does the same
 * with yes or no.  ask_which() prints
 *
 *     Available disks are: sd0 sd1.
 *     Which disk is the root disk? ('?' for details) [sd0]
 *
 * - the list first, as a sentence, then the question.  The aside in
 * round brackets is not decoration: it is the complete list of things
 * the prompt will take that are not on the list, and it is there
 * because there is no help line anywhere to put it on.
 *
 * The noun below is what goes in "Available ___s are"; a screen with
 * nothing to list leaves it NULL.  The list itself is composed from the
 * items build() just put in the widget, so a choice that is offered is
 * a choice that gets printed and there is no second table to drift.
 */
struct phrasing {
    const char *noun;
    const char *lead;
    const char *ask;
    /*
     * A screen whose choices are ROWS rather than words.  install.sub
     * prints the disklabel it is about to write, and the summary at the
     * end, one line each with their values beside them - those are not
     * lists you pick a name out of, they are tables you read.  The rest
     * of the installer's lists are "Available sets are: base kernel."
     */
    bool table;
};

static struct phrasing phrasing_for(enum orfs_step step)
{
    struct phrasing p = { NULL, NULL, NULL, false };

    switch (step) {
    case ORFS_STEP_KEYMAP:
        p.noun = "keyboard layout";
        p.ask = "Choose your keyboard layout ('?' for list)";
        break;
    case ORFS_STEP_WELCOME:
        p.lead = "Welcome to the " ORFS_SYSTEM "/amd64 " ORFS_RELEASE
                 " installation program.";
        p.ask = "(I)nstall, (S)hell or (L)ive?";
        break;
    case ORFS_STEP_HOSTNAME:
        p.ask = "System hostname? (short form, e.g. 'foo')";
        break;
    case ORFS_STEP_COMPONENTS:
        p.noun = "set";
        p.ask = "Set name(s)? (or 'done')";
        break;
    case ORFS_STEP_METHOD:
        p.ask = "Use (A)uto layout, (E)dit auto layout, or create "
                "(C)ustom layout?";
        break;
    case ORFS_STEP_DISK:
        p.noun = "disk";
        p.ask = "Which disk is the root disk? ('?' for details)";
        break;
    case ORFS_STEP_SWAP:
        p.ask = "Size of swap, in MiB?";
        break;
    case ORFS_STEP_SCHEME:
        p.ask = "Use whole disk (G)PT, whole disk (M)BR or (E)dit?";
        break;
    case ORFS_STEP_REVIEW:
        p.lead = "The label that will be written.  Nothing has been "
                 "written to the disk yet.";
        p.table = true;
        p.ask = "Write new label?";
        break;
    case ORFS_STEP_COMMIT:
        p.lead = "Nothing has been written to the disk yet.";
        p.ask = "Are you *SURE* you want to write to " /* the disk */
                "the root disk?";
        break;
    case ORFS_STEP_ROOTPW:
        p.ask = "Password for root account? (will not echo)";
        break;
    case ORFS_STEP_NETIF:
        p.noun = "network interface";
        p.ask = "Which network interface do you wish to configure? "
                "(or 'done')";
        break;
    case ORFS_STEP_DHCP:
        p.ask = "IPv4 address for the interface? (or 'dhcp' or 'none')";
        break;
    case ORFS_STEP_STATIC:
        p.ask = "Address, netmask and default route?";
        break;
    case ORFS_STEP_DNS:
        p.ask = "DNS nameservers? (or 'none')";
        break;
    case ORFS_STEP_REGION:
        p.noun = "region";
        p.ask = "What timezone are you in? ('?' for list)";
        break;
    case ORFS_STEP_ZONE:
        p.noun = "timezone";
        p.ask = "What timezone are you in? ('?' for list)";
        break;
    case ORFS_STEP_STARTUP:
        p.noun = "daemon";
        p.ask = "Which daemons should start by default? (or 'done')";
        break;
    case ORFS_STEP_HARDENING:
        p.noun = "restriction";
        p.ask = "Which restrictions should the kernel enforce? "
                "(or 'done')";
        break;
    case ORFS_STEP_ADDUSER:
        p.ask = "Setup a user? (enter a lower-case loginname, or 'no')";
        break;
    case ORFS_STEP_FINAL:
        p.table = true;
        p.ask = "Anything to change? (or 'done')";
        break;
    case ORFS_STEP_DONE:
        p.lead = "CONGRATULATIONS! Your " ORFS_SYSTEM " install has "
                 "been successfully completed!\n"
                 "\n"
                 "To boot the new system, enter halt at the command "
                 "prompt.  Once the\n"
                 "system has halted, remove the medium before "
                 "rebooting.";
        break;
    default:
        break;
    }
    return p;
}

/*
 * Rewrite whatever build() said into the way install.sub would say it.
 * build() decides WHAT is offered; this decides how it is put, and
 * keeping the two apart is what let the boxes come off without touching
 * a single answer.
 */
static void phrase(struct orfs_install *in)
{
    struct phrasing p = phrasing_for(in->step);
    char line[ORFS_UI_TEXT_MAX];

    line[0] = '\0';
    if (p.lead != NULL) {
        (void)orfs_strcopy(line, sizeof(line), p.lead);
    }
    if (p.table) {
        for (uint32_t i = 0U; i < in->ui.items; ++i) {
            if (line[0] != '\0') {
                (void)orfs_strcat(line, sizeof(line), "\n");
            }
            (void)orfs_strcat(line, sizeof(line), in->ui.item[i].tag);
            if (in->ui.item[i].desc[0] != '\0') {
                (void)orfs_strcat(line, sizeof(line), "  ");
                (void)orfs_strcat(line, sizeof(line),
                                  in->ui.item[i].desc);
            }
        }
    } else if (p.noun != NULL && in->ui.items != 0U) {
        if (line[0] != '\0') {
            (void)orfs_strcat(line, sizeof(line), "\n");
        }
        (void)orfs_strcat(line, sizeof(line), "Available ");
        (void)orfs_strcat(line, sizeof(line), p.noun);
        (void)orfs_strcat(line, sizeof(line), "s are:");
        for (uint32_t i = 0U; i < in->ui.items; ++i) {
            (void)orfs_strcat(line, sizeof(line), " ");
            (void)orfs_strcat(line, sizeof(line), in->ui.item[i].tag);
        }
        (void)orfs_strcat(line, sizeof(line), ".");
    }
    if (line[0] != '\0' || p.ask != NULL) {
        (void)orfs_strcopy(in->ui.text, sizeof(in->ui.text), line);
    }
    orfs_ui_prompt(&in->ui, p.ask);
}

static void build(struct orfs_install *in)
{
    struct orfs_ui *ui = &in->ui;
    char line[ORFS_UI_DESC];

    switch (in->step) {
    case ORFS_STEP_KEYMAP:
        orfs_ui_begin(ui, ORFS_UI_MENU, "Keymap Selection",
                      "Choose a keyboard layout. Escape keeps the "
                      "first one.");
        orfs_ui_item(ui, "us", "United States, QWERTY", false);
        orfs_ui_help(ui, "The default.");
        orfs_ui_item(ui, "uk", "United Kingdom", false);
        orfs_ui_help(ui, "As us, with \" and @ swapped.");
        orfs_ui_item(ui, "de", "Germany, QWERTZ", false);
        orfs_ui_help(ui, "Y and Z swapped. AltGr reaches the brackets.");
        orfs_ui_item(ui, "fr", "France, AZERTY", false);
        orfs_ui_help(ui, "Digits need Shift.");
        orfs_ui_item(ui, "ar", "Arabic 101", false);
        orfs_ui_help(ui, "Arabic on the second level. Login needs "
                         "Latin.");
        orfs_ui_item(ui, "dvorak", "United States, Dvorak", false);
        orfs_ui_help(ui, "US keys, Dvorak arrangement.");
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_WELCOME:
        orfs_ui_begin(ui, ORFS_UI_MSG, "OpenRFS",
                      ORFS_SYSTEM " " ORFS_RELEASE ".\n"
                      "\n"
                      "Install to a disk, drop to a shell, or run from "
                      "this medium without touching the disk.");
        orfs_ui_buttons(ui, "Install", "Shell", "Live");
        break;

    case ORFS_STEP_HOSTNAME:
        orfs_ui_begin(ui, ORFS_UI_FORM, "Set Hostname",
                      "Enter a hostname: a single word, or a fully "
                      "qualified name.");
        orfs_ui_field(ui, "Hostname", in->hostname, 28U, false);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_COMPONENTS:
        checklist(in, "Distribution Select",
                  "Choose what to install. base and kernel are "
                  "required.",
                  COMP_TAG, COMP_DESC, in->comp_on, ORFS_COMPONENTS);
        orfs_ui_lock(ui, 0U);
        orfs_ui_lock(ui, 1U);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_METHOD:
        orfs_ui_begin(ui, ORFS_UI_MENU, "Partitioning",
                      "How should the disk be partitioned?");
        orfs_ui_item(ui, "Auto", "Lay the whole disk out for OpenRFS",
                     false);
        orfs_ui_help(ui, "1 MiB boot, swap, and the rest as root on "
                         "FAT32.");
        orfs_ui_item(ui, "Edit", "Choose the swap size yourself", false);
        orfs_ui_help(ui, "The same layout. You set the swap size.");
        orfs_ui_item(ui, "Shell", "A command line, and do it yourself",
                     false);
        orfs_ui_help(ui, "Type exit to return to the installer.");
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_SWAP:
        orfs_ui_begin(ui, ORFS_UI_FORM, "Swap Size",
                      "Swap size in MiB. 1 MiB goes to boot and the "
                      "rest to root.");
        (void)orfs_u32(line, sizeof(line), in->swap_mib);
        orfs_ui_field(ui, "Swap (MiB)", line, 8U, false);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_DISK:
        orfs_ui_begin(ui, ORFS_UI_MENU, "Select a Disk",
                      "Choose the disk to install on.");
        (void)orfs_strcopy(line, sizeof(line), "");
        (void)orfs_u32(line, sizeof(line), in->disk_mib);
        (void)orfs_strcat(line, sizeof(line),
                          " MiB   NVMe namespace 1");
        orfs_ui_item(ui, in->disk, line, false);
        orfs_ui_item(ui, "rescan", "Look for disks again", false);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_SCHEME:
        orfs_ui_begin(ui, ORFS_UI_RADIO, "Partition Scheme",
                      "Choose a partition scheme. Either will boot; "
                      "GPT unless the firmware is old.");
        orfs_ui_item(ui, "GPT", "GUID partition table",
                     orfs_streq(in->scheme, "GPT"));
        orfs_ui_item(ui, "MBR", "Master boot record",
                     orfs_streq(in->scheme, "MBR"));
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_REVIEW:
        orfs_ui_begin(ui, ORFS_UI_MENU, "Review Partitions",
                      "Review the layout. Nothing has been written "
                      "to the disk.");
        (void)orfs_u32(line, sizeof(line), in->disk_mib);
        (void)orfs_strcat(line, sizeof(line), " MiB    ");
        (void)orfs_strcat(line, sizeof(line), in->scheme);
        orfs_ui_item(ui, in->disk, line, false);
        orfs_ui_help(ui, in->by_hand
                     ? "Sizes as you set them."
                     : "Sizes chosen from the size of the disk.");
        orfs_ui_item(ui, "  p1", " 1 MiB    boot", false);
        orfs_ui_help(ui, "Boot partition. Holds the loader.");
        /* Root is whatever is left. Written this way round, the three
         * partitions add up to the disk by construction rather than by
         * three numbers agreeing with each other. */
        (void)orfs_u32_pad(line, sizeof(line),
                           in->disk_mib - in->swap_mib - 1U, 2U, ' ');
        (void)orfs_strcat(line, sizeof(line), " MiB    fat32     /");
        orfs_ui_item(ui, "  p2", line, false);
        orfs_ui_help(ui, "Root: the system, the packages, your "
                         "files.");
        (void)orfs_u32_pad(line, sizeof(line), in->swap_mib, 2U, ' ');
        (void)orfs_strcat(line, sizeof(line), " MiB    swap");
        orfs_ui_item(ui, "  p3", line, false);
        orfs_ui_help(ui, "Not encrypted. Swap encryption is not "
                         "implemented, so paged-out memory is in the "
                         "clear.");
        orfs_ui_buttons(ui, "Finish", "Revert", NULL);
        orfs_ui_select(ui, 2U);
        break;

    case ORFS_STEP_COMMIT:
        orfs_ui_begin(ui, ORFS_UI_MSG, "Confirmation",
                      "Your changes will now be written to the disk. "
                      "Everything already on it will be PERMANENTLY "
                      "ERASED.\n"
                      "\n"
                      "Nothing has been written yet.");
        orfs_ui_buttons(ui, "Commit", "Back", "Revert");
        ui->on_buttons = true;
        ui->chosen = 1U;      /* the safe one is under the cursor */
        break;

    case ORFS_STEP_WRITE:
        orfs_ui_begin(ui, ORFS_UI_GAUGE, "Writing", NULL);
        orfs_ui_buttons(ui, NULL, NULL, NULL);
        orfs_ui_gauge(ui, 0U, "Writing the partition table");
        break;

    case ORFS_STEP_VERIFY:
        orfs_ui_begin(ui, ORFS_UI_GAUGE, "Verifying", NULL);
        orfs_ui_buttons(ui, NULL, NULL, NULL);
        orfs_ui_gauge(ui, 0U, "Hashing the installed tree");
        break;

    case ORFS_STEP_ROOTPW:
        orfs_ui_begin(ui, ORFS_UI_FORM, "Root Password",
                      "Set a password for root. It is stored in "
                      "OPENRFS/LOGIN.DAT as an iterated SHA-256 digest "
                      "with a 128-bit salt, never in the clear.");
        orfs_ui_field(ui, "Password", "", 24U, true);
        orfs_ui_field(ui, "Again", "", 24U, true);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_NETIF:
        orfs_ui_begin(ui, ORFS_UI_MENU, "Network Configuration",
                      "Choose a network interface. The network is not "
                      "required to finish.");
        orfs_ui_item(ui, "em0", "Intel Gigabit Ethernet", false);
        orfs_ui_item(ui, "none", "Do not configure the network", false);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_DHCP:
        orfs_ui_begin(ui, ORFS_UI_MSG, "IPv4",
                      "Configure em0 with DHCP, or by hand?");
        orfs_ui_buttons(ui, "DHCP", "Static", NULL);
        ui->on_buttons = true;
        break;

    case ORFS_STEP_STATIC:
        orfs_ui_begin(ui, ORFS_UI_FORM, "IPv4 Static Configuration",
                      NULL);
        orfs_ui_field(ui, "IP Address", in->ip, 18U, false);
        orfs_ui_field(ui, "Subnet Mask", in->mask, 18U, false);
        orfs_ui_field(ui, "Default Router", in->router, 18U, false);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_DNS:
        orfs_ui_begin(ui, ORFS_UI_FORM, "DNS Configuration", NULL);
        orfs_ui_field(ui, "Nameserver 1", in->dns1, 18U, false);
        orfs_ui_field(ui, "Nameserver 2", in->dns2, 18U, false);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_REGION:
        orfs_ui_begin(ui, ORFS_UI_MENU, "Select a Region",
                      "Choose a region. This sets how the time is "
                      "displayed. The clock itself is monotonic and "
                      "does not trust the RTC.");
        for (uint32_t i = 0U; i < REGIONS; ++i) {
            orfs_ui_item(ui, ZONES[i].region, "", false);
        }
        orfs_ui_item(ui, "UTC", "Keep it in UTC", false);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_ZONE: {
        uint32_t r = region_index(in);

        orfs_ui_begin(ui, ORFS_UI_MENU, "Select a Time Zone", NULL);
        for (uint32_t i = 0U; i < 6U; ++i) {
            if (ZONES[r].zone[i] == NULL) {
                break;
            }
            orfs_ui_item(ui, ZONES[r].zone[i], "", false);
        }
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;
    }

    case ORFS_STEP_STARTUP:
        checklist(in, "Startup",
                  "Choose what runs at boot. All of it can be "
                  "changed later in /ETC/RC.CONF.",
                  START_TAG, START_DESC, in->startup_on, ORFS_STARTUP);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_HARDENING:
        checklist(in, "Hardening",
                  "Restrictions the kernel enforces. Unticking one "
                  "removes the check, not just the message.",
                  HARD_TAG, HARD_DESC, in->harden_on, ORFS_HARDENING);
        orfs_ui_buttons(ui, "OK", "Cancel", NULL);
        break;

    case ORFS_STEP_ADDUSER:
        orfs_ui_begin(ui, ORFS_UI_FORM, "Add a User",
                      "Add a user account for ordinary work. "
                      "Optional.");
        orfs_ui_field(ui, "Username", in->user, 20U, false);
        orfs_ui_field(ui, "Full name", "", 24U, false);
        orfs_ui_field(ui, "Password", "", 20U, true);
        orfs_ui_field(ui, "Again", "", 20U, true);
        orfs_ui_buttons(ui, "OK", "Skip", NULL);
        break;

    case ORFS_STEP_FINAL: {
        uint32_t on;

        orfs_ui_begin(ui, ORFS_UI_MENU, "Final Configuration",
                      "The installation is finished. Choose an item "
                      "to change it, or Exit.");
        orfs_ui_item(ui, "Exit", "Finish and leave the installer", false);
        orfs_ui_help(ui, "Nothing further is written.");

        orfs_ui_item(ui, "Hostname", in->hostname, false);
        orfs_ui_help(ui, "The name of this machine.");

        if (orfs_streq(in->iface, "none")) {
            (void)orfs_strcopy(line, sizeof(line), "not configured");
        } else {
            (void)orfs_strcopy(line, sizeof(line), in->iface);
            (void)orfs_strcat(line, sizeof(line),
                              in->dhcp ? ", DHCP" : ", ");
            if (!in->dhcp) {
                (void)orfs_strcat(line, sizeof(line), in->ip);
            }
        }
        orfs_ui_item(ui, "Network", line, false);
        orfs_ui_help(ui, "Interface and address.");

        orfs_ui_item(ui, "Time", in->zone, false);
        orfs_ui_help(ui, "How the time is displayed.");

        on = 0U;
        for (uint32_t i = 0U; i < ORFS_STARTUP; ++i) {
            on += in->startup_on[i] ? 1U : 0U;
        }
        (void)orfs_u32(line, sizeof(line), on);
        (void)orfs_strcat(line, sizeof(line), " of 5 at boot");
        orfs_ui_item(ui, "Startup", line, false);
        orfs_ui_help(ui, "What runs at boot.");

        on = 0U;
        for (uint32_t i = 0U; i < ORFS_HARDENING; ++i) {
            on += in->harden_on[i] ? 1U : 0U;
        }
        (void)orfs_u32(line, sizeof(line), on);
        (void)orfs_strcat(line, sizeof(line), " of 5 refusals kept");
        orfs_ui_item(ui, "Kernel", line, false);
        orfs_ui_help(ui, on == ORFS_HARDENING
                     ? "All restrictions enabled."
                     : "One or more restrictions were turned off.");

        orfs_ui_item(ui, "Root", in->rootpw[0] != '\0' ? "set" : "NOT SET",
                     false);
        orfs_ui_help(ui, "Cannot be shown. It can only be set "
                         "again.");

        orfs_ui_item(ui, "User",
                     in->user[0] != '\0' ? in->user : "none", false);
        orfs_ui_help(ui, in->user[0] != '\0'
                     ? "A user account exists."
                     : "No account but root.");

        orfs_ui_buttons(ui, "OK", NULL, NULL);
        break;
    }

    case ORFS_STEP_HELP:
        orfs_ui_begin(ui, ORFS_UI_MSG, "Help", help_for(in->help_from));
        orfs_ui_buttons(ui, "OK", NULL, NULL);
        break;

    case ORFS_STEP_DONE:
        orfs_ui_begin(ui, ORFS_UI_MSG, "Complete",
                      "Installation complete. Remove the medium "
                      "before rebooting.\n"
                      "\n"
                      ORFS_SYSTEM " boots to a command line and stops "
                      "at a login prompt.");
        orfs_ui_buttons(ui, "Reboot", "Shell", "Live");
        ui->on_buttons = true;
        break;

    default:
        break;
    }
    phrase(in);
}

static void enter(struct orfs_install *in, enum orfs_step step)
{
    in->step = step;
    if (step == ORFS_STEP_SHELL) {
        handover(in, "The installer is still running. Type exit to "
                      "return to it.", "openrfs#");
        return;
    }
    if (step == ORFS_STEP_REBOOT) {
        handover(in, "Syncing disks... done.",
                 "The operating system has halted. Remove the medium "
                 "and press any key to reboot.");
        in->term.cursor = false;
        return;
    }
    if (step == ORFS_STEP_ABANDONED) {
        handover(in, "Nothing was written to the disk.", "openrfs#");
        return;
    }
    build(in);
    redraw(in);
}

/* ------------------------------------------------------------ the flow */

/* Where escape goes. Every screen has somewhere to go back to, and the
 * first one goes out of the installer altogether. */
static enum orfs_step back_from(enum orfs_step step)
{
    switch (step) {
    case ORFS_STEP_KEYMAP:      return ORFS_STEP_WELCOME;
    case ORFS_STEP_WELCOME:     return ORFS_STEP_ABANDONED;
    case ORFS_STEP_HOSTNAME:    return ORFS_STEP_WELCOME;
    case ORFS_STEP_COMPONENTS:  return ORFS_STEP_HOSTNAME;
    case ORFS_STEP_METHOD:      return ORFS_STEP_COMPONENTS;
    case ORFS_STEP_DISK:        return ORFS_STEP_METHOD;
    case ORFS_STEP_SWAP:        return ORFS_STEP_DISK;
    case ORFS_STEP_SCHEME:      return ORFS_STEP_DISK;
    case ORFS_STEP_REVIEW:      return ORFS_STEP_SCHEME;
    case ORFS_STEP_COMMIT:      return ORFS_STEP_REVIEW;
    case ORFS_STEP_ROOTPW:      return ORFS_STEP_ROOTPW;
    case ORFS_STEP_NETIF:       return ORFS_STEP_ROOTPW;
    case ORFS_STEP_DHCP:        return ORFS_STEP_NETIF;
    case ORFS_STEP_STATIC:      return ORFS_STEP_DHCP;
    case ORFS_STEP_DNS:         return ORFS_STEP_DHCP;
    case ORFS_STEP_REGION:      return ORFS_STEP_DNS;
    case ORFS_STEP_ZONE:        return ORFS_STEP_REGION;
    case ORFS_STEP_STARTUP:     return ORFS_STEP_REGION;
    case ORFS_STEP_HARDENING:   return ORFS_STEP_STARTUP;
    case ORFS_STEP_ADDUSER:     return ORFS_STEP_HARDENING;
    case ORFS_STEP_FINAL:       return ORFS_STEP_FINAL;
    case ORFS_STEP_DONE:        return ORFS_STEP_FINAL;
    default:                    return ORFS_STEP_WELCOME;
    }
}

/*
 * What a finished screen does next. Everything that reads an answer out
 * of the widget does it here, once, at the moment the screen is left -
 * so there is exactly one place where a question becomes a setting.
 */
static void accept(struct orfs_install *in)
{
    struct orfs_ui *ui = &in->ui;
    const char *pressed = orfs_ui_button(ui);
    char line[ORFS_UI_DESC];

    /* Before anything moves: the question and what it was answered
     * with, onto the transcript, in the words they were on the screen
     * in. */
    log_answer(in);

    switch (in->step) {
    case ORFS_STEP_HELP:
        enter(in, in->help_from);
        return;

    case ORFS_STEP_KEYMAP:
        (void)orfs_strcopy(in->keymap, sizeof(in->keymap),
                           orfs_ui_tag(ui));
        enter(in, ORFS_STEP_WELCOME);
        return;

    case ORFS_STEP_WELCOME:
        if (orfs_streq(pressed, "Shell")) {
            enter(in, ORFS_STEP_SHELL);
        } else if (orfs_streq(pressed, "Live")) {
            enter(in, ORFS_STEP_ABANDONED);
        } else {
            enter(in, ORFS_STEP_HOSTNAME);
        }
        return;

    case ORFS_STEP_HOSTNAME:
        (void)orfs_strcopy(in->hostname, sizeof(in->hostname),
                           orfs_ui_value(ui, 0U));
        if (in->hostname[0] == '\0') {
            (void)orfs_strcopy(in->hostname, sizeof(in->hostname),
                               "openrfs");
        }
        enter(in, ORFS_STEP_COMPONENTS);
        return;

    case ORFS_STEP_COMPONENTS:
        save_flags(in, in->comp_on, ORFS_COMPONENTS);
        enter(in, ORFS_STEP_METHOD);
        return;

    case ORFS_STEP_METHOD:
        if (orfs_streq(orfs_ui_tag(ui), "Shell")) {
            enter(in, ORFS_STEP_SHELL);
            return;
        }
        in->by_hand = orfs_streq(orfs_ui_tag(ui), "Edit");
        enter(in, ORFS_STEP_DISK);
        return;

    case ORFS_STEP_SWAP: {
        /* A swap that leaves no room for the system is not a layout,
         * it is a way of losing the disk. One MiB for the loader, and
         * at least sixteen for everything else. */
        uint32_t want = orfs_atou(orfs_ui_value(ui, 0U));
        uint32_t most = in->disk_mib > 17U ? in->disk_mib - 17U : 0U;

        if (want == 0U || want > most) {
            orfs_ui_begin(ui, ORFS_UI_FORM, "Swap Size",
                          "That leaves nothing for the system. Swap has "
                          "to be at least 1 MiB and small enough to "
                          "leave 16 MiB of root behind it.");
            (void)orfs_u32(line, sizeof(line), in->swap_mib);
            orfs_ui_field(ui, "Swap (MiB)", line, 8U, false);
            orfs_ui_buttons(ui, "OK", "Cancel", NULL);
            redraw(in);
            return;
        }
        in->swap_mib = want;
        enter(in, ORFS_STEP_SCHEME);
        return;
    }

    case ORFS_STEP_DISK:
        /* Rescanning is a thing you do on this screen, not a way off
         * it. Pressing it has to leave you here or it is a lie. */
        if (orfs_streq(orfs_ui_tag(ui), "rescan")) {
            enter(in, ORFS_STEP_DISK);
            return;
        }
        (void)orfs_strcopy(in->disk, sizeof(in->disk), orfs_ui_tag(ui));
        enter(in, in->by_hand ? ORFS_STEP_SWAP : ORFS_STEP_SCHEME);
        return;

    case ORFS_STEP_SCHEME:
        for (uint32_t i = 0U; i < ui->items; ++i) {
            if (ui->item[i].on) {
                (void)orfs_strcopy(in->scheme, sizeof(in->scheme),
                                   ui->item[i].tag);
            }
        }
        enter(in, ORFS_STEP_REVIEW);
        return;

    case ORFS_STEP_REVIEW:
        if (orfs_streq(pressed, "Revert")) {
            enter(in, ORFS_STEP_METHOD);
        } else {
            enter(in, ORFS_STEP_COMMIT);
        }
        return;

    case ORFS_STEP_COMMIT:
        if (orfs_streq(pressed, "Commit")) {
            enter(in, ORFS_STEP_WRITE);
        } else if (orfs_streq(pressed, "Revert")) {
            enter(in, ORFS_STEP_ABANDONED);
        } else {
            enter(in, ORFS_STEP_REVIEW);
        }
        return;

    case ORFS_STEP_ROOTPW:
        /* Two boxes that do not match is the whole reason there are
         * two boxes. Say so and ask again rather than taking one. */
        if (!orfs_streq(orfs_ui_value(ui, 0U), orfs_ui_value(ui, 1U))) {
            orfs_ui_begin(ui, ORFS_UI_FORM, "Root Password",
                          "The passwords did not match. Nothing has "
                          "been set. Try again.");
            orfs_ui_field(ui, "Password", "", 24U, true);
            orfs_ui_field(ui, "Again", "", 24U, true);
            orfs_ui_buttons(ui, "OK", "Cancel", NULL);
            redraw(in);
            return;
        }
        (void)orfs_strcopy(in->rootpw, sizeof(in->rootpw),
                           orfs_ui_value(ui, 0U));
        enter(in, ORFS_STEP_NETIF);
        return;

    case ORFS_STEP_NETIF:
        (void)orfs_strcopy(in->iface, sizeof(in->iface), orfs_ui_tag(ui));
        enter(in, orfs_streq(in->iface, "none") ? ORFS_STEP_REGION
                                                : ORFS_STEP_DHCP);
        return;

    case ORFS_STEP_DHCP:
        in->dhcp = orfs_streq(pressed, "DHCP");
        enter(in, in->dhcp ? ORFS_STEP_DNS : ORFS_STEP_STATIC);
        return;

    case ORFS_STEP_STATIC:
        (void)orfs_strcopy(in->ip, sizeof(in->ip), orfs_ui_value(ui, 0U));
        (void)orfs_strcopy(in->mask, sizeof(in->mask),
                           orfs_ui_value(ui, 1U));
        (void)orfs_strcopy(in->router, sizeof(in->router),
                           orfs_ui_value(ui, 2U));
        enter(in, ORFS_STEP_DNS);
        return;

    case ORFS_STEP_DNS:
        (void)orfs_strcopy(in->dns1, sizeof(in->dns1),
                           orfs_ui_value(ui, 0U));
        (void)orfs_strcopy(in->dns2, sizeof(in->dns2),
                           orfs_ui_value(ui, 1U));
        enter(in, ORFS_STEP_REGION);
        return;

    case ORFS_STEP_REGION:
        (void)orfs_strcopy(in->region, sizeof(in->region),
                           orfs_ui_tag(ui));
        if (orfs_streq(in->region, "UTC")) {
            (void)orfs_strcopy(in->zone, sizeof(in->zone), "UTC");
            enter(in, ORFS_STEP_STARTUP);
        } else {
            enter(in, ORFS_STEP_ZONE);
        }
        return;

    case ORFS_STEP_ZONE:
        (void)orfs_strcopy(in->zone, sizeof(in->zone), in->region);
        (void)orfs_strcat(in->zone, sizeof(in->zone), "/");
        (void)orfs_strcat(in->zone, sizeof(in->zone), orfs_ui_tag(ui));
        enter(in, ORFS_STEP_STARTUP);
        return;

    case ORFS_STEP_STARTUP:
        save_flags(in, in->startup_on, ORFS_STARTUP);
        enter(in, ORFS_STEP_HARDENING);
        return;

    case ORFS_STEP_HARDENING:
        save_flags(in, in->harden_on, ORFS_HARDENING);
        enter(in, ORFS_STEP_ADDUSER);
        return;

    case ORFS_STEP_ADDUSER:
        if (orfs_streq(pressed, "Skip")) {
            enter(in, ORFS_STEP_FINAL);
            return;
        }
        if (!orfs_streq(orfs_ui_value(ui, 2U), orfs_ui_value(ui, 3U))) {
            orfs_ui_begin(ui, ORFS_UI_FORM, "Add a User",
                          "The passwords did not match. The account "
                          "has not been created.");
            orfs_ui_field(ui, "Username", orfs_ui_value(ui, 0U), 20U,
                          false);
            orfs_ui_field(ui, "Full name", "", 24U, false);
            orfs_ui_field(ui, "Password", "", 20U, true);
            orfs_ui_field(ui, "Again", "", 20U, true);
            orfs_ui_buttons(ui, "OK", "Skip", NULL);
            redraw(in);
            return;
        }
        (void)orfs_strcopy(in->user, sizeof(in->user),
                           orfs_ui_value(ui, 0U));
        (void)orfs_strcopy(in->userpw, sizeof(in->userpw),
                           orfs_ui_value(ui, 2U));
        enter(in, ORFS_STEP_FINAL);
        return;

    case ORFS_STEP_FINAL: {
        const char *what = orfs_ui_tag(ui);

        if (orfs_streq(what, "Hostname")) {
            enter(in, ORFS_STEP_HOSTNAME);
        } else if (orfs_streq(what, "Network")) {
            enter(in, ORFS_STEP_NETIF);
        } else if (orfs_streq(what, "Time")) {
            enter(in, ORFS_STEP_REGION);
        } else if (orfs_streq(what, "Startup")) {
            enter(in, ORFS_STEP_STARTUP);
        } else if (orfs_streq(what, "Kernel")) {
            enter(in, ORFS_STEP_HARDENING);
        } else if (orfs_streq(what, "Root")) {
            enter(in, ORFS_STEP_ROOTPW);
        } else if (orfs_streq(what, "User")) {
            enter(in, ORFS_STEP_ADDUSER);
        } else {
            enter(in, ORFS_STEP_DONE);
        }
        return;
    }

    case ORFS_STEP_DONE:
        if (orfs_streq(pressed, "Shell")) {
            enter(in, ORFS_STEP_SHELL);
        } else if (orfs_streq(pressed, "Live")) {
            enter(in, ORFS_STEP_ABANDONED);
        } else {
            enter(in, ORFS_STEP_REBOOT);
        }
        return;

    default:
        return;
    }
}

/* --------------------------------------------------------------- entry */

void orfs_install_begin(struct orfs_install *in)
{
    if (in == NULL) {
        return;
    }
    orfs_term_reset(&in->term);
    /*
     * WHAT IS ALREADY ON THE SCREEN when the installer starts talking.
     * The boot lines above it are the kernel's, and the paragraph is
     * install.sub's own - it is the only place the installer explains
     * itself, and it explains exactly two things: how to get out and
     * what the brackets mean.
     */
    in->logged = 0U;
    say(in, "root on rd0a swap on rd0b dump on rd0b");
    say(in, "erase ^?, werase ^W, kill ^U, intr ^C, status ^T");
    say(in, "");
    say(in, "At any prompt except password prompts you can escape to a "
            "shell by");
    say(in, "typing '!'. Default answers are shown in []'s and are "
            "selected by");
    say(in, "pressing RETURN.  You can exit this program at any time by "
            "pressing");
    say(in, "Control-C, but this can leave your system in an "
            "inconsistent state.");
    say(in, "");
    (void)orfs_strcopy(in->keymap, sizeof(in->keymap), "us");
    (void)orfs_strcopy(in->hostname, sizeof(in->hostname), "openrfs");
    (void)orfs_strcopy(in->disk, sizeof(in->disk), "nvme0");
    in->disk_mib = 64U;
    (void)orfs_strcopy(in->scheme, sizeof(in->scheme), "GPT");
    in->rootpw[0] = '\0';
    in->user[0] = '\0';
    in->userpw[0] = '\0';
    (void)orfs_strcopy(in->ip, sizeof(in->ip), "192.168.1.40");
    (void)orfs_strcopy(in->mask, sizeof(in->mask), "255.255.255.0");
    (void)orfs_strcopy(in->router, sizeof(in->router), "192.168.1.1");
    (void)orfs_strcopy(in->dns1, sizeof(in->dns1), "9.9.9.9");
    (void)orfs_strcopy(in->dns2, sizeof(in->dns2), "1.1.1.1");
    (void)orfs_strcopy(in->iface, sizeof(in->iface), "em0");
    (void)orfs_strcopy(in->region, sizeof(in->region), "Asia");
    (void)orfs_strcopy(in->zone, sizeof(in->zone), "UTC");
    in->dhcp = true;
    in->swap_mib = 16U;
    in->by_hand = false;
    in->help_from = ORFS_STEP_WELCOME;
    in->written = false;
    in->verified = false;
    for (uint32_t i = 0U; i < ORFS_COMPONENTS; ++i) {
        in->comp_on[i] = COMP_ON[i];
    }
    for (uint32_t i = 0U; i < ORFS_STARTUP; ++i) {
        in->startup_on[i] = START_ON[i];
    }
    for (uint32_t i = 0U; i < ORFS_HARDENING; ++i) {
        in->harden_on[i] = HARD_ON[i];
    }
    enter(in, ORFS_STEP_KEYMAP);
}

void orfs_install_key(struct orfs_install *in, int key)
{
    enum orfs_ui_result r;

    if (in == NULL) {
        return;
    }
    /* A gauge is not waiting for you. Keys pressed at one go nowhere,
     * There is nothing to answer yet. */
    if (in->step == ORFS_STEP_WRITE || in->step == ORFS_STEP_VERIFY) {
        return;
    }
    if (in->step == ORFS_STEP_SHELL || in->step == ORFS_STEP_REBOOT
        || in->step == ORFS_STEP_ABANDONED) {
        return;
    }
    r = orfs_ui_key(&in->ui, key);
    if (r == ORFS_UI_ACCEPT) {
        accept(in);
        return;
    }
    if (r == ORFS_UI_REJECT) {
        /* Leaving help puts you back where you pressed F1, not one
         * screen further back from it. */
        enter(in, in->step == ORFS_STEP_HELP ? in->help_from
                                             : back_from(in->step));
        return;
    }
    if (r == ORFS_UI_ASKED && in->step != ORFS_STEP_HELP) {
        in->help_from = in->step;
        enter(in, ORFS_STEP_HELP);
        return;
    }
    redraw(in);
}

void orfs_install_type(struct orfs_install *in, const char *text)
{
    for (uint32_t at = 0U; text != NULL && text[at] != '\0'; ++at) {
        orfs_install_key(in, (int)(unsigned char)text[at]);
    }
}

void orfs_install_tick(struct orfs_install *in)
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
    if (in->step != ORFS_STEP_WRITE && in->step != ORFS_STEP_VERIFY) {
        return;
    }
    pc = in->ui.percent + 5U;
    if (pc >= 100U) {
        if (in->step == ORFS_STEP_WRITE) {
            in->written = true;
            enter(in, ORFS_STEP_VERIFY);
        } else {
            in->verified = true;
            enter(in, ORFS_STEP_ROOTPW);
        }
        return;
    }
    if (in->step == ORFS_STEP_WRITE) {
        orfs_ui_gauge(&in->ui, pc, WRITING[(pc * 5U) / 100U]);
    } else {
        orfs_ui_gauge(&in->ui, pc, CHECKING[(pc * 4U) / 100U]);
    }
    redraw(in);
}

void orfs_install_settle(struct orfs_install *in)
{
    enum orfs_step started;
    uint32_t guard = 0U;

    if (in == NULL) {
        return;
    }
    started = in->step;
    while (in->step == started && guard < 64U) {
        orfs_install_tick(in);
        ++guard;
    }
}

const char *orfs_install_step_name(const struct orfs_install *in)
{
    static const char *const NAMES[ORFS_INSTALL_STEPS] = {
        "keymap", "welcome", "hostname", "components", "method",
        "disk", "swap", "scheme", "review", "commit", "write", "verify",
        "rootpw", "netif", "dhcp", "static", "dns", "region", "zone",
        "startup", "hardening", "adduser", "final", "done", "shell",
        "reboot", "abandoned", "help"
    };

    if (in == NULL || (uint32_t)in->step >= ORFS_INSTALL_STEPS) {
        return "?";
    }
    return NAMES[in->step];
}

bool orfs_install_selected(const struct orfs_install *in,
                           enum orfs_step step, const char *tag)
{
    const char *const *names;
    const bool *flags;
    uint32_t count;

    if (in == NULL) {
        return false;
    }
    if (step == ORFS_STEP_COMPONENTS) {
        names = COMP_TAG;
        flags = in->comp_on;
        count = ORFS_COMPONENTS;
    } else if (step == ORFS_STEP_STARTUP) {
        names = START_TAG;
        flags = in->startup_on;
        count = ORFS_STARTUP;
    } else if (step == ORFS_STEP_HARDENING) {
        names = HARD_TAG;
        flags = in->harden_on;
        count = ORFS_HARDENING;
    } else {
        return false;
    }
    for (uint32_t i = 0U; i < count; ++i) {
        if (orfs_streq(names[i], tag)) {
            return flags[i];
        }
    }
    return false;
}

void orfs_install_row(const struct orfs_install *in, uint32_t row,
                      char *out, uint32_t capacity)
{
    uint32_t n = 0U;

    if (out == NULL || capacity == 0U) {
        return;
    }
    out[0] = '\0';
    if (in == NULL || row >= ORFS_ROWS) {
        return;
    }
    for (uint32_t c = 0U; c < ORFS_COLS && n + 1U < capacity; ++c) {
        out[n++] = orfs_term_at(&in->term, row, c);
    }
    while (n != 0U && out[n - 1U] == ' ') {
        --n;
    }
    out[n] = '\0';
}
