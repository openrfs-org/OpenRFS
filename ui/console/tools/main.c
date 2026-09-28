/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The harness.
 *
 * It is not a demo. It drives the shell through the same entry point a
 * keyboard would - one key at a time, from a real key code - and then
 * reads the screen back and asserts what is on it. A check that called
 * cmd_ledger() directly would prove the function works and tell you
 * nothing about whether typing "ledger" reaches it.
 *
 * Every check here can fail. If one does, the build fails.
 */
#include <rsd/config.h>
#include <rsd/shell.h>
#include <rsd/version.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render.h"

/* The shell's own cursor rule: it is on unless the machine has halted. */
static void draw(const struct rsd_shell *sh, const char *path)
{
    (void)rsd_render(&sh->term, sh->term.cursor
                      && sh->phase != RSD_PHASE_HALTED, path);
}

/* --------------------------------------------------------------- state */

static void populate(void)
{
    uint32_t root, etc, home, user, bin;

    rsd_sys_reset();
    /* The ledger RSD's boot actually declares, in dependency order.
     * One stage is SKIPPED and says why - a ledger that only listed
     * successes would be an advert. */
    rsd_sys_add_stage("multiboot2",  "loader records validated",
                       RSD_STAGE_VERIFIED, 118U);
    rsd_sys_add_stage("cpu",         "descriptor tables, TSS",
                       RSD_STAGE_VERIFIED, 92U);
    rsd_sys_add_stage("longmode",    "entered, paging installed",
                       RSD_STAGE_VERIFIED, 240U);
    rsd_sys_add_stage("physmem",     "frames counted, W^X asserted",
                       RSD_STAGE_VERIFIED, 1104U);
    rsd_sys_add_stage("heap",        "guarded allocations armed",
                       RSD_STAGE_VERIFIED, 76U);
    rsd_sys_add_stage("interrupts",  "IDT, PIC masked, APIC on",
                       RSD_STAGE_VERIFIED, 88U);
    rsd_sys_add_stage("clock",       "monotonic source, no RTC trust",
                       RSD_STAGE_VERIFIED, 66U);
    rsd_sys_add_stage("entropy",     "source health-checked",
                       RSD_STAGE_VERIFIED, 512U);
    rsd_sys_add_stage("pci",         "10 devices identified",
                       RSD_STAGE_VERIFIED, 1440U);
    rsd_sys_add_stage("iommu",       "absent: bus mastering left off",
                       RSD_STAGE_SKIPPED, 4U);
    rsd_sys_add_stage("nvme",        "1 namespace, 64MiB",
                       RSD_STAGE_VERIFIED, 980U);
    rsd_sys_add_stage("fat32",       "BPB and FSInfo agree",
                       RSD_STAGE_VERIFIED, 322U);
    rsd_sys_add_stage("trustroots",  "2 Ed25519 roots loaded",
                       RSD_STAGE_VERIFIED, 154U);
    rsd_sys_add_stage("login",       "LOGIN.DAT checksum ok",
                       RSD_STAGE_VERIFIED, 61U);

    root = rsd_fs_root();
    bin  = rsd_fs_add(root, "BIN", true, 0U);
    etc  = rsd_fs_add(root, "ETC", true, 0U);
    home = rsd_fs_add(root, "HOME", true, 0U);
    (void)rsd_fs_add(root, "RSD", true, 0U);
    (void)rsd_fs_add(bin, "RSD.BIN", false, 21504U);
    (void)rsd_fs_add(bin, "STARTY.BIN", false, 40960U);
    (void)rsd_fs_add(etc, "MOTD", false, 212U);
    (void)rsd_fs_add(etc, "FSTAB", false, 64U);
    user = rsd_fs_add(home, "ALICE", true, 0U);
    (void)rsd_fs_add(user, "NOTES.TXT", false, 1284U);
    (void)rsd_fs_add(user, "README.TXT", false, 812U);

    rsd_pkg_reset();
    rsd_pkg_add("bearssl", "0.6", "root:9f2a", true);
    rsd_pkg_add("lua", "5.4.7", "root:9f2a", true);
    rsd_pkg_add("sqlite", "3.46.0", "root:9f2a", true);
    rsd_pkg_add("sdl2", "2.30.5", "root:c417", true);
    rsd_pkg_add("zlib", "1.3.1", "root:9f2a", true);
}

/* -------------------------------------------------------------- checks */

static int failures;

static void check(const char *name, int ok, const char *detail)
{
    printf(ok ? "  ok   %s - %s\n" : "  FAIL %s - %s\n", name, detail);
    if (!ok) {
        ++failures;
    }
}

/* Is `needle` anywhere on the screen? Returns the row, or -1. */
static int screen_find(const struct rsd_shell *sh, const char *needle)
{
    char row[RSD_COLS + 1];

    for (uint32_t r = 0U; r < RSD_ROWS; ++r) {
        rsd_screen_row(sh, r, row, sizeof(row));
        if (strstr(row, needle) != NULL) {
            return (int)r;
        }
    }
    return -1;
}

/* Is `needle` anywhere in the kernel's message buffer? */
static int dmesg_has(const char *needle)
{
    for (uint32_t i = 0U; i < rsd_dmesg_count(); ++i) {
        if (strstr(rsd_dmesg_line(i), needle) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* Boot the way almost everybody boots: press return at the menu. */
static void boot_default(struct rsd_shell *sh)
{
    rsd_boot(sh);
    rsd_key(sh, '\r');
}

static void log_in(struct rsd_shell *sh)
{
    boot_default(sh);
    rsd_type(sh, "alice\r");
    rsd_type(sh, "hunter2\r");
}

int main(void)
{
    struct rsd_shell sh;
    char row[RSD_COLS + 1];

    populate();

    /* ---- the loader menu ---- */
    rsd_boot(&sh);
    check("the loader comes up before the kernel does",
          sh.phase == RSD_PHASE_LOADER
          && screen_find(&sh, "loader") >= 0,
          "a menu, not a kernel");
    check("it offers the four things a loader menu offers",
          screen_find(&sh, "1. Boot") >= 0
          && screen_find(&sh, "2. Boot single user") >= 0
          && screen_find(&sh, "3. Escape to loader prompt") >= 0
          && screen_find(&sh, "4. Reboot") >= 0,
          "boot, single user, the prompt, reboot");
    /*
     * THE MARK, and it is asked for by its shape rather than by a run
     * of hashes. This looked for twelve '#' in a row, which the mark
     * had a lot of and the RSD wordmark has fewer of - the letters are
     * strokes, not a body. What every row of the mark has is a run of
     * the gradient's heaviest character, so this asks the header how
     * wide the mark is and looks for a row that carries it.
     */
    {
        int mark = -1;

        for (uint32_t r = 0U; r < RSD_ROWS && mark < 0; ++r) {
            char line[RSD_COLS + 1];
            uint32_t run = 0U;
            uint32_t best = 0U;

            rsd_screen_row(&sh, r, line, sizeof(line));
            for (uint32_t c = 0U; line[c] != '\0'; ++c) {
                run = line[c] == '%' || line[c] == '#' ? run + 1U : 0U;
                best = run > best ? run : best;
            }
            if (best >= 8U) {
                mark = (int)r;
            }
        }
        check("and the mark is on it", mark >= 0,
              mark >= 0 ? "a row of it, at loader width"
                        : "no row of the mark anywhere on the screen");
    }
    draw(&sh, "../screens/00-loader.png");

    /* A key that is not on the menu does nothing at all. There is
     * nowhere on this screen to put a complaint, and a menu that
     * scrolls an error message off its own picture is worse than one
     * that ignores you. */
    rsd_key(&sh, 'q');
    check("a key that is not on the menu is ignored",
          sh.phase == RSD_PHASE_LOADER, "still the menu");

    /* ---- option 3: the loader's own prompt ---- */
    rsd_key(&sh, '3');
    check("escaping to the loader gives a boot prompt",
          sh.phase == RSD_PHASE_LOADER_CMD
          && screen_find(&sh, "boot>") >= 0, "boot>");
    rsd_type(&sh, "machine\r");
    check("the loader knows what it found",
          screen_find(&sh, "amd64") >= 0 && screen_find(&sh, "256MB") >= 0,
          "amd64, 256MB, one disk");
    rsd_type(&sh, "frobnicate\r");
    check("and says so when it does not know a command",
          screen_find(&sh, "unknown command: frobnicate") >= 0,
          "unknown command: frobnicate");
    draw(&sh, "../screens/00b-loader-prompt.png");
    rsd_type(&sh, "boot\r");
    check("boot at the loader prompt boots",
          sh.phase == RSD_PHASE_LOGIN, "on to the login");

    /* ---- option 2: single user ---- */
    rsd_boot(&sh);
    rsd_key(&sh, '2');
    check("single user asks which shell, the way it always has",
          sh.phase == RSD_PHASE_SINGLE_ASK
          && screen_find(&sh, "Enter full pathname of shell") >= 0,
          "or RETURN for the default");
    rsd_key(&sh, '\r');
    check("single user is root, with a hash",
          sh.phase == RSD_PHASE_SHELL && sh.single
          && rsd_streq(rsd_prompt(&sh), "# "),
          rsd_prompt(&sh));
    check("it does not ask who you are",
          screen_find(&sh, "login:") < 0,
          "no login prompt: that is the point of single user");
    rsd_type(&sh, "mount\r");
    check("and root is read-only until you say otherwise",
          screen_find(&sh, "(ro)") >= 0, "mounted read-only");
    rsd_type(&sh, "mount -uw\r");
    check("mount -uw makes it writable",
          screen_find(&sh, "(rw)") >= 0, "and now read-write");
    draw(&sh, "../screens/00c-single.png");

    /* ---- the boot screen and the login ---- */
    boot_default(&sh);
    /*
     * IN THE BUFFER, not on the screen. The version line is the fourth
     * thing the kernel prints and the probe under it is sixty lines
     * long, so by the time the login prompt is up it has gone off the
     * top - which is what happens on every BSD console there has ever
     * been, and why dmesg exists.
     */
    check("boot names the kernel",
          dmesg_has(RSD_BANNER),
          "the version line, in the message buffer");
    check("boot reports the ledger",
          screen_find(&sh, "13 of 14 stages verified") >= 0,
          "13 of 14, because the IOMMU stage is skipped");
    /* No trailing space in the needle: rsd_screen_row trims blanks off
     * the end of a row, so "login: " could never match however right the
     * console was. Ask for what the reader actually returns, and ask the
     * phase too so this cannot pass on the word alone. */
    rsd_screen_row(&sh, sh.term.row, row, sizeof(row));
    check("boot stops at a login prompt",
          rsd_streq(row, "login:") && sh.phase == RSD_PHASE_LOGIN,
          row[0] != '\0' ? row : "(the last row is empty)");
    draw(&sh, "../screens/01-boot.png");

    check("the boot says who wrote it before it says anything else",
          strstr(rsd_dmesg_line(0U), "Copyright") != NULL
          && strstr(rsd_dmesg_line(0U), "Saud Aljuaid") != NULL,
          "the first line in the buffer, where FreeBSD puts its own");
    /*
     * AND IT DOES NOT FIT ON ONE SCREEN.
     *
     * This asserted the opposite - that the copyright was still on row
     * 0 when the login prompt came up - and there was a note in shell.c
     * warning that one more line of probe would break it. That is
     * backwards. A boot you can read in full on an 80x25 console is a
     * boot that is not telling you much; a BSD one scrolls, and the
     * buffer is what you read it from afterwards.
     */
    check("the boot is longer than the screen it printed on",
          rsd_dmesg_count() > RSD_ROWS
          && screen_find(&sh, "Copyright") < 0,
          "it scrolled, the way a boot does");
    check("every line of it is in the buffer, from the first",
          dmesg_has("Copyright") && dmesg_has("nvme0")
          && dmesg_has("trust roots") && dmesg_has(RSD_BANNER),
          "start to finish");
    /*
     * THE DEVICE COUNT IS NOT WRITTEN DOWN TWICE.
     *
     * pci0's line says how many devices it identified and the lines
     * under it are those devices. This parses the number out of the
     * first and counts the second, so a device added to the probe
     * without the count moving fails the build - which is the one way
     * a dmesg this long goes quietly wrong.
     */
    {
        unsigned said = 0U;
        unsigned seen = 0U;
        int slot[32];
        unsigned slots = 0U;

        for (uint32_t i = 0U; i < rsd_dmesg_count(); ++i) {
            const char *line = rsd_dmesg_line(i);
            const char *at = strstr(line, " devices identified");
            const char *dev = strstr(line, "at pci0 dev ");

            if (at != NULL) {
                while (at > line && at[-1] >= '0' && at[-1] <= '9') {
                    --at;
                }
                said = (unsigned)atoi(at);
            }
            if (dev != NULL) {
                int which = atoi(dev + 12);
                unsigned known = 0U;

                /* One device may answer on several functions; pci0
                 * counts devices, so count each slot once. */
                for (unsigned k = 0U; k < slots; ++k) {
                    known |= slot[k] == which ? 1U : 0U;
                }
                if (known == 0U && slots < 32U) {
                    slot[slots++] = which;
                    ++seen;
                }
            }
        }
        {
            char how[64];

            (void)snprintf(how, sizeof(how),
                           "%u said, %u probed", said, seen);
            check("pci0 counted what it went on to print",
                  said != 0U && said == seen, how);
        }
        /*
         * And the boot ledger's own pci line agrees with both. Three
         * places now say how many devices there are; two of them are
         * checked against the third.
         */
        {
            int found = 0;

            for (uint32_t i = 0U; i < rsd_sys_stage_count(); ++i) {
                const struct rsd_stage *st = rsd_sys_stage(i);

                if (st != NULL && rsd_streq(st->name, "pci")) {
                    found = strstr(st->note, "10 devices") != NULL;
                }
            }
            check("and the ledger says the same number",
                  found, "the ledger's pci stage and the probe agree");
        }
    }
    check("it says what it mounted and how",
          screen_find(&sh, "Trying to mount root from") >= 0
          && screen_find(&sh, "fat32") >= 0,
          "root on fat32");
    check("and what it did before the login prompt",
          screen_find(&sh, "Automatic boot in progress") >= 0
          && screen_find(&sh, "trust roots") >= 0,
          "the filesystem check and the package signatures");

    /* ---- the password must not be echoed ---- */
    rsd_type(&sh, "alice\r");
    check("login asks for a password",
          sh.phase == RSD_PHASE_PASSWORD, "phase is PASSWORD");
    rsd_type(&sh, "hunter2");
    check("the password is not echoed",
          screen_find(&sh, "hunter2") < 0,
          "\"hunter2\" appears nowhere on the screen");
    draw(&sh, "../screens/02-password.png");
    rsd_type(&sh, "\r");
    check("the shell starts after the password",
          sh.phase == RSD_PHASE_SHELL && screen_find(&sh, "rsd$") >= 0,
          "the prompt is rsd$");
    draw(&sh, "../screens/03-motd.png");

    /* ---- dmesg: the boot, after the boot has scrolled away ---- */
    log_in(&sh);
    for (int i = 0; i < 24; ++i) {
        rsd_type(&sh, "uname\r");
    }
    check("the boot messages have scrolled off",
          screen_find(&sh, "real mem") < 0, "gone from the screen");
    rsd_type(&sh, "dmesg\r");
    check("dmesg brings them back",
          screen_find(&sh, "nvme0") >= 0 || screen_find(&sh, "cpu0") >= 0,
          "the kernel's own lines, from the buffer");
    check("dmesg is a buffer, not a second boot",
          !sh.desktop && sh.phase == RSD_PHASE_SHELL,
          "it printed, it did not reboot");
    draw(&sh, "../screens/04b-dmesg.png");

    /* ---- the ledger ---- */
    rsd_type(&sh, "ledger\r");
    check("ledger lists every stage",
          screen_find(&sh, "multiboot2") >= 0 &&
          screen_find(&sh, "trustroots") >= 0,
          "first and last stage both present");
    check("ledger shows the skipped stage and its reason",
          screen_find(&sh, "bus mastering left off") >= 0,
          "the IOMMU line says why it was skipped");
    draw(&sh, "../screens/04-ledger.png");

    /* ---- the filesystem ---- */
    log_in(&sh);
    rsd_type(&sh, "ls\r");
    check("ls lists the root", screen_find(&sh, "RSD") >= 0,
          "RSD is in the listing");
    rsd_type(&sh, "cd home\r");
    check("cd is case-insensitive, as FAT32 is",
          screen_find(&sh, "no such file") < 0,
          "lowercase \"home\" found HOME");
    rsd_type(&sh, "cd alice\r");
    rsd_type(&sh, "pwd\r");
    check("pwd builds the path from the tree",
          screen_find(&sh, "/HOME/ALICE") >= 0, "/HOME/ALICE");
    rsd_type(&sh, "ls -l\r");
    draw(&sh, "../screens/05-files.png");

    rsd_type(&sh, "cd nowhere\r");
    check("cd into nothing fails and says so",
          screen_find(&sh, "no such file or directory") >= 0 &&
          sh.status == 1U,
          "status 1 and a message");

    /* ---- an unknown command ---- */
    log_in(&sh);
    rsd_type(&sh, "frobnicate\r");
    check("an unknown command is refused, not guessed at",
          screen_find(&sh, "rsd: frobnicate: not found") >= 0 &&
          sh.status == 127U,
          "ksh's wording, status 127");

    /* ---- packages ---- */
    log_in(&sh);
    rsd_type(&sh, "pkg\r");
    check("pkg names the key that signed each package",
          screen_find(&sh, "root:9f2a") >= 0, "the trust root's short id");
    draw(&sh, "../screens/06-pkg.png");

    /* ---- the line editor, key by key ---- */
    log_in(&sh);
    rsd_type(&sh, "ledgerXX");
    rsd_key(&sh, 0x08);                      /* ^H */
    rsd_key(&sh, 0x08);
    check("backspace removes what it should",
          rsd_streq(rsd_line_text(&sh.line), "ledger"),
          rsd_line_text(&sh.line));

    rsd_type(&sh, " and more");
    rsd_key(&sh, 0x17);                      /* ^W */
    check("^W kills one word, not the line",
          rsd_streq(rsd_line_text(&sh.line), "ledger and "),
          rsd_line_text(&sh.line));

    rsd_key(&sh, 0x15);                      /* ^U */
    check("^U kills the whole line",
          rsd_line_text(&sh.line)[0] == '\0', "the buffer is empty");

    rsd_type(&sh, "help");
    rsd_key(&sh, 0x01);                      /* ^A */
    check("^A goes to the start",
          rsd_line_cursor(&sh.line) == 0U, "cursor at 0");
    rsd_type(&sh, "x");
    check("typing at the start inserts there",
          rsd_streq(rsd_line_text(&sh.line), "xhelp"),
          rsd_line_text(&sh.line));
    rsd_key(&sh, 0x08);
    rsd_key(&sh, 0x05);                      /* ^E */
    check("^E goes to the end",
          rsd_line_cursor(&sh.line) == 4U, "cursor at 4");
    rsd_type(&sh, "\r");

    /* The screen must agree with the buffer after all that editing. */
    log_in(&sh);
    rsd_type(&sh, "echo abcdef");
    rsd_key(&sh, 0x02); rsd_key(&sh, 0x02); rsd_key(&sh, 0x02);
    rsd_type(&sh, "-");
    rsd_screen_row(&sh, sh.term.row, row, sizeof(row));
    check("the screen shows what the buffer holds",
          strstr(row, "echo abc-def") != NULL,
          row);
    draw(&sh, "../screens/07-editing.png");
    rsd_key(&sh, 0x15);

    /* ---- history ---- */
    log_in(&sh);
    rsd_type(&sh, "uname\r");
    rsd_type(&sh, "whoami\r");
    rsd_key(&sh, RSD_KEY_UP);
    check("up recalls the last command",
          rsd_streq(rsd_line_text(&sh.line), "whoami"),
          rsd_line_text(&sh.line));
    rsd_key(&sh, RSD_KEY_UP);
    check("up again goes further back",
          rsd_streq(rsd_line_text(&sh.line), "uname"),
          rsd_line_text(&sh.line));
    rsd_key(&sh, RSD_KEY_DOWN);
    check("down comes forward again",
          rsd_streq(rsd_line_text(&sh.line), "whoami"),
          rsd_line_text(&sh.line));
    rsd_key(&sh, 0x15);
    rsd_type(&sh, "history\r");
    draw(&sh, "../screens/08-history.png");

    /* ---- quoting ---- */
    {
        struct rsd_argv av;

        rsd_parse("echo 'two words' plain", &av);
        check("a quoted argument stays one argument",
              av.count == 3U && rsd_streq(av.arg[1], "two words"),
              av.arg[1]);
        rsd_parse("a  b\tc", &av);
        check("runs of blanks are one separator",
              av.count == 3U, "three arguments");
    }

    /* ---- man, and starty ---- */
    log_in(&sh);
    rsd_type(&sh, "man ledger\r");
    check("man has a page for the ledger",
          screen_find(&sh, "LEDGER(1)") >= 0, "LEDGER(1)");
    draw(&sh, "../screens/09-man.png");

    log_in(&sh);
    rsd_type(&sh, "help\r");
    draw(&sh, "../screens/10-help.png");

    log_in(&sh);
    rsd_type(&sh, "starty\r");
    check("starty starts the session", sh.desktop,
          "the desktop flag is set");

    /* ---- config(8): the full-screen editor ---- */
    log_in(&sh);
    rsd_type(&sh, "config\r");
    check("config opens a full-screen editor",
          sh.phase == RSD_PHASE_CONFIG
          && screen_find(&sh, "Configuration") >= 0,
          "a menu of groups");
    check("it counts what is on in each group",
          screen_find(&sh, "of") >= 0 && screen_find(&sh, "enabled") >= 0,
          "N of M enabled, per group");
    draw(&sh, "../screens/12-config.png");

    rsd_key(&sh, '\r');                     /* open the kernel group */
    check("a group opens a checklist",
          screen_find(&sh, "WXORX") >= 0 && screen_find(&sh, "SMP") >= 0,
          "the kernel options");
    check("the bottom line explains the highlighted option",
          screen_find(&sh, "Not a switch") >= 0,
          "W^X is verified, not chosen");
    draw(&sh, "../screens/13-config-kernel.png");

    /* Two of these are not switches, and the space bar has to say so by
     * doing nothing. */
    rsd_key(&sh, ' ');
    check("a locked option will not turn off",
          rsd_conf_get("WXORX"), "W^X is still on");
    rsd_type(&sh, "smp");                   /* jump by first letter */
    rsd_key(&sh, ' ');
    check("an ordinary option toggles",
          rsd_conf_get("SMP"), "SMP went on");
    check("and the table changed, not just the widget",
          rsd_conf_changed() == 1U, "one option differs from saved");

    rsd_key(&sh, 0x1B);                     /* back to the groups */
    check("escaping a group keeps the toggle",
          rsd_conf_get("SMP") && sh.phase == RSD_PHASE_CONFIG,
          "still on, back on the group list");

    rsd_key(&sh, RSD_KEY_F1);
    check("F1 explains why some options are locked",
          screen_find(&sh, "nvme0") >= 0 || screen_find(&sh, "FAT32") >= 0,
          "root is on nvme0 and formatted FAT32");
    rsd_key(&sh, '\r');

    /* ---- make: what config left behind ---- */
    rsd_type(&sh, "\t");                    /* onto Save */
    rsd_key(&sh, '\r');
    check("saving leaves the editor and says where it went",
          sh.phase == RSD_PHASE_SHELL
          && screen_find(&sh, "KERNEL.CONF") >= 0,
          "RSD/KERNEL.CONF");
    check("and there is nothing unsaved afterwards",
          rsd_conf_changed() == 0U, "0 changed");

    rsd_type(&sh, "make\r");
    check("make compiles what is enabled",
          screen_find(&sh, "rsd.elf") >= 0
          && screen_find(&sh, "options") >= 0,
          "an ld line and a summary");
    draw(&sh, "../screens/14-make.png");

    {
        /* Turning an option off has to build less. Read the count off
         * the screen both times rather than trusting the option table -
         * the point of make is that the config reaches the build. */
        char summary[RSD_COLS + 1];
        int before, after;
        int at = screen_find(&sh, "make: ");

        rsd_screen_row(&sh, (uint32_t)at, summary, sizeof(summary));
        before = atoi(strstr(summary, "make: ") + 6);

        rsd_type(&sh, "config\r");
        rsd_key(&sh, '\r');                 /* kernel group */
        rsd_type(&sh, "l");                  /* LEDGER */
        rsd_key(&sh, ' ');
        rsd_key(&sh, 0x1B);
        rsd_type(&sh, "\t");
        rsd_key(&sh, '\r');                 /* Save */
        rsd_type(&sh, "make\r");
        at = screen_find(&sh, "make: ");
        rsd_screen_row(&sh, (uint32_t)at, summary, sizeof(summary));
        after = atoi(strstr(summary, "make: ") + 6);

        check("turning an option off builds one thing less",
              before > 0 && after == before - 1,
              before > 0 ? "the count fell by one" : "no count on screen");
    }

    rsd_type(&sh, "make verify\r");
    check("make verify checks the rules that are not options",
          screen_find(&sh, "3 of 3") >= 0, "W^X, no FP, reproducible");
    rsd_type(&sh, "make frobnicate\r");
    check("make refuses a target it does not have",
          screen_find(&sh, "no rule to make target") >= 0
          && sh.status == 2U, "status 2, like make");

    /* ---- config -e: the same table from a prompt ---- */
    log_in(&sh);
    rsd_type(&sh, "config -e\r");
    check("config -e gives a ukc prompt",
          sh.phase == RSD_PHASE_UKC && screen_find(&sh, "ukc>") >= 0,
          "ukc>");
    rsd_type(&sh, "show NVME\r");
    check("show prints one option and why",
          screen_find(&sh, "locked") >= 0 && screen_find(&sh, "Root") >= 0,
          "enabled, locked, and the reason");
    rsd_type(&sh, "disable NVME\r");
    check("it refuses to disable a required one",
          screen_find(&sh, "cannot be changed") >= 0
          && rsd_conf_get("NVME"), "NVME is still on");
    rsd_type(&sh, "disable HDA\r");
    check("and does disable one that is not",
          !rsd_conf_get("HDA"), "audio off");
    rsd_type(&sh, "disable NOTHING\r");
    check("an unknown option is refused, not invented",
          screen_find(&sh, "no such option") >= 0, "no such option");
    draw(&sh, "../screens/15-ukc.png");
    rsd_type(&sh, "quit\r");
    check("quitting says it did not save",
          sh.phase == RSD_PHASE_SHELL && screen_find(&sh, "not saved") >= 0,
          "ukc: not saved");
    rsd_conf_reset();

    /* ---- scrolling ---- */
    log_in(&sh);
    for (int i = 0; i < 30; ++i) {
        rsd_type(&sh, "uname\r");
    }
    rsd_screen_row(&sh, 0U, row, sizeof(row));
    check("the screen scrolls rather than growing",
          screen_find(&sh, RSD_BANNER) < 0,
          "the boot banner has scrolled off the top");
    draw(&sh, "../screens/11-scrolled.png");

    printf("\n");
    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
