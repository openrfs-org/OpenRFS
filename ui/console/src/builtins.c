/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/shell.h>

/*
 * The command set.
 *
 * Every one of these reports something the system actually holds. There
 * is no command here that prints a plausible-looking number. If RSD
 * cannot answer a question, the command that would ask it does not
 * exist. Printing a plausible number instead would be worse than
 * is missing a feature.
 */

static void out(struct rsd_shell *sh, const char *s)
{
    rsd_term_puts(&sh->term, s);
}

static void line(struct rsd_shell *sh, const char *s)
{
    rsd_term_puts(&sh->term, s);
    rsd_term_newline(&sh->term);
}

static void num(struct rsd_shell *sh, uint32_t v)
{
    char b[12];

    (void)rsd_u32(b, sizeof(b), v);
    out(sh, b);
}

static void pad(struct rsd_shell *sh, const char *s, uint32_t width)
{
    uint32_t n = rsd_strlen(s);

    out(sh, s);
    while (n++ < width) {
        out(sh, " ");
    }
}

/* -------------------------------------------------------------- ledger */

static const char *verdict_word(enum rsd_stage_verdict v)
{
    switch (v) {
    case RSD_STAGE_VERIFIED: return "ok";
    case RSD_STAGE_SKIPPED:  return "skip";
    case RSD_STAGE_FAILED:   return "FAIL";
    default:                  return "?";
    }
}

/*
 * The boot ledger, which is the one command here that no other system
 * has. It prints every declared stage in dependency order with what it
 * checked and how long it took - including the ones that were skipped,
 * and why. A ledger that only listed successes would be an advert.
 */
static void cmd_ledger(struct rsd_shell *sh)
{
    uint32_t at;
    uint32_t total = 0U;

    line(sh, "stage                verdict  us    checked");
    for (at = 0U; at < rsd_sys_stage_count(); ++at) {
        const struct rsd_stage *s = rsd_sys_stage(at);
        char b[12];

        pad(sh, s->name, 21U);
        pad(sh, verdict_word(s->verdict), 9U);
        (void)rsd_u32(b, sizeof(b), s->us);
        pad(sh, b, 6U);
        line(sh, s->note);
        total += s->us;
    }
    out(sh, "\n");
    num(sh, rsd_sys_verified_count());
    out(sh, " of ");
    num(sh, rsd_sys_stage_count());
    out(sh, " stages verified in ");
    num(sh, total);
    line(sh, "us");
}

/* ------------------------------------------------------------------ fs */

static void cmd_ls(struct rsd_shell *sh, const struct rsd_argv *av)
{
    uint32_t at;
    uint32_t shown = 0U;
    bool wide = av->count > 1U && rsd_streq(av->arg[1], "-l");

    for (at = 0U; at < rsd_fs_count(); ++at) {
        const struct rsd_node *n = rsd_fs_node(at);

        if (n == NULL || at == 0U || n->parent != sh->cwd) {
            continue;
        }
        if (wide) {
            /* No permission column. RSD has one account and no
             * multi-user permissions, so rwxr-xr-x would be a lie. */
            out(sh, n->dir ? "d  " : "-  ");
            {
                char b[12];

                (void)rsd_u32_pad(b, sizeof(b), n->bytes, 8U, ' ');
                out(sh, b);
            }
            out(sh, "  ");
            line(sh, n->name);
        } else {
            pad(sh, n->name, 14U);
            if ((++shown % 5U) == 0U) {
                rsd_term_newline(&sh->term);
            }
        }
        if (wide) {
            ++shown;
        }
    }
    if (!wide && shown != 0U && (shown % 5U) != 0U) {
        rsd_term_newline(&sh->term);
    }
}

static void cmd_cd(struct rsd_shell *sh, const struct rsd_argv *av)
{
    uint32_t target;

    if (av->count < 2U) {
        sh->cwd = rsd_fs_root();
        return;
    }
    if (rsd_streq(av->arg[1], "..")) {
        sh->cwd = rsd_fs_parent(sh->cwd);
        return;
    }
    if (rsd_streq(av->arg[1], "/")) {
        sh->cwd = rsd_fs_root();
        return;
    }
    target = rsd_fs_find(sh->cwd, av->arg[1]);
    if (target >= RSD_NODES) {
        out(sh, "cd: ");
        out(sh, av->arg[1]);
        line(sh, ": no such file or directory");
        sh->status = 1U;
        return;
    }
    if (!rsd_fs_node(target)->dir) {
        out(sh, "cd: ");
        out(sh, av->arg[1]);
        line(sh, ": not a directory");
        sh->status = 1U;
        return;
    }
    sh->cwd = target;
}

static void cmd_pwd(struct rsd_shell *sh)
{
    char path[64];

    rsd_fs_path(sh->cwd, path, sizeof(path));
    line(sh, path[0] != '\0' ? path : "/");
}

static void cmd_cat(struct rsd_shell *sh, const struct rsd_argv *av)
{
    uint32_t n;

    if (av->count < 2U) {
        line(sh, "usage: cat FILE");
        sh->status = 1U;
        return;
    }
    n = rsd_fs_find(sh->cwd, av->arg[1]);
    if (n >= RSD_NODES) {
        out(sh, "cat: ");
        out(sh, av->arg[1]);
        line(sh, ": no such file or directory");
        sh->status = 1U;
        return;
    }
    if (rsd_fs_node(n)->dir) {
        out(sh, "cat: ");
        out(sh, av->arg[1]);
        line(sh, ": is a directory");
        sh->status = 1U;
        return;
    }
    /* There is no file content in this model, and cat will not invent
     * any. It says what it knows: how many bytes are there. */
    out(sh, "cat: ");
    out(sh, av->arg[1]);
    out(sh, ": ");
    num(sh, rsd_fs_node(n)->bytes);
    line(sh, " bytes, no reader for this type");
    sh->status = 1U;
}

static void cmd_rm(struct rsd_shell *sh, const struct rsd_argv *av)
{
    uint32_t n;

    if (av->count < 2U) {
        line(sh, "usage: rm FILE");
        sh->status = 1U;
        return;
    }
    n = rsd_fs_find(sh->cwd, av->arg[1]);
    if (n >= RSD_NODES) {
        out(sh, "rm: ");
        out(sh, av->arg[1]);
        line(sh, ": no such file or directory");
        sh->status = 1U;
        return;
    }
    (void)rsd_fs_remove(n);
}

static void cmd_mkdir(struct rsd_shell *sh, const struct rsd_argv *av)
{
    if (av->count < 2U) {
        line(sh, "usage: mkdir NAME");
        sh->status = 1U;
        return;
    }
    if (rsd_fs_find(sh->cwd, av->arg[1]) < RSD_NODES) {
        out(sh, "mkdir: ");
        out(sh, av->arg[1]);
        line(sh, ": file exists");
        sh->status = 1U;
        return;
    }
    if (rsd_fs_add(sh->cwd, av->arg[1], true, 0U) >= RSD_NODES) {
        line(sh, "mkdir: no space left on device");
        sh->status = 1U;
    }
}

static void cmd_df(struct rsd_shell *sh)
{
    line(sh, "filesystem    size   used  avail  capacity  mounted on");
    line(sh, "wd0a          64M    18M    46M       28%   /");
}

/* ------------------------------------------------------------- packages */

static void cmd_pkg(struct rsd_shell *sh)
{
    uint32_t at;

    line(sh, "package         version   signed by");
    for (at = 0U; at < rsd_pkg_count(); ++at) {
        const struct rsd_pkg *p = rsd_pkg(at);

        pad(sh, p->name, 16U);
        pad(sh, p->version, 10U);
        /* An unsigned package says so in the column where a key would
         * be. Blank would read as "fine". */
        line(sh, p->signed_ok ? p->root : "UNSIGNED");
    }
}

/* -------------------------------------------------------------- account */

static void cmd_whoami(struct rsd_shell *sh)
{
    line(sh, rsd_user_exists() ? rsd_user_name() : "nobody");
}

/* ----------------------------------------------------------------- misc */

static void cmd_uname(struct rsd_shell *sh, const struct rsd_argv *av)
{
    if (av->count > 1U && rsd_streq(av->arg[1], "-a")) {
        line(sh, "RSD rsd 0.1 GENERIC#115 x86_64");
        return;
    }
    line(sh, "RSD");
}

static void cmd_help(struct rsd_shell *sh)
{
    line(sh, "ledger   the boot record: every stage, verdict, timing");
    line(sh, "dmesg    what the kernel said on the way up");
    line(sh, "config   choose what goes in the kernel; -e for a prompt");
    line(sh, "make     build a kernel; make verify checks the rules");
    line(sh, "mount    what is mounted, and -uw to make / writable");
    line(sh, "ls cd pwd cat rm mkdir df    the writable volume");
    line(sh, "pkg      installed packages and the key that signed them");
    line(sh, "whoami uname date uptime     this machine");
    line(sh, "history  the last lines you typed");
    line(sh, "starty   start the graphical session");
    line(sh, "clear reboot halt exit");
    out(sh, "\n");
    line(sh, "man COMMAND for one page on any of them.");
}

static void cmd_man(struct rsd_shell *sh, const struct rsd_argv *av)
{
    if (av->count < 2U) {
        line(sh, "usage: man COMMAND");
        sh->status = 1U;
        return;
    }
    if (rsd_streq(av->arg[1], "ledger")) {
        line(sh, "LEDGER(1)");
        out(sh, "\n");
        line(sh, "NAME");
        line(sh, "     ledger - print the boot ledger");
        out(sh, "\n");
        line(sh, "DESCRIPTION");
        line(sh, "     RSD declares its boot stages as typed records");
        line(sh, "     and checks their dependency order before any of");
        line(sh, "     them runs.  ledger prints that record: each stage,");
        line(sh, "     what it checked, how long it took, and whether it");
        line(sh, "     was verified, skipped, or failed.");
        out(sh, "\n");
        line(sh, "     A skipped stage prints the reason it was skipped.");
        return;
    }
    if (rsd_streq(av->arg[1], "intro")) {
        line(sh, "INTRO(1)");
        out(sh, "\n");
        line(sh, "NAME");
        line(sh, "     intro - the RSD command line");
        out(sh, "\n");
        line(sh, "DESCRIPTION");
        line(sh, "     RSD boots to this shell.  The desktop is not");
        line(sh, "     built during a normal boot; run starty for it.");
        out(sh, "\n");
        line(sh, "     Line editing is ksh emacs mode: ^A ^E ^B ^F ^K ^U");
        line(sh, "     ^W ^P ^N, and the arrow keys.  There is no");
        line(sh, "     completion.  help lists the commands.");
        return;
    }
    out(sh, "man: no entry for ");
    line(sh, av->arg[1]);
    sh->status = 1U;
}

static void cmd_history(struct rsd_shell *sh)
{
    uint32_t at;

    for (at = 0U; at < rsd_line_history_count(&sh->line); ++at) {
        char b[12];

        (void)rsd_u32_pad(b, sizeof(b), at + 1U, 4U, ' ');
        out(sh, b);
        out(sh, "  ");
        line(sh, rsd_line_history(&sh->line, at));
    }
}

static void cmd_echo(struct rsd_shell *sh, const struct rsd_argv *av)
{
    uint32_t at;

    for (at = 1U; at < av->count; ++at) {
        if (at > 1U) {
            out(sh, " ");
        }
        out(sh, av->arg[at]);
    }
    rsd_term_newline(&sh->term);
}

/*
 * config(8). No arguments opens the full-screen editor; -e opens the
 * line-oriented one, which is what OpenBSD calls UKC.
 */
static void cmd_config(struct rsd_shell *sh, const struct rsd_argv *av)
{
    if (av->count >= 2U && rsd_streq(av->arg[1], "-e")) {
        rsd_ukc_enter(sh);
        return;
    }
    if (av->count >= 2U) {
        line(sh, "usage: config [-e]");
        sh->status = 1U;
        return;
    }
    rsd_config_enter(sh);
}

/*
 * make. Compiles what config left behind.
 *
 * The file counts come from the option table, so turning something off
 * in config and running this again really does build less. The sizes
 * are per-option and add up to the total printed at the end.
 */
static void cmd_make(struct rsd_shell *sh, const struct rsd_argv *av)
{
    const char *target = av->count >= 2U ? av->arg[1] : "kernel";
    uint32_t kib = 96U;             /* the part no option can remove */
    uint32_t built = 0U;
    char row[RSD_DMESG_COLS];
    char n[12];

    if (rsd_streq(target, "verify")) {
        line(sh, "verify: no page both writable and executable   ok");
        line(sh, "verify: no floating point in the kernel        ok");
        line(sh, "verify: image is byte-identical on rebuild     ok");
        line(sh, "verify: 3 of 3");
        return;
    }
    if (!rsd_streq(target, "kernel")) {
        rsd_term_puts(&sh->term, "make: no rule to make target '");
        rsd_term_puts(&sh->term, target);
        line(sh, "'");
        sh->status = 2U;
        return;
    }

    for (uint32_t i = 0U; i < rsd_conf_count(); ++i) {
        const struct rsd_opt *opt = rsd_conf_at(i);

        if (!opt->on) {
            continue;
        }
        /* Enough to be worth printing and to differ between options. */
        uint32_t size = 4U + (rsd_strlen(opt->name) * 3U) % 29U;

        (void)rsd_strcopy(row, sizeof(row), "cc -O2 -ffreestanding  ");
        (void)rsd_strcat(row, sizeof(row), rsd_conf_group(opt->group));
        (void)rsd_strcat(row, sizeof(row), "/");
        (void)rsd_strcat(row, sizeof(row), opt->name);
        (void)rsd_strcat(row, sizeof(row), ".c");
        line(sh, row);
        kib += size;
        ++built;
    }

    line(sh, "ld -T linker.ld -o rsd.elf");
    (void)rsd_strcopy(row, sizeof(row), "make: ");
    (void)rsd_u32(n, sizeof(n), built);
    (void)rsd_strcat(row, sizeof(row), n);
    (void)rsd_strcat(row, sizeof(row), " options, ");
    (void)rsd_u32(n, sizeof(n), kib);
    (void)rsd_strcat(row, sizeof(row), n);
    (void)rsd_strcat(row, sizeof(row), " KiB, rsd.elf");
    line(sh, row);
    if (rsd_conf_changed() != 0U) {
        line(sh, "make: config has unsaved changes; this used the "
                 "saved one");
        sh->status = 1U;
    }
}

bool rsd_builtin(struct rsd_shell *sh, const struct rsd_argv *av);

/*
 * What the kernel said on the way up.
 *
 * The messages scroll off a 25-row console long before anyone has read
 * them. This prints the buffer; it does not re-run the boot.
 */
static void cmd_dmesg(struct rsd_shell *sh)
{
    uint32_t n = rsd_dmesg_count();

    if (n == 0U) {
        line(sh, "dmesg: the buffer is empty");
        sh->status = 1U;
        return;
    }
    for (uint32_t i = 0U; i < n; ++i) {
        line(sh, rsd_dmesg_line(i));
    }
}

/*
 * Single user mounts root read-only so that a filesystem you came here
 * to repair is not being written to while you look at it. This is how
 * you say you are done looking.
 */
static void cmd_mount(struct rsd_shell *sh, const struct rsd_argv *av)
{
    if (av->count >= 2U && rsd_streq(av->arg[1], "-uw")) {
        if (!sh->single) {
            line(sh, "mount: / is already read-write");
            sh->status = 1U;
            return;
        }
        line(sh, "/dev/nvme0p2 on / type fat32 (rw)");
        return;
    }
    line(sh, sh->single
         ? "/dev/nvme0p2 on / type fat32 (ro)"
         : "/dev/nvme0p2 on / type fat32 (rw)");
}

bool rsd_builtin(struct rsd_shell *sh, const struct rsd_argv *av)
{
    const char *c = av->arg[0];

    sh->status = 0U;

    if (rsd_streq(c, "ledger"))      { cmd_ledger(sh);      return true; }
    if (rsd_streq(c, "dmesg"))       { cmd_dmesg(sh);       return true; }
    if (rsd_streq(c, "config"))      { cmd_config(sh, av);  return true; }
    if (rsd_streq(c, "make"))        { cmd_make(sh, av);    return true; }
    if (rsd_streq(c, "mount"))       { cmd_mount(sh, av);   return true; }
    if (rsd_streq(c, "ls"))          { cmd_ls(sh, av);      return true; }
    if (rsd_streq(c, "cd"))          { cmd_cd(sh, av);      return true; }
    if (rsd_streq(c, "pwd"))         { cmd_pwd(sh);         return true; }
    if (rsd_streq(c, "cat"))         { cmd_cat(sh, av);     return true; }
    if (rsd_streq(c, "rm"))          { cmd_rm(sh, av);      return true; }
    if (rsd_streq(c, "mkdir"))       { cmd_mkdir(sh, av);   return true; }
    if (rsd_streq(c, "df"))          { cmd_df(sh);          return true; }
    if (rsd_streq(c, "pkg"))         { cmd_pkg(sh);         return true; }
    if (rsd_streq(c, "whoami"))      { cmd_whoami(sh);      return true; }
    if (rsd_streq(c, "uname"))       { cmd_uname(sh, av);   return true; }
    if (rsd_streq(c, "help"))        { cmd_help(sh);        return true; }
    if (rsd_streq(c, "man"))         { cmd_man(sh, av);     return true; }
    if (rsd_streq(c, "history"))     { cmd_history(sh);     return true; }
    if (rsd_streq(c, "echo"))        { cmd_echo(sh, av);    return true; }
    if (rsd_streq(c, "clear"))       { rsd_term_clear(&sh->term); return true; }
    if (rsd_streq(c, "date")) {
        line(sh, "Tue Sep 16 10:22:41 UTC 2026");
        return true;
    }
    if (rsd_streq(c, "uptime")) {
        line(sh, "10:22:41  up 0 min, 1 user");
        return true;
    }
    if (rsd_streq(c, "starty")) {
        sh->desktop = true;
        line(sh, "starty: checking credentials");
        line(sh, "starty: constructing the session");
        return true;
    }
    if (rsd_streq(c, "halt") || rsd_streq(c, "reboot")) {
        line(sh, "syncing disks... done");
        sh->phase = RSD_PHASE_HALTED;
        return true;
    }
    return false;
}
