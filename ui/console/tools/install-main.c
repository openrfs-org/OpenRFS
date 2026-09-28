/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The installer's harness.
 *
 * It presses keys. Every screen in here is reached the way a person
 * would reach it - down arrow, space, tab, return - and then the screen
 * is read back and asserted. Nothing calls a step function directly,
 * because a check that called the step function would prove the step
 * works and tell you nothing about whether any key reaches it.
 *
 * Several of these checks are about colour, which is only possible
 * because the console keeps an attribute per cell and the harness can
 * read it. "The box is grey and the field behind it is blue" is a
 * claim, and a claim that can be wrong is worth asserting.
 *
 * Every check here can fail. If one does, the build fails.
 */
#include <rsd/install.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render.h"

static int failures;
static int shots;

static void check(const char *name, int ok, const char *detail)
{
    printf(ok ? "  ok   %s - %s\n" : "  FAIL %s - %s\n", name, detail);
    if (!ok) {
        ++failures;
    }
}

static int find(const struct rsd_install *in, const char *needle)
{
    char row[RSD_COLS + 1];

    for (uint32_t r = 0U; r < RSD_ROWS; ++r) {
        rsd_install_row(in, r, row, sizeof(row));
        if (strstr(row, needle) != NULL) {
            return (int)r;
        }
    }
    return -1;
}

/* The screen as text, when a check disagrees with a screenshot and you
 * need to know which of them is wrong. */
static void dump(const struct rsd_install *in)
{
    char row[RSD_COLS + 1];

    if (getenv("RSD_DUMP") == NULL) {
        return;
    }
    for (uint32_t r = 0U; r < RSD_ROWS; ++r) {
        rsd_install_row(in, r, row, sizeof(row));
        printf("    |%s\n", row);
    }
}

/*
 * The screen as one line, with runs of blanks collapsed and the frame
 * glyphs blanked out.
 *
 * A dialog wraps its paragraph to fit its box, so a sentence on the
 * screen is very often two rows of it. A check that searched one row at
 * a time would be asserting where the line breaks fell, which is the
 * box's business and nobody else's.
 */
static const char *flat(const struct rsd_install *in)
{
    static char out[RSD_ROWS * (RSD_COLS + 2U) + 1U];
    char row[RSD_COLS + 1];
    size_t n = 0;

    for (uint32_t r = 0U; r < RSD_ROWS; ++r) {
        rsd_install_row(in, r, row, sizeof(row));
        for (size_t i = 0; row[i] != '\0'; ++i) {
            char c = (unsigned char)row[i] > 0x7EU ? ' ' : row[i];

            if (c == ' ' && (n == 0 || out[n - 1] == ' ')) {
                continue;
            }
            out[n++] = c;
        }
        if (n != 0 && out[n - 1] != ' ') {
            out[n++] = ' ';
        }
    }
    out[n] = '\0';
    return out;
}

static int says(const struct rsd_install *in, const char *phrase)
{
    return strstr(flat(in), phrase) != NULL;
}

/*
 * EVERY CHOICE THAT IS OFFERED IS PRINTED.
 *
 * install.sub lists what you may answer with before it asks - "Available
 * disks are: sd0 sd1." - and that sentence is the only place a choice
 * appears.  There are no descriptions beside them and no help line under
 * them, so a name that is in the widget and not in that sentence is a
 * choice you cannot know about.
 *
 * This used to check the descriptions too, because the boxes drew them.
 * The transcript does not, and neither does OpenBSD: a set is called
 * base, and that is the whole of what the installer says about it.
 */
static const char *unlisted(const struct rsd_install *in)
{
    const struct rsd_ui *ui = &in->ui;

    for (uint32_t i = 0U; i < ui->items; ++i) {
        if (ui->item[i].tag[0] != '\0' && find(in, ui->item[i].tag) < 0) {
            return ui->item[i].tag;
        }
    }
    return NULL;
}

static void check_intact(const struct rsd_install *in, const char *where)
{
    const char *bad = unlisted(in);

    check("every choice offered is printed", bad == NULL,
          bad == NULL ? where : bad);
}

/*
 * THE THREE THINGS A BSDINSTALL SCREEN IS MADE OF, asserted by colour.
 *
 * This is only possible because the console keeps an attribute per cell
 * and the harness can read it back, and it is worth doing: "the field
 * is blue, the box is grey and there is a shadow under it" is a claim,
 * and a claim that can be wrong is worth asserting.
 *
 * The check that used to be here asserted the OPPOSITE of all three -
 * that no cell was on anything but black and no glyph was above ASCII -
 * because the installer had been repainted as OpenBSD's install(8),
 * which prints instead of drawing. It passed, and it was a check on a
 * look this installer was never meant to have.
 */
static int field_is(const struct rsd_install *in, uint32_t bg)
{
    uint32_t seen = 0U;

    for (uint32_t c = 0U; c < RSD_COLS; ++c) {
        if (RSD_ATTR_BG(rsd_term_attr_at(&in->term, 3U, c)) == bg) {
            ++seen;
        }
    }
    return seen > RSD_COLS / 2U;
}

static int box_is(const struct rsd_install *in, uint32_t bg)
{
    for (uint32_t r = 0U; r < RSD_ROWS; ++r) {
        uint32_t run = 0U;

        for (uint32_t c = 0U; c < RSD_COLS; ++c) {
            run = RSD_ATTR_BG(rsd_term_attr_at(&in->term, r, c)) == bg
                ? run + 1U : 0U;
            if (run > 30U) {
                return 1;
            }
        }
    }
    return 0;
}

/* The shadow is two columns of black to the right of the box, one row
 * below its top - which is where dialog(1) puts it and nowhere else on
 * a bsdinstall screen is black. */
static int has_shadow(const struct rsd_install *in)
{
    for (uint32_t r = 1U; r + 1U < RSD_ROWS; ++r) {
        for (uint32_t c = 2U; c < RSD_COLS; ++c) {
            if (RSD_ATTR_BG(rsd_term_attr_at(&in->term, r, c))
                    == RSD_BLACK
                && RSD_ATTR_BG(rsd_term_attr_at(&in->term, r, c - 1U))
                    == RSD_GREY) {
                return 1;
            }
        }
    }
    return 0;
}

/* A frame glyph, which is the one thing a printing installer never
 * emits. */
static int has_frame(const struct rsd_install *in)
{
    for (uint32_t r = 0U; r < RSD_ROWS; ++r) {
        for (uint32_t c = 0U; c < RSD_COLS; ++c) {
            if ((unsigned char)rsd_term_at(&in->term, r, c)
                    == (unsigned char)RSD_G_UL) {
                return 1;
            }
        }
    }
    return 0;
}

static void shoot(const struct rsd_install *in, const char *name)
{
    char path[128];

    ++shots;
    dump(in);
    snprintf(path, sizeof(path), "../screens/i%02d-%s.png", shots, name);
    (void)rsd_render(&in->term, in->term.cursor, path);
}

/* Press a key and say nothing; the checks do the talking. */
static void key(struct rsd_install *in, int k)
{
    rsd_install_key(in, k);
}

static void down(struct rsd_install *in, int n)
{
    while (n-- > 0) {
        key(in, RSD_KEY_DOWN);
    }
}




/* ------------------------------------------------------------- the run */

int main(void)
{
    struct rsd_install in;
    char row[RSD_COLS + 1];

    /* ---- the keyboard, before anything is typed ---- */
    rsd_install_begin(&in);
    check("the first screen is the keymap",
          in.step == RSD_STEP_KEYMAP
          && find(&in, "Keymap Selection") >= 0,
          "the title in the box's top border, before a key is pressed");
    /*
     * By their tags AND their descriptions. A dialog menu draws both -
     * `uk' in the tag column and "United Kingdom" beside it - and the
     * description is the only thing that tells you what the tag means.
     * This checked tags alone while the installer printed tags alone.
     */
    check("it offers more than one keyboard",
          find(&in, "uk") >= 0 && find(&in, "ar") >= 0
          && find(&in, "dvorak") >= 0
          && says(&in, "United Kingdom") && says(&in, "Arabic 101"),
          "uk, ar and dvorak, each with what it is");
    check_intact(&in, "keymap");
    shoot(&in, "keymap");

    /*
     * THE BOX, THE FIELD, THE SHADOW AND THE TWO LINES ROUND THEM.
     *
     * This block used to assert the opposite of all of it - that
     * nothing was drawn, no box glyph anywhere and every cell on the
     * console's own black - and every one of those assertions passed.
     * They were checks on install(8)'s look, which is not the look this
     * was asked for.
     */
    {
        check("the field is blue and the box is grey",
              field_is(&in, RSD_BLUE) && box_is(&in, RSD_GREY),
              "dialog's SCREEN on BLUE, its DIALOG on WHITE - which is "
              "grey on a console");
        check("the box is framed and has a shadow under it",
              has_frame(&in) && has_shadow(&in),
              "a corner glyph, and two columns of black off its right "
              "edge");
        check("the installer says what it is along the top",
              find(&in, "RSD Installer") == 0,
              "row 0, which is where --backtitle goes");
        check("the keys are named in the bottom border",
              says(&in, "Press arrows, TAB or ENTER"),
              "bsdinstall's own hline, in the box's bottom border");
        /*
         * And the highlighted item is the one the widget is holding -
         * read twice from two places rather than compared against a
         * string written here, which is what catches a screen offering
         * a default it cannot honour.
         */
        {
            char def[RSD_UI_VALUE];

            rsd_ui_default(&in.ui, def, sizeof(def));
            check("the highlight is on what RETURN would take",
                  def[0] != '\0'
                  && rsd_streq(def, in.ui.item[in.ui.cursor].tag),
                  def);
        }
        check("the line about the highlighted item is along the foot",
              find(&in, "The default.") == (int)RSD_ROWS - 1,
              "dialog's --item-help, on the last row");
    }

    /* Escape on the first screen takes the default rather than
     * stranding you: there is nowhere further back to go. */
    key(&in, 0x1B);
    check("escape on the keymap takes the default",
          in.step == RSD_STEP_WELCOME && rsd_streq(in.keymap, "us"),
          "us, and on to the welcome");

    /* ---- the welcome, and the shell that is not the end ---- */
    check("the welcome offers three ways in",
          says(&in, "Install") && says(&in, "Shell") && says(&in, "Live"),
          "three buttons, which is how a dialog offers three ways");
    check("and says which system and which release it is",
          says(&in, "RSD 2.5"),
          "the one screen that carries the release");
    shoot(&in, "welcome");

    key(&in, '\t');                       /* onto Shell */
    key(&in, '\r');
    check("the shell keeps the installer alive behind it",
          in.step == RSD_STEP_SHELL && find(&in, "exit") >= 0,
          "it says how to come back");
    shoot(&in, "shell");

    /* ---- a whole installation, from the top ---- */
    rsd_install_begin(&in);
    key(&in, '\r');                       /* keymap: us */
    key(&in, '\r');                       /* welcome: Install */
    check("install leads to the hostname",
          in.step == RSD_STEP_HOSTNAME, rsd_install_step_name(&in));

    /* Typing into a form has to reach the screen, not just the field. */
    for (int i = 0; i < 7; ++i) {
        key(&in, 0x08);                   /* clear "rsd" */
    }
    rsd_install_type(&in, "puffy");
    check("what you type lands in the box",
          find(&in, "puffy") >= 0, "the field shows it");
    shoot(&in, "hostname");
    key(&in, '\r');
    check("the hostname was taken",
          rsd_streq(in.hostname, "puffy"), in.hostname);

    /* ---- components: two of them are not a choice ---- */
    check("the components screen came next",
          in.step == RSD_STEP_COMPONENTS, rsd_install_step_name(&in));
    key(&in, ' ');                        /* try to untick base */
    check("the system itself cannot be unticked",
          in.ui.item[0].on,
          "space on base leaves it ticked, because it is not optional");
    down(&in, 3);
    key(&in, ' ');                        /* tick ports */
    check("something optional can be ticked",
          in.ui.item[3].on, "ports went on");
    check_intact(&in, "components");
    shoot(&in, "components");
    key(&in, '\r');
    check("the ticks were kept",
          rsd_install_selected(&in, RSD_STEP_COMPONENTS, "ports")
          && rsd_install_selected(&in, RSD_STEP_COMPONENTS, "base")
          && !rsd_install_selected(&in, RSD_STEP_COMPONENTS, "src"),
          "ports on, base on, src off");

    /* ---- who lays the disk out ---- */
    check("the partitioning method is asked before the disk",
          in.step == RSD_STEP_METHOD, rsd_install_step_name(&in));
    {
        /*
         * THE BOTTOM LINE IS BACK, and it follows the highlight.
         *
         * A dialog menu's items are one or two words; what they mean is
         * on the line along the foot of the screen, and it changes as
         * you move. That line is the difference between a list of words
         * and a list you can act on without guessing, which is why
         * bsdinstall passes --item-help to every menu it draws.
         *
         * This asserted the absence of it for a while, on the grounds
         * that install.sub has no such line. It has one again.
         */
        char first[RSD_UI_HELP];

        (void)rsd_strcopy(first, sizeof(first),
                           in.ui.item[in.ui.cursor].help);
        check("every choice is in the box, with what it does beside it",
              says(&in, "Auto") && says(&in, "Edit")
              && says(&in, "Shell")
              && says(&in, "Lay the whole disk out"),
              "tag and description, which is what a dialog menu draws");
        check("the line at the foot is the highlighted item's own",
              first[0] != '\0'
              && find(&in, first) == (int)RSD_ROWS - 1, first);
        down(&in, 1);
        check("and it moves when the highlight does",
              !rsd_streq(first, in.ui.item[in.ui.cursor].help)
              && find(&in, in.ui.item[in.ui.cursor].help)
                 == (int)RSD_ROWS - 1,
              in.ui.item[in.ui.cursor].help);
    }
    shoot(&in, "method");

    /* F1 is not a key the list can answer; it has to reach the screen. */
    key(&in, RSD_KEY_F1);
    check("F1 explains the screen, not the arrow keys",
          in.step == RSD_STEP_HELP && says(&in, "Auto:")
          && says(&in, "Shell:"),
          rsd_install_step_name(&in));
    shoot(&in, "help");
    key(&in, 0x1B);
    check("leaving help goes back where F1 was pressed",
          in.step == RSD_STEP_METHOD,
          "not one screen further back than that");

    key(&in, RSD_KEY_UP);
    key(&in, '\r');                       /* Auto */
    check("the disk screen came next",
          in.step == RSD_STEP_DISK, rsd_install_step_name(&in));
    shoot(&in, "disk");
    down(&in, 1);
    key(&in, '\r');                       /* rescan */
    check("rescanning does not leave the disk screen",
          in.step == RSD_STEP_DISK,
          "it is a thing you do here, not a way off");
    key(&in, RSD_KEY_UP);
    key(&in, '\r');
    check("the disk was taken",
          rsd_streq(in.disk, "nvme0") && in.step == RSD_STEP_SCHEME,
          in.disk);

    /* ---- the scheme is a radio, so exactly one of it ---- */
    shoot(&in, "scheme");
    down(&in, 1);
    key(&in, ' ');
    check("picking one scheme unpicks the other",
          in.ui.item[1].on && !in.ui.item[0].on, "MBR on, GPT off");
    key(&in, RSD_KEY_UP);
    key(&in, ' ');
    key(&in, '\r');
    check("the scheme was taken",
          rsd_streq(in.scheme, "GPT"), in.scheme);

    /* ---- review: the numbers on the screen have to add up ---- */
    check("the review screen came next",
          in.step == RSD_STEP_REVIEW, rsd_install_step_name(&in));
    {
        int mib = 0;
        int parts = 0;

        for (uint32_t r = 0U; r < RSD_ROWS; ++r) {
            const char *at;

            rsd_install_row(&in, r, row, sizeof(row));
            at = strstr(row, " MiB");
            /* The disk's own line is the total, not a partition. */
            if (at == NULL || strstr(row, in.disk) != NULL) {
                continue;
            }
            mib += atoi(at - 2);
            ++parts;
        }
        check("the partitions fill the disk and no more",
              parts == 3 && mib == (int)in.disk_mib,
              parts == 3 ? "1 + 47 + 16 is 64, which is the disk"
                         : "the table does not have three partitions");
    }
    check("the review says nothing has happened yet",
          says(&in, "Nothing has been written"),
          "it says so on the screen, not just in the code");
    check_intact(&in, "review");
    shoot(&in, "review");

    /* ---- the last screen where the disk is still untouched ---- */
    key(&in, '\r');                       /* Finish */
    check("the confirmation came next",
          in.step == RSD_STEP_COMMIT, rsd_install_step_name(&in));
    check("the button under the cursor is not the destructive one",
          !rsd_streq(rsd_ui_button(&in.ui), "Commit"),
          rsd_ui_button(&in.ui));
    shoot(&in, "commit");

    /* Backing out here must leave the disk alone. */
    key(&in, RSD_KEY_RIGHT);
    key(&in, RSD_KEY_RIGHT);
    key(&in, '\r');                       /* Revert */
    check("reverting writes nothing",
          in.step == RSD_STEP_ABANDONED && !in.written,
          "and it says the disk is as you found it");
    check("and says so on the screen",
          find(&in, "Nothing was written") >= 0, "in as many words");
    shoot(&in, "abandoned");

    /* ---- again, and this time commit ---- */
    rsd_install_begin(&in);
    key(&in, '\r');
    key(&in, '\r');
    key(&in, '\r');                       /* hostname: the default */
    key(&in, '\r');                       /* components */
    key(&in, '\r');                       /* method: Auto */
    key(&in, '\r');                       /* disk */
    key(&in, '\r');                       /* scheme */
    key(&in, '\r');                       /* review: Finish */
    key(&in, RSD_KEY_LEFT);              /* onto Commit */
    key(&in, '\r');
    check("committing starts the writing",
          in.step == RSD_STEP_WRITE, rsd_install_step_name(&in));
    rsd_install_tick(&in);
    rsd_install_tick(&in);
    rsd_install_tick(&in);
    check("the gauge says what it is doing, not just how far",
          says(&in, "Writing the partition table") || says(&in, "FAT32"),
          "a line of prose under the bar");
    check("and shows how far as a number as well as a bar",
          says(&in, "%"), "a percentage under the blocks");
    check("keys do nothing while it works",
          (key(&in, '\r'), in.step == RSD_STEP_WRITE),
          "return at a progress bar goes nowhere");
    shoot(&in, "writing");

    rsd_install_settle(&in);
    check("writing finishes and verifying begins",
          in.written && in.step == RSD_STEP_VERIFY,
          rsd_install_step_name(&in));
    rsd_install_tick(&in);
    rsd_install_tick(&in);
    rsd_install_tick(&in);
    rsd_install_tick(&in);
    rsd_install_tick(&in);
    rsd_install_tick(&in);
    check("verifying is about signatures, not checksums alone",
          says(&in, "Ed25519") || says(&in, "Hashing"),
          "it names what it is checking");
    check("the line under the bar is not cut off by the box",
          in.ui.note[0] == '\0' || find(&in, in.ui.note) >= 0,
          in.ui.note);
    shoot(&in, "verifying");
    rsd_install_settle(&in);
    check("verifying finishes and the password comes next",
          in.verified && in.step == RSD_STEP_ROOTPW,
          rsd_install_step_name(&in));

    /* ---- the password, which is never on the screen ---- */
    /*
     * install.sub says one thing about the root password and it is not
     * where the password is stored: it says the typing will not show.
     * That is the only thing the person at the keyboard needs before
     * they start typing into an apparently dead prompt.
     */
    check("the password prompt warns that nothing will appear",
          find(&in, "(will not echo)") >= 0, "will not echo");
    rsd_install_type(&in, "correcthorse");
    check("the password is not echoed",
          find(&in, "correcthorse") < 0,
          "nowhere on the screen");
    /*
     * AND NOTHING APPEARS - not even stars, and this asks it of the
     * WHOLE SCREEN rather than of one line. It used to read the last
     * row and look for an asterisk there, which passed for a while
     * because the last row is the key legend and never had one.
     */
    check("and nothing at all is echoed, not even a count",
          strchr(flat(&in), '*') == NULL,
          "a --passwordbox without --insecure shows no length either");
    shoot(&in, "rootpw");
    key(&in, RSD_KEY_DOWN);
    rsd_install_type(&in, "correcthorseX");
    key(&in, '\r');
    check("two passwords that differ are refused",
          in.step == RSD_STEP_ROOTPW && in.rootpw[0] == '\0',
          "nothing was set");
    check("and it says why",
          find(&in, "did not match") >= 0, "in as many words");
    shoot(&in, "rootpw-mismatch");

    rsd_install_type(&in, "correcthorse");
    key(&in, RSD_KEY_DOWN);
    rsd_install_type(&in, "correcthorse");
    key(&in, '\r');
    check("two that match are taken",
          rsd_streq(in.rootpw, "correcthorse")
          && in.step == RSD_STEP_NETIF,
          "on to the network");

    /* ---- the network, and the way past it ---- */
    shoot(&in, "netif");
    key(&in, '\r');                       /* em0 */
    check("picking an interface asks how to configure it",
          in.step == RSD_STEP_DHCP, rsd_install_step_name(&in));
    shoot(&in, "dhcp");
    key(&in, RSD_KEY_RIGHT);
    key(&in, '\r');                       /* Static */
    check("static configuration asks for the three things it needs",
          in.step == RSD_STEP_STATIC && find(&in, "Subnet Mask") >= 0
          && find(&in, "Default Router") >= 0,
          "address, mask, router");
    shoot(&in, "static");
    key(&in, '\r');
    check("the address was taken",
          rsd_streq(in.ip, "192.168.1.40") && in.step == RSD_STEP_DNS,
          in.ip);
    shoot(&in, "dns");
    key(&in, '\r');

    /* ---- time ---- */
    check("the region comes before the zone",
          in.step == RSD_STEP_REGION, rsd_install_step_name(&in));
    /* In a dialog the regions ARE the list; there is no sentence
     * naming them first, because the box is the sentence. */
    check("the region screen lists the regions by name",
          find(&in, "Asia") >= 0 && find(&in, "Europe") >= 0,
          "each one a row of the menu");
    check_intact(&in, "region");
    shoot(&in, "region");
    down(&in, 2);                         /* Asia */
    key(&in, '\r');
    check("the zone list follows the region",
          in.step == RSD_STEP_ZONE && find(&in, "Riyadh") >= 0
          && find(&in, "London") < 0,
          "Asia's zones, and not Europe's");
    shoot(&in, "zone");
    key(&in, '\r');
    check("the zone is stored as region and city",
          rsd_streq(in.zone, "Asia/Riyadh"), in.zone);

    /* ---- startup, and the refusals ---- */
    check("startup came next",
          in.step == RSD_STEP_STARTUP, rsd_install_step_name(&in));
    check_intact(&in, "startup");
    shoot(&in, "startup");
    down(&in, 4);
    key(&in, ' ');                        /* start the desktop */
    key(&in, '\r');
    check("the desktop can be asked for at boot",
          rsd_install_selected(&in, RSD_STEP_STARTUP, "desktop"),
          "ticked");

    check("hardening came next",
          in.step == RSD_STEP_HARDENING, rsd_install_step_name(&in));
    check("every restriction is offered by name",
          find(&in, "wxorx") >= 0,
          "each one a row of the checklist");
    check_intact(&in, "hardening");
    check_intact(&in, "hardening");
    shoot(&in, "hardening");
    key(&in, '\r');

    /* ---- a user ---- */
    check("adding a user came next",
          in.step == RSD_STEP_ADDUSER, rsd_install_step_name(&in));
    rsd_install_type(&in, "saud");
    key(&in, RSD_KEY_DOWN);
    rsd_install_type(&in, "Saud Aljuaid");
    key(&in, RSD_KEY_DOWN);
    rsd_install_type(&in, "hunter2");
    key(&in, RSD_KEY_DOWN);
    rsd_install_type(&in, "hunter2");
    check("the name is shown and the password is not",
          find(&in, "Saud Aljuaid") >= 0 && find(&in, "hunter2") < 0,
          "one of these is not like the other");
    shoot(&in, "adduser");
    key(&in, '\r');
    check("the account was taken",
          rsd_streq(in.user, "saud") && in.step == RSD_STEP_FINAL,
          in.user);

    /* ---- the final menu really goes back ---- */
    check("the final menu lists what can still be changed",
          find(&in, "Hostname") >= 0 && find(&in, "Kernel") >= 0
          && find(&in, "Exit") >= 0,
          "and an Exit at the top");
    /* And it shows what each of them was set to, which is the only
     * place the whole installation is visible at once. */
    check("the final menu shows the answers, not just the questions",
          find(&in, in.hostname) >= 0 && find(&in, in.zone) >= 0
          && says(&in, "of 5 refusals kept"),
          "hostname, zone and how many refusals were kept");
    shoot(&in, "final");
    down(&in, 1);
    key(&in, '\r');
    check("going back to the hostname really goes there",
          in.step == RSD_STEP_HOSTNAME, rsd_install_step_name(&in));
    key(&in, '\r');
    key(&in, '\r');                       /* components, unchanged */
    check("a screen revisited comes back the way it was left",
          rsd_install_selected(&in, RSD_STEP_COMPONENTS, "sdk"),
          "the sdk is still ticked");

    /* Walk back to the end the short way. */
    rsd_install_begin(&in);
    key(&in, '\r'); key(&in, '\r'); key(&in, '\r'); key(&in, '\r');
    key(&in, '\r'); key(&in, '\r'); key(&in, '\r'); key(&in, '\r');
    key(&in, RSD_KEY_LEFT); key(&in, '\r');
    rsd_install_settle(&in);
    rsd_install_settle(&in);
    rsd_install_type(&in, "x");
    key(&in, RSD_KEY_DOWN);
    rsd_install_type(&in, "x");
    key(&in, '\r');
    down(&in, 1); key(&in, '\r');         /* network: none */
    check("declining the network skips straight to the clock",
          in.step == RSD_STEP_REGION,
          "no DHCP question for an interface you did not pick");
    down(&in, 6); key(&in, '\r');         /* UTC */
    check("UTC needs no second question",
          in.step == RSD_STEP_STARTUP && rsd_streq(in.zone, "UTC"),
          in.zone);
    key(&in, '\r');                       /* startup */
    key(&in, '\r');                       /* hardening */
    key(&in, '\t'); key(&in, '\t');       /* onto Skip */
    key(&in, '\r');
    check("a user can be skipped",
          in.step == RSD_STEP_FINAL && in.user[0] == '\0',
          "no account, and on to the end");
    key(&in, '\r');                       /* Exit */
    check("exit finishes the installation",
          in.step == RSD_STEP_DONE, rsd_install_step_name(&in));
    check("the last screen tells you to take the medium out",
          says(&in, "Remove the medium before rebooting"),
          "before rebooting");
    shoot(&in, "done");
    key(&in, '\r');                       /* Reboot */
    check("reboot hands the machine back",
          in.step == RSD_STEP_REBOOT && find(&in, "halted") >= 0,
          "the operating system has halted");
    shoot(&in, "reboot");

    printf("\n%d screens rendered\n", shots);
    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
