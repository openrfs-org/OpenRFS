/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/terminal.h>

#include <trait/packages.h>
#include <trait/theme.h>

#include "trait_logo.h"
#include "trait_mono.h"

#define TRAIT_MONO_COUNT (sizeof(trait_mono) / sizeof(trait_mono[0]))

/* lxterminal's own defaults, out of its .desktop and its preferences:
 * a black ground and a light grey ink, which is the pair it ships. */
#define TERM_GROUND 0x000000U

/*
 * HOW MUCH OF THE BLACK YOU GET.
 *
 * 255 is an opaque terminal and anything less lets what is behind it
 * through, which is what `xterm -tr` has done since before there was a
 * compositor to do it properly.
 *
 * 90, which is a third of the way to opaque.  The road here: 208 is
 * see-through on paper and not on a screen, 170 is, 140 needed the ink
 * to go white first, and below that the limit stopped being the ink at
 * all - white text cannot go any whiter, and a PALE WINDOW behind a
 * sheer terminal lifts the ground to meet it.  80 and 55 were tried and
 * both lose the text wherever the file manager is behind it.
 *
 * What buys the rest is the halo under the glyphs: see draw_mono().
 * With the text carrying its own dark ground the limit moves off the
 * contrast entirely, and 90 reads over black, over the root and over a
 * white window alike.
 */
#define TERM_OPAQUE 255U
#define TERM_SHEER 150U

static uint32_t term_opacity = TERM_SHEER;
/*
 * TWO FOREGROUNDS, AND WHICH ONE IS IN USE DEPENDS ON THE GROUND.
 *
 * #D3D7CF is lxterminal's own, and it is right on a black terminal.
 * It is not right on a see-through one: pseudo-transparency has no
 * compositor to hold the text opaque while the ground goes clear, so
 * the ground rises towards the ink and a light grey on a mid grey is
 * the first thing to become uncomfortable.  White buys back the
 * contrast the transparency spends, which is what everybody who runs
 * xterm -tr hard ends up doing.
 */
#define TERM_INK_OPAQUE 0xD3D7CFU
#define TERM_INK_SHEER 0xFFFFFFU
#define TERM_PAD 4U

/* ONE STRING.  uname -a prints it and gfetch prints it, and two copies
 * of a version number are two copies that can disagree. */
static const char KERNEL[] = "OpenRFS 2.4 amd64";

static const char PROMPT[] = "user@openrfs:~$ ";

/*
 * ONE INK CODE PER CHARACTER, beside the characters.
 *
 * A terminal prints what the program emits and gfetch emits a picture,
 * so this is the smallest thing that can carry one: a parallel plane of
 * indexes into the short table below, where 0 is the terminal's own
 * foreground and everything printed the ordinary way is 0.
 *
 * The two reds are the MARK'S OWN, averaged out of the drawing by
 * tools/make-logo.py rather than picked to look about right - which is
 * why they are the only two colours on this desktop that are not one of
 * the sixteen.  Black is in the table for ONE thing, which is a pupil:
 * the drawing is built out of black lines, and a line thinner than a
 * cell inked black on a black terminal is a bite out of the fish rather
 * than a line.  tools/make-logo.py is where that is decided and why.
 */
/* The ink plane indexes trait_logo_ink[], which the mark's own capture
 * generates; TRAIT_LOGO_INKS is how many entries it has. */
#define TERM_INKS TRAIT_LOGO_INKS

uint32_t trait_terminal_ink(void)
{
    return term_opacity < TERM_OPAQUE ? TERM_INK_SHEER : TERM_INK_OPAQUE;
}

static uint32_t term_ink(void)
{
    return trait_terminal_ink();
}

static uint32_t ink_of(uint8_t code)
{
    /*
     * A CELL'S COLOUR IS THE MARK'S OWN, straight out of the table the
     * capture generated.  This used to be a switch over five named
     * things - a pupil, the body, the tongue, an eye - because the art
     * arrived as a silhouette and the colours had to be put back by
     * classifying the drawing.  The art arrives coloured now, so there
     * is nothing to name and nothing to decide: the index is the index.
     *
     * Index 0 is not a colour.  It means the cell was not part of the
     * picture, and those are drawn in whatever the terminal is writing
     * in - which is how the facts beside the mark come out.
     */
    if (code == 0U || code >= TRAIT_LOGO_INKS) {
        return term_ink();
    }
    return trait_logo_ink[code];
}

static char lines[TRAIT_TERM_HISTORY][TRAIT_TERM_LINE_BYTES];
static uint8_t inks[TRAIT_TERM_HISTORY][TRAIT_TERM_LINE_BYTES];
static uint32_t line_count;
static uint32_t scrolled;
static char input[TRAIT_TERM_LINE_BYTES];
static uint32_t input_length;

static void copy(char *out, const char *text, uint32_t capacity)
{
    uint32_t at = 0U;

    while (text != NULL && text[at] != '\0' && at + 1U < capacity) {
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
}

static void append(char *out, const char *text, uint32_t capacity)
{
    uint32_t at = 0U;
    uint32_t from = 0U;

    while (out[at] != '\0') {
        ++at;
    }
    while (text[from] != '\0' && at + 1U < capacity) {
        out[at++] = text[from++];
    }
    out[at] = '\0';
}

static bool same(const char *a, const char *b)
{
    uint32_t at = 0U;

    while (a[at] != '\0' && b[at] != '\0') {
        if (a[at] != b[at]) {
            return false;
        }
        ++at;
    }
    return a[at] == b[at];
}

void trait_terminal_reset(void)
{
    line_count = 0U;
    scrolled = 0U;
    input_length = 0U;
    input[0] = '\0';
}

void trait_terminal_type(char ch)
{
    /* Printable only.  A control character in the line buffer would be
     * drawn as nothing and counted as something, so the cursor would sit
     * one place right of where the text ends. */
    if (ch < 32 || ch > 126) {
        return;
    }
    if (input_length + 1U >= TRAIT_TERM_LINE_BYTES) {
        return;
    }
    input[input_length++] = ch;
    input[input_length] = '\0';
}

void trait_terminal_backspace(void)
{
    if (input_length == 0U) {
        return;
    }
    input[--input_length] = '\0';
}

void trait_terminal_enter(void)
{
    char held[TRAIT_TERM_LINE_BYTES];

    /* The line is cleared BEFORE it runs, not after: `clear` empties the
     * screen, and a line cleared afterwards would put the command back
     * on a screen it had just wiped. */
    copy(held, input, sizeof(held));
    input_length = 0U;
    input[0] = '\0';
    trait_terminal_run(held);
}

const char *trait_terminal_input(void)
{
    return input;
}

/* The scrollback is a window, not a buffer: once it is full the oldest
 * line goes, which is what a terminal of this size does. */
static void print_inked(const char *line, const char *ink)
{
    uint32_t at;
    uint32_t column;

    if (line_count >= TRAIT_TERM_HISTORY) {
        for (at = 1U; at < TRAIT_TERM_HISTORY; ++at) {
            copy(lines[at - 1U], lines[at], TRAIT_TERM_LINE_BYTES);
            for (column = 0U; column < TRAIT_TERM_LINE_BYTES; ++column) {
                inks[at - 1U][column] = inks[at][column];
            }
        }
        line_count = TRAIT_TERM_HISTORY - 1U;
    }
    /* Anything printed brings the view back to the bottom, which is what
     * a terminal does: output you scrolled away from is not output you
     * want to miss. */
    scrolled = 0U;
    copy(lines[line_count], line == NULL ? "" : line,
         TRAIT_TERM_LINE_BYTES);
    for (column = 0U; column < TRAIT_TERM_LINE_BYTES; ++column) {
        uint8_t code = 0U;

        /* An ink string shorter than the line inks the rest of it in the
         * terminal's own foreground, which is what the facts beside the
         * mark want. */
        if (ink != NULL && column < TRAIT_TERM_LINE_BYTES) {
            uint32_t scan = 0U;

            while (scan < column && ink[scan] != '\0') {
                ++scan;
            }
            /* HEX, because there are more than ten inks now.  The
             * generator writes 0-9 then A-F, which is one character a
             * cell either way - an ink plane wider than the line it
             * inks would not line up with it. */
            if (scan == column) {
                char digit = ink[column];
                uint8_t value = TERM_INKS;

                if (digit >= '0' && digit <= '9') {
                    value = (uint8_t)(digit - '0');
                } else if (digit >= 'A' && digit <= 'F') {
                    value = (uint8_t)(digit - 'A' + 10);
                }
                if (value < TERM_INKS) {
                    code = value;
                }
            }
        }
        inks[line_count][column] = code;
    }
    ++line_count;
}

void trait_terminal_print(const char *line)
{
    print_inked(line, NULL);
}

uint32_t trait_terminal_row_count(void)
{
    return line_count;
}

void trait_terminal_scroll(int32_t by)
{
    /* Clamped at BOTH ends: past the top there is nothing to show, and
     * past the bottom the prompt would float off the foot of the
     * window. */
    uint32_t most = line_count > TRAIT_TERM_ROWS ?
        line_count - TRAIT_TERM_ROWS : 0U;

    if (by < 0) {
        uint32_t back = (uint32_t)(-by);

        scrolled = scrolled > back ? scrolled - back : 0U;
    } else {
        scrolled += (uint32_t)by;
    }
    if (scrolled > most) {
        scrolled = most;
    }
}

uint32_t trait_terminal_scrolled(void)
{
    return scrolled;
}

const char *trait_terminal_row(uint32_t at)
{
    if (at >= line_count) {
        return "";
    }
    return lines[at];
}

/*
 * gfetch, which is this desktop's fetch.
 *
 * EVERY LINE IS READ FROM SOMETHING.  A fetch is a screenful of facts
 * about the machine it is run on, and the one failure mode it has is
 * printing a figure nobody can trace - so the theme comes from
 * trait_theme(), the font from the generated face's own metrics, the
 * terminal size from the constants the terminal lays itself out with,
 * and the package count from the package manager.  Nothing here is
 * typed in twice.
 *
 * The mark goes on the left the way screenfetch put Tux there.  It is
 * the OpenRFS fish, reduced to characters by tools/make-logo.py from
 * the owner's own drawing.
 */
#define GFETCH_GAP 2U

static void number(char *out, uint32_t value, uint32_t room)
{
    char digits[12];
    uint32_t at = 0U;
    uint32_t back;

    if (value == 0U) {
        digits[at++] = '0';
    }
    while (value > 0U && at < sizeof(digits)) {
        digits[at++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    out[0] = '\0';
    for (back = at; back > 0U; --back) {
        char one[2];

        one[0] = digits[back - 1U];
        one[1] = '\0';
        append(out, one, room);
    }
}

static void gfetch(void)
{
    /* The facts, built once so the loop below can put them beside the
     * rows of the mark rather than under it. */
    char facts[10][TRAIT_TERM_LINE_BYTES];
    uint32_t count = 0U;
    uint32_t at;

    copy(facts[count], "user@openrfs", sizeof(facts[0]));
    ++count;
    copy(facts[count], "------------", sizeof(facts[0]));
    ++count;
    copy(facts[count], "OS      OpenRFS", sizeof(facts[0]));
    ++count;
    copy(facts[count], "Kernel  ", sizeof(facts[0]));
    append(facts[count], KERNEL, sizeof(facts[0]));
    ++count;
    copy(facts[count], "WM      openrfs, no reparenting",
         sizeof(facts[0]));
    ++count;
    copy(facts[count], "Shell   ", sizeof(facts[0]));
    append(facts[count], "openrfs", sizeof(facts[0]));
    ++count;
    copy(facts[count], "Theme   ", sizeof(facts[0]));
    append(facts[count], trait_theme_name(trait_theme_selected()),
           sizeof(facts[0]));
    ++count;
    {
        char figure[12];

        copy(facts[count], "Font    Misc-Fixed 8x", sizeof(facts[0]));
        number(figure, TRAIT_MONO_HEIGHT, sizeof(figure));
        append(facts[count], figure, sizeof(facts[0]));
        ++count;

        copy(facts[count], "Term    ", sizeof(facts[0]));
        number(figure, TRAIT_TERM_COLUMNS, sizeof(figure));
        append(facts[count], figure, sizeof(facts[0]));
        append(facts[count], "x", sizeof(facts[0]));
        number(figure, TRAIT_TERM_ROWS, sizeof(figure));
        append(facts[count], figure, sizeof(facts[0]));
        ++count;

        copy(facts[count], "Pkgs    ", sizeof(facts[0]));
        number(figure, trait_packages_installed_count(),
               sizeof(figure));
        append(facts[count], figure, sizeof(facts[0]));
        append(facts[count], " installed of ", sizeof(facts[0]));
        number(figure, trait_packages_count(), sizeof(figure));
        append(facts[count], figure, sizeof(facts[0]));
        ++count;
    }

    /*
     * AS MANY ROWS AS THERE ARE OF EITHER.  Running to the mark's
     * height alone loses a fact whenever the mark is shorter than the
     * list - which is a fetch that silently drops a line the moment
     * somebody resizes the logo, and it did.
     */
    for (at = 0U; at < TRAIT_LOGO_ROWS || at < count; ++at) {
        char line[TRAIT_TERM_LINE_BYTES];
        char ink[TRAIT_TERM_LINE_BYTES];
        uint32_t width = 0U;

        copy(line, at < TRAIT_LOGO_ROWS ? trait_logo[at] : "",
             sizeof(line));
        while (line[width] != '\0') {
            ++width;
        }
        /* Pad out to the mark's full width so the facts line up in a
         * column rather than following each row's ragged right edge. */
        while (width < TRAIT_LOGO_COLUMNS + GFETCH_GAP &&
                width + 1U < sizeof(line)) {
            line[width] = ' ';
            ++width;
            line[width] = '\0';
        }
        copy(ink, at < TRAIT_LOGO_ROWS ? trait_logo_at[at] : "",
             sizeof(ink));
        if (at < count) {
            append(line, facts[at], sizeof(line));
        }
        /* A row with nothing on either side is not printed at all: a
         * fetch that ends in six blank lines has padded its own
         * output. */
        {
            uint32_t last = 0U;
            uint32_t scan = 0U;

            while (line[scan] != '\0') {
                if (line[scan] != ' ') {
                    last = scan + 1U;
                }
                ++scan;
            }
            line[last] = '\0';
        }
        print_inked(line, ink);
    }
}

void trait_terminal_set_opacity(uint32_t alpha)
{
    term_opacity = alpha > TERM_OPAQUE ? TERM_OPAQUE : alpha;
}

uint32_t trait_terminal_opacity(void)
{
    return term_opacity;
}

bool trait_terminal_transparent(void)
{
    return term_opacity < TERM_OPAQUE;
}

void trait_terminal_set_transparent(bool sheer)
{
    term_opacity = sheer ? TERM_SHEER : TERM_OPAQUE;
}

/*
 * The commands this shell actually has.  A command that is not here says
 * so the way a shell does - "command not found" - rather than printing
 * nothing, because a terminal that swallows what it cannot do is worse
 * than one that admits it.
 */
void trait_terminal_run(const char *command)
{
    char echo[TRAIT_TERM_LINE_BYTES];

    if (command == NULL) {
        return;
    }
    copy(echo, PROMPT, sizeof(echo));
    append(echo, command, sizeof(echo));
    trait_terminal_print(echo);

    if (same(command, "uname -s")) {
        trait_terminal_print("OpenRFS");
    } else if (same(command, "uname -a")) {
        trait_terminal_print(KERNEL);
    } else if (same(command, "pwd")) {
        trait_terminal_print("/home/user");
    } else if (same(command, "whoami")) {
        trait_terminal_print("user");
    } else if (same(command, "ls")) {
        trait_terminal_print("Desktop    Documents  Downloads  Music");
        trait_terminal_print("Pictures   Videos     README.txt");
    } else if (same(command, "free -h")) {
        trait_terminal_print("               total        used        free");
        trait_terminal_print("Mem:            62Mi       9.5Mi        52Mi");
    } else if (same(command, "gfetch")) {
        gfetch();
    } else if (same(command, "clear")) {
        trait_terminal_reset();
    } else if (command[0] == '\0') {
        /* An empty line is a new prompt and nothing else, which is what
         * pressing return at a shell does. */
        return;
    } else {
        char complaint[TRAIT_TERM_LINE_BYTES];

        copy(complaint, "bash: ", sizeof(complaint));
        append(complaint, command, sizeof(complaint));
        append(complaint, ": command not found", sizeof(complaint));
        trait_terminal_print(complaint);
    }
}

static const struct trait_glyph *glyph_for(char ch)
{
    uint32_t code = (uint32_t)(unsigned char)ch;

    if (code < TRAIT_MONO_FIRST || code > TRAIT_MONO_LAST) {
        return NULL;
    }
    return &trait_mono[code - TRAIT_MONO_FIRST];
}

/*
 * A HALO UNDER THE TEXT WHILE THE TERMINAL IS SEE-THROUGH.
 *
 * Shading the ground is the whole of what pseudo-transparency is, and
 * it has one failure: the ground is whatever is BEHIND the window, so a
 * pale window behind a sheer terminal lifts the ground to near white
 * and white text on it disappears.  Going whiter cannot fix that - it
 * is already white.
 *
 * So the text carries its own dark ground, one pixel down and right,
 * the way pcmanfm's desktop labels do over a wallpaper that is light in
 * one place and dark in another.  It is the same problem and it is the
 * same answer, and it is what lets the ground go much clearer than the
 * ink alone could stand.
 */
static uint32_t draw_run(struct trait_surface *surface,
    struct trait_rect clip, uint32_t x, uint32_t baseline,
    const char *text, uint32_t colour);

static uint32_t draw_mono(struct trait_surface *surface,
    struct trait_rect clip, uint32_t x, uint32_t baseline,
    const char *text, uint32_t colour)
{
    if (term_opacity < TERM_OPAQUE) {
        (void)draw_run(surface, clip, x + 1U, baseline + 1U, text,
                       0x000000U);
    }
    return draw_run(surface, clip, x, baseline, text, colour);
}

static uint32_t draw_run(struct trait_surface *surface,
    struct trait_rect clip, uint32_t x, uint32_t baseline,
    const char *text, uint32_t colour)
{
    uint32_t pen = x;
    uint32_t at;

    for (at = 0U; text[at] != '\0'; ++at) {
        const struct trait_glyph *glyph = glyph_for(text[at]);
        uint32_t top = baseline - TRAIT_MONO_ASCENT;
        uint32_t row;
        uint32_t column;

        if (glyph == NULL) {
            continue;
        }
        for (row = 0U; row < TRAIT_MONO_HEIGHT; ++row) {
            for (column = 0U; column < glyph->width; ++column) {
                uint32_t alpha =
                    glyph->coverage[row * glyph->width + column];
                uint32_t under;

                if (alpha == 0U) {
                    continue;
                }
                under = trait_surface_read(surface, pen + column,
                                           top + row);
                trait_surface_plot(surface, clip, pen + column, top + row,
                                   trait_blend(under, colour, alpha));
            }
        }
        pen += glyph->advance;
    }
    return pen;
}

void trait_terminal_draw(struct trait_surface *surface,
    const struct trait_window *window)
{
    struct trait_rect client;
    uint32_t at;
    uint32_t first = 0U;
    uint32_t shown = 0U;
    uint32_t advance = trait_mono[0].advance;

    if (window == NULL || !trait_surface_valid(surface)) {
        return;
    }
    client = trait_window_client(window);
    trait_surface_wash(surface, client, client, TERM_GROUND,
                       term_opacity);
    {
        /* The window of history that is on screen: the last TERM_ROWS
         * lines, moved back by however far it has been scrolled. */
        uint32_t most = line_count > TRAIT_TERM_ROWS ?
            line_count - TRAIT_TERM_ROWS : 0U;

        first = most > scrolled ? most - scrolled : 0U;
        shown = line_count - first;
        if (shown > TRAIT_TERM_ROWS) {
            shown = TRAIT_TERM_ROWS;
        }
    }
    for (at = 0U; at < shown; ++at) {
        uint32_t baseline = client.y + TERM_PAD + TRAIT_MONO_ASCENT +
            at * TRAIT_MONO_HEIGHT;

        if (baseline + TRAIT_MONO_DESCENT > client.y + client.height) {
            break;
        }
        {
            /* One run per ink rather than one call per line: a row of
             * the mark is mostly one colour, so this is a handful of
             * calls and not one per character. */
            const char *text = lines[first + at];
            const uint8_t *ink = inks[first + at];
            uint32_t pen = client.x + TERM_PAD;
            uint32_t from = 0U;

            while (text[from] != '\0') {
                char run[TRAIT_TERM_LINE_BYTES];
                uint32_t to = from;
                uint32_t length = 0U;

                while (text[to] != '\0' && ink[to] == ink[from] &&
                        length + 1U < sizeof(run)) {
                    run[length++] = text[to];
                    ++to;
                }
                run[length] = '\0';
                pen = draw_mono(surface, client, pen, baseline, run,
                    ink_of(ink[from] < TERM_INKS ? ink[from] : 0U));
                from = to;
            }
        }
    }
    /* The live prompt, and a BLOCK cursor after it - the old terminal's
     * cursor, not a thin bar. */
    {
        /* The prompt sits after the last SHOWN line, not the last line
         * held: scrolled back, it belongs off the bottom with the output
         * it comes after. */
        uint32_t baseline = client.y + TERM_PAD + TRAIT_MONO_ASCENT +
            shown * TRAIT_MONO_HEIGHT;
        struct trait_rect cursor;
        uint32_t pen;

        if (baseline + TRAIT_MONO_DESCENT <= client.y + client.height) {
            pen = draw_mono(surface, client, client.x + TERM_PAD, baseline,
                            PROMPT, term_ink());
            pen = draw_mono(surface, client, pen, baseline, input,
                            term_ink());
            cursor.x = pen;
            cursor.y = baseline - TRAIT_MONO_ASCENT + 2U;
            cursor.width = advance;
            cursor.height = TRAIT_MONO_HEIGHT - 3U;
            trait_surface_fill(surface, client, cursor, term_ink());
        }
    }
}

/*
 * The self test asks the one thing that separates a terminal from a
 * picture of one: does typing at it change what is on it, and does a
 * command it has not got SAY SO rather than quietly doing nothing?
 */
bool trait_terminal_self_test(void)
{
    trait_terminal_reset();
    if (trait_terminal_row_count() != 0U) {
        return false;
    }
    trait_terminal_run("whoami");
    /* The echo and the answer: two lines, not one. */
    if (trait_terminal_row_count() != 2U) {
        return false;
    }
    if (!same(trait_terminal_row(1U), "user")) {
        return false;
    }
    trait_terminal_run("frobnicate");
    if (trait_terminal_row_count() != 4U) {
        return false;
    }
    if (!same(trait_terminal_row(3U),
              "bash: frobnicate: command not found")) {
        return false;
    }
    /* clear empties it rather than printing the word "clear". */
    trait_terminal_run("clear");
    if (trait_terminal_row_count() != 0U) {
        return false;
    }
    /*
     * The history drops the OLDEST line when it is full, so the last line
     * written is always the last line held.
     *
     * This used to fill to TRAIT_TERM_ROWS and assert the buffer capped
     * there, which was right when the terminal kept exactly what was on
     * screen and became wrong the moment it got scrollback - the check
     * was describing the absence of the feature.  It fills the whole
     * HISTORY now.
     */
    for (uint32_t at = 0U; at < TRAIT_TERM_HISTORY + 4U; ++at) {
        trait_terminal_print(at + 1U == TRAIT_TERM_HISTORY + 4U ?
                             "last" : "filler");
    }
    if (trait_terminal_row_count() != TRAIT_TERM_HISTORY) {
        return false;
    }
    if (!same(trait_terminal_row(TRAIT_TERM_HISTORY - 1U), "last")) {
        return false;
    }
    trait_terminal_reset();

    /* Typing puts characters on the line and nothing on the screen; only
     * return commits it. */
    trait_terminal_type('p');
    trait_terminal_type('w');
    trait_terminal_type('d');
    if (!same(trait_terminal_input(), "pwd")) {
        return false;
    }
    if (trait_terminal_row_count() != 0U) {
        return false;
    }
    trait_terminal_backspace();
    if (!same(trait_terminal_input(), "pw")) {
        return false;
    }
    trait_terminal_type('d');
    trait_terminal_enter();
    if (trait_terminal_row_count() != 2U) {
        return false;
    }
    if (!same(trait_terminal_row(1U), "/home/user")) {
        return false;
    }
    /* Return leaves the line EMPTY, or the next command is typed onto
     * the end of the last one. */
    if (!same(trait_terminal_input(), "")) {
        return false;
    }
    /* Backspace on an empty line does nothing rather than running off
     * the front of the buffer. */
    trait_terminal_backspace();
    if (!same(trait_terminal_input(), "")) {
        return false;
    }
    /* A control character is refused, so the cursor cannot end up right
     * of where the text is. */
    trait_terminal_type('\t');
    if (!same(trait_terminal_input(), "")) {
        return false;
    }
    /* `clear` typed and entered empties the screen and leaves nothing
     * behind - including itself. */
    trait_terminal_type('c');
    trait_terminal_type('l');
    trait_terminal_type('e');
    trait_terminal_type('a');
    trait_terminal_type('r');
    trait_terminal_enter();
    if (trait_terminal_row_count() != 0U) {
        return false;
    }
    /* Scrollback: more history than fits, and a view that moves. */
    {
        uint32_t at;

        trait_terminal_reset();
        for (at = 0U; at < TRAIT_TERM_ROWS + 10U; ++at) {
            trait_terminal_print(at == 0U ? "first" :
                (at + 1U == TRAIT_TERM_ROWS + 10U ? "last" : "middle"));
        }
        /* Nothing was dropped - the history holds more than a screen. */
        if (trait_terminal_row_count() != TRAIT_TERM_ROWS + 10U) {
            return false;
        }
        if (!same(trait_terminal_row(0U), "first")) {
            return false;
        }
        if (trait_terminal_scrolled() != 0U) {
            return false;
        }
        trait_terminal_scroll(5);
        if (trait_terminal_scrolled() != 5U) {
            return false;
        }
        /* It CLAMPS at the top rather than running off the front. */
        trait_terminal_scroll(500);
        if (trait_terminal_scrolled() != 10U) {
            return false;
        }
        /* And at the bottom. */
        trait_terminal_scroll(-500);
        if (trait_terminal_scrolled() != 0U) {
            return false;
        }
        /* Printing brings the view back down: output you scrolled away
         * from is not output you want to miss. */
        trait_terminal_scroll(6);
        trait_terminal_print("something happened");
        if (trait_terminal_scrolled() != 0U) {
            return false;
        }
    }
    trait_terminal_reset();
    return true;
}
