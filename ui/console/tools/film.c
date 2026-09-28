/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * A film of one whole installation.
 *
 * It presses the same keys through the same entry point the harness
 * does - this is not a scripted animation of what the installer looks
 * like, it is the installer, photographed. If a screen in the film is
 * wrong, the installer is wrong.
 *
 * One PNG per distinct screen state, and a line of milliseconds beside
 * it. Holding a screen by writing the same picture twenty times would
 * make a file twenty times the size for a pause the format already
 * knows how to express.
 */
#include <rsd/install.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "render.h"

#define DIR "../screens/film"

static int frames;
static FILE *timing;

/* One frame, held for `ms`. */
static void hold(const struct rsd_install *in, int ms)
{
    char path[160];

    snprintf(path, sizeof(path), DIR "/f%04d.png", frames);
    if (rsd_render(&in->term, in->term.cursor, path) != 0) {
        return;
    }
    fprintf(timing, "%d\n", ms);
    ++frames;
}

/* A keystroke, and the screen it produced. */
static void press(struct rsd_install *in, int key, int ms)
{
    rsd_install_key(in, key);
    hold(in, ms);
}

/* Typed one character at a time, because that is how it is typed. */
static void typewrite(struct rsd_install *in, const char *text, int ms)
{
    for (uint32_t at = 0U; text[at] != '\0'; ++at) {
        press(in, (int)(unsigned char)text[at], ms);
    }
}

/* A gauge, a frame per tick, until the installer has moved on. */
static void run_gauge(struct rsd_install *in, int ms)
{
    enum rsd_step started = in->step;
    int guard = 0;

    while (in->step == started && guard++ < 64) {
        rsd_install_tick(in);
        hold(in, ms);
    }
}

int main(void)
{
    struct rsd_install in;

    (void)mkdir("../screens", 0755);
    (void)mkdir(DIR, 0755);
    timing = fopen(DIR "/timing.txt", "w");
    if (timing == NULL) {
        fprintf(stderr, "cannot write " DIR "/timing.txt\n");
        return 1;
    }

    rsd_install_begin(&in);
    hold(&in, 992);                       /* keymap */
    press(&in, RSD_KEY_DOWN, 198);
    press(&in, RSD_KEY_DOWN, 198);
    press(&in, RSD_KEY_DOWN, 198);
    press(&in, RSD_KEY_DOWN, 434);        /* ar, and its own help line */
    press(&in, RSD_KEY_UP, 161);
    press(&in, RSD_KEY_UP, 161);
    press(&in, RSD_KEY_UP, 161);
    press(&in, RSD_KEY_UP, 434);          /* back to us */
    press(&in, '\r', 930);                /* welcome */

    press(&in, '\r', 558);                 /* Install -> hostname */
    for (int i = 0; i < 7; ++i) {
        press(&in, 0x08, 68);             /* clear the default */
    }
    typewrite(&in, "reef", 105);
    hold(&in, 558);
    press(&in, '\r', 682);                /* components */

    press(&in, RSD_KEY_DOWN, 186);
    press(&in, RSD_KEY_DOWN, 186);
    press(&in, RSD_KEY_DOWN, 310);
    press(&in, ' ', 434);                  /* ports */
    press(&in, RSD_KEY_DOWN, 248);
    press(&in, ' ', 558);                  /* src */
    press(&in, '\r', 806);                /* partitioning */

    press(&in, RSD_KEY_DOWN, 558);        /* Edit, and its help line */
    press(&in, RSD_KEY_F1, 1612);         /* what F1 is for */
    press(&in, 0x1B, 558);                 /* back where it was pressed */
    press(&in, RSD_KEY_UP, 434);
    press(&in, '\r', 682);                /* disk */
    press(&in, '\r', 620);                /* scheme */
    press(&in, RSD_KEY_DOWN, 372);
    press(&in, RSD_KEY_UP, 434);
    press(&in, '\r', 1364);                /* review: read it */

    press(&in, '\r', 1488);                /* the confirmation */
    press(&in, RSD_KEY_LEFT, 744);       /* onto Commit, deliberately */
    press(&in, '\r', 310);
    run_gauge(&in, 93);                   /* writing */
    run_gauge(&in, 93);                   /* verifying */
    hold(&in, 992);                       /* the root password screen */

    typewrite(&in, "correcthorse", 74);
    press(&in, RSD_KEY_DOWN, 310);
    typewrite(&in, "correcthorse", 74);
    hold(&in, 434);
    press(&in, '\r', 806);                /* network */

    press(&in, '\r', 682);                /* em0 -> DHCP or static */
    press(&in, '\r', 744);                /* DHCP -> DNS */
    press(&in, '\r', 806);                /* region */
    press(&in, RSD_KEY_DOWN, 198);
    press(&in, RSD_KEY_DOWN, 558);        /* Asia */
    press(&in, '\r', 806);                /* zone */
    press(&in, '\r', 868);                /* startup */

    press(&in, RSD_KEY_DOWN, 174);
    press(&in, RSD_KEY_DOWN, 174);
    press(&in, RSD_KEY_DOWN, 174);
    press(&in, RSD_KEY_DOWN, 310);
    press(&in, ' ', 558);                  /* start the desktop */
    press(&in, '\r', 1612);                /* hardening: read it */

    press(&in, '\r', 682);                /* add a user */
    typewrite(&in, "saud", 105);
    press(&in, RSD_KEY_DOWN, 248);
    typewrite(&in, "Saud Aljuaid", 81);
    press(&in, RSD_KEY_DOWN, 248);
    typewrite(&in, "hunter2", 93);
    press(&in, RSD_KEY_DOWN, 248);
    typewrite(&in, "hunter2", 93);
    hold(&in, 558);
    press(&in, '\r', 1736);                /* the final menu, all of it */

    press(&in, '\r', 1612);                /* complete */
    press(&in, '\r', 1860);                /* halted */

    fclose(timing);
    printf("%d frames in " DIR "\n", frames);
    return 0;
}
