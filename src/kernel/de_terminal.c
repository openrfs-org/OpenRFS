/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/de/terminal.h>

#include <openrfs/de/theme.h>

#include "de_openrfs_mono.h"

#define OPENRFS_MONO_COUNT (sizeof(openrfs_mono) / sizeof(openrfs_mono[0]))

/* lxterminal's own defaults, out of its .desktop and its preferences:
 * a black ground and a light grey ink, which is the pair it ships. */
#define TERM_GROUND 0x000000U
#define TERM_INK 0xD3D7CFU
#define TERM_PROMPT_INK 0xD3D7CFU
#define TERM_PAD 4U

static const char PROMPT[] = "user@openrfs:~$ ";

static char lines[OPENRFS_TERM_HISTORY][OPENRFS_TERM_LINE_BYTES];
static uint32_t line_count;
static uint32_t scrolled;
static char input[OPENRFS_TERM_LINE_BYTES];
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

void openrfs_terminal_reset(void)
{
    line_count = 0U;
    scrolled = 0U;
    input_length = 0U;
    input[0] = '\0';
}

void openrfs_terminal_type(char ch)
{
    /* Printable only.  A control character in the line buffer would be
     * drawn as nothing and counted as something, so the cursor would sit
     * one place right of where the text ends. */
    if (ch < 32 || ch > 126) {
        return;
    }
    if (input_length + 1U >= OPENRFS_TERM_LINE_BYTES) {
        return;
    }
    input[input_length++] = ch;
    input[input_length] = '\0';
}

void openrfs_terminal_backspace(void)
{
    if (input_length == 0U) {
        return;
    }
    input[--input_length] = '\0';
}

void openrfs_terminal_enter(void)
{
    char held[OPENRFS_TERM_LINE_BYTES];

    /* The line is cleared BEFORE it runs, not after: `clear` empties the
     * screen, and a line cleared afterwards would put the command back
     * on a screen it had just wiped. */
    copy(held, input, sizeof(held));
    input_length = 0U;
    input[0] = '\0';
    openrfs_terminal_run(held);
}

const char *openrfs_terminal_input(void)
{
    return input;
}

/* The scrollback is a window, not a buffer: once it is full the oldest
 * line goes, which is what a terminal of this size does. */
void openrfs_terminal_print(const char *line)
{
    uint32_t at;

    if (line_count >= OPENRFS_TERM_HISTORY) {
        for (at = 1U; at < OPENRFS_TERM_HISTORY; ++at) {
            copy(lines[at - 1U], lines[at], OPENRFS_TERM_LINE_BYTES);
        }
        line_count = OPENRFS_TERM_HISTORY - 1U;
    }
    /* Anything printed brings the view back to the bottom, which is what
     * a terminal does: output you scrolled away from is not output you
     * want to miss. */
    scrolled = 0U;
    copy(lines[line_count++], line == NULL ? "" : line,
         OPENRFS_TERM_LINE_BYTES);
}

uint32_t openrfs_terminal_row_count(void)
{
    return line_count;
}

void openrfs_terminal_scroll(int32_t by)
{
    /* Clamped at BOTH ends: past the top there is nothing to show, and
     * past the bottom the prompt would float off the foot of the
     * window. */
    uint32_t most = line_count > OPENRFS_TERM_ROWS ?
        line_count - OPENRFS_TERM_ROWS : 0U;

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

uint32_t openrfs_terminal_scrolled(void)
{
    return scrolled;
}

const char *openrfs_terminal_row(uint32_t at)
{
    if (at >= line_count) {
        return "";
    }
    return lines[at];
}

/*
 * The commands this shell actually has.  A command that is not here says
 * so the way a shell does - "command not found" - rather than printing
 * nothing, because a terminal that swallows what it cannot do is worse
 * than one that admits it.
 */
void openrfs_terminal_run(const char *command)
{
    char echo[OPENRFS_TERM_LINE_BYTES];

    if (command == NULL) {
        return;
    }
    copy(echo, PROMPT, sizeof(echo));
    append(echo, command, sizeof(echo));
    openrfs_terminal_print(echo);

    if (same(command, "uname -s")) {
        openrfs_terminal_print("OpenRFS");
    } else if (same(command, "uname -a")) {
        openrfs_terminal_print("OpenRFS openrfs 1.0 x86_64 Unix-like");
    } else if (same(command, "pwd")) {
        openrfs_terminal_print("/home/user");
    } else if (same(command, "whoami")) {
        openrfs_terminal_print("user");
    } else if (same(command, "ls")) {
        openrfs_terminal_print("Desktop    Documents  Downloads  Music");
        openrfs_terminal_print("Pictures   Videos     README.txt");
    } else if (same(command, "free -h")) {
        openrfs_terminal_print("               total        used        free");
        openrfs_terminal_print("Mem:            62Mi       9.5Mi        52Mi");
    } else if (same(command, "clear")) {
        openrfs_terminal_reset();
    } else if (command[0] == '\0') {
        /* An empty line is a new prompt and nothing else, which is what
         * pressing return at a shell does. */
        return;
    } else {
        char complaint[OPENRFS_TERM_LINE_BYTES];

        copy(complaint, "bash: ", sizeof(complaint));
        append(complaint, command, sizeof(complaint));
        append(complaint, ": command not found", sizeof(complaint));
        openrfs_terminal_print(complaint);
    }
}

static const struct openrfs_glyph *glyph_for(char ch)
{
    uint32_t code = (uint32_t)(unsigned char)ch;

    if (code < OPENRFS_MONO_FIRST || code > OPENRFS_MONO_LAST) {
        return NULL;
    }
    return &openrfs_mono[code - OPENRFS_MONO_FIRST];
}

static uint32_t draw_mono(struct openrfs_surface *surface,
    struct openrfs_rect clip, uint32_t x, uint32_t baseline,
    const char *text, uint32_t colour)
{
    uint32_t pen = x;
    uint32_t at;

    for (at = 0U; text[at] != '\0'; ++at) {
        const struct openrfs_glyph *glyph = glyph_for(text[at]);
        uint32_t top = baseline - OPENRFS_MONO_ASCENT;
        uint32_t row;
        uint32_t column;

        if (glyph == NULL) {
            continue;
        }
        for (row = 0U; row < OPENRFS_MONO_HEIGHT; ++row) {
            for (column = 0U; column < glyph->width; ++column) {
                uint32_t alpha =
                    glyph->coverage[row * glyph->width + column];
                uint32_t under;

                if (alpha == 0U) {
                    continue;
                }
                under = openrfs_surface_read(surface, pen + column,
                                           top + row);
                openrfs_surface_plot(surface, clip, pen + column, top + row,
                                   openrfs_blend(under, colour, alpha));
            }
        }
        pen += glyph->advance;
    }
    return pen;
}

void openrfs_terminal_draw(struct openrfs_surface *surface,
    const struct openrfs_window *window)
{
    struct openrfs_rect client;
    uint32_t at;
    uint32_t first = 0U;
    uint32_t shown = 0U;
    uint32_t advance = openrfs_mono[0].advance;

    if (window == NULL || !openrfs_surface_valid(surface)) {
        return;
    }
    client = openrfs_window_client(window);
    openrfs_surface_fill(surface, client, client, TERM_GROUND);
    {
        /* The window of history that is on screen: the last TERM_ROWS
         * lines, moved back by however far it has been scrolled. */
        uint32_t most = line_count > OPENRFS_TERM_ROWS ?
            line_count - OPENRFS_TERM_ROWS : 0U;

        first = most > scrolled ? most - scrolled : 0U;
        shown = line_count - first;
        if (shown > OPENRFS_TERM_ROWS) {
            shown = OPENRFS_TERM_ROWS;
        }
    }
    for (at = 0U; at < shown; ++at) {
        uint32_t baseline = client.y + TERM_PAD + OPENRFS_MONO_ASCENT +
            at * OPENRFS_MONO_HEIGHT;

        if (baseline + OPENRFS_MONO_DESCENT > client.y + client.height) {
            break;
        }
        (void)draw_mono(surface, client, client.x + TERM_PAD, baseline,
                        lines[first + at], TERM_INK);
    }
    /* The live prompt, and a BLOCK cursor after it - the old terminal's
     * cursor, not a thin bar. */
    {
        /* The prompt sits after the last SHOWN line, not the last line
         * held: scrolled back, it belongs off the bottom with the output
         * it comes after. */
        uint32_t baseline = client.y + TERM_PAD + OPENRFS_MONO_ASCENT +
            shown * OPENRFS_MONO_HEIGHT;
        struct openrfs_rect cursor;
        uint32_t pen;

        if (baseline + OPENRFS_MONO_DESCENT <= client.y + client.height) {
            pen = draw_mono(surface, client, client.x + TERM_PAD, baseline,
                            PROMPT, TERM_PROMPT_INK);
            pen = draw_mono(surface, client, pen, baseline, input,
                            TERM_INK);
            cursor.x = pen;
            cursor.y = baseline - OPENRFS_MONO_ASCENT + 2U;
            cursor.width = advance;
            cursor.height = OPENRFS_MONO_HEIGHT - 3U;
            openrfs_surface_fill(surface, client, cursor, TERM_INK);
        }
    }
}

/*
 * The self test asks the one thing that separates a terminal from a
 * picture of one: does typing at it change what is on it, and does a
 * command it has not got SAY SO rather than quietly doing nothing?
 */
bool openrfs_terminal_self_test(void)
{
    openrfs_terminal_reset();
    if (openrfs_terminal_row_count() != 0U) {
        return false;
    }
    openrfs_terminal_run("whoami");
    /* The echo and the answer: two lines, not one. */
    if (openrfs_terminal_row_count() != 2U) {
        return false;
    }
    if (!same(openrfs_terminal_row(1U), "user")) {
        return false;
    }
    openrfs_terminal_run("frobnicate");
    if (openrfs_terminal_row_count() != 4U) {
        return false;
    }
    if (!same(openrfs_terminal_row(3U),
              "bash: frobnicate: command not found")) {
        return false;
    }
    /* clear empties it rather than printing the word "clear". */
    openrfs_terminal_run("clear");
    if (openrfs_terminal_row_count() != 0U) {
        return false;
    }
    /*
     * The history drops the OLDEST line when it is full, so the last line
     * written is always the last line held.
     *
     * This used to fill to OPENRFS_TERM_ROWS and assert the buffer capped
     * there, which was right when the terminal kept exactly what was on
     * screen and became wrong the moment it got scrollback - the check
     * was describing the absence of the feature.  It fills the whole
     * HISTORY now.
     */
    for (uint32_t at = 0U; at < OPENRFS_TERM_HISTORY + 4U; ++at) {
        openrfs_terminal_print(at + 1U == OPENRFS_TERM_HISTORY + 4U ?
                             "last" : "filler");
    }
    if (openrfs_terminal_row_count() != OPENRFS_TERM_HISTORY) {
        return false;
    }
    if (!same(openrfs_terminal_row(OPENRFS_TERM_HISTORY - 1U), "last")) {
        return false;
    }
    openrfs_terminal_reset();

    /* Typing puts characters on the line and nothing on the screen; only
     * return commits it. */
    openrfs_terminal_type('p');
    openrfs_terminal_type('w');
    openrfs_terminal_type('d');
    if (!same(openrfs_terminal_input(), "pwd")) {
        return false;
    }
    if (openrfs_terminal_row_count() != 0U) {
        return false;
    }
    openrfs_terminal_backspace();
    if (!same(openrfs_terminal_input(), "pw")) {
        return false;
    }
    openrfs_terminal_type('d');
    openrfs_terminal_enter();
    if (openrfs_terminal_row_count() != 2U) {
        return false;
    }
    if (!same(openrfs_terminal_row(1U), "/home/user")) {
        return false;
    }
    /* Return leaves the line EMPTY, or the next command is typed onto
     * the end of the last one. */
    if (!same(openrfs_terminal_input(), "")) {
        return false;
    }
    /* Backspace on an empty line does nothing rather than running off
     * the front of the buffer. */
    openrfs_terminal_backspace();
    if (!same(openrfs_terminal_input(), "")) {
        return false;
    }
    /* A control character is refused, so the cursor cannot end up right
     * of where the text is. */
    openrfs_terminal_type('\t');
    if (!same(openrfs_terminal_input(), "")) {
        return false;
    }
    /* `clear` typed and entered empties the screen and leaves nothing
     * behind - including itself. */
    openrfs_terminal_type('c');
    openrfs_terminal_type('l');
    openrfs_terminal_type('e');
    openrfs_terminal_type('a');
    openrfs_terminal_type('r');
    openrfs_terminal_enter();
    if (openrfs_terminal_row_count() != 0U) {
        return false;
    }
    /* Scrollback: more history than fits, and a view that moves. */
    {
        uint32_t at;

        openrfs_terminal_reset();
        for (at = 0U; at < OPENRFS_TERM_ROWS + 10U; ++at) {
            openrfs_terminal_print(at == 0U ? "first" :
                (at + 1U == OPENRFS_TERM_ROWS + 10U ? "last" : "middle"));
        }
        /* Nothing was dropped - the history holds more than a screen. */
        if (openrfs_terminal_row_count() != OPENRFS_TERM_ROWS + 10U) {
            return false;
        }
        if (!same(openrfs_terminal_row(0U), "first")) {
            return false;
        }
        if (openrfs_terminal_scrolled() != 0U) {
            return false;
        }
        openrfs_terminal_scroll(5);
        if (openrfs_terminal_scrolled() != 5U) {
            return false;
        }
        /* It CLAMPS at the top rather than running off the front. */
        openrfs_terminal_scroll(500);
        if (openrfs_terminal_scrolled() != 10U) {
            return false;
        }
        /* And at the bottom. */
        openrfs_terminal_scroll(-500);
        if (openrfs_terminal_scrolled() != 0U) {
            return false;
        }
        /* Printing brings the view back down: output you scrolled away
         * from is not output you want to miss. */
        openrfs_terminal_scroll(6);
        openrfs_terminal_print("something happened");
        if (openrfs_terminal_scrolled() != 0U) {
            return false;
        }
    }
    openrfs_terminal_reset();
    return true;
}
