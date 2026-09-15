/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/de/packages.h>

#include <opengat/de/font.h>
#include <opengat/de/theme.h>

#define PKG_TOOLBAR 30U
#define PKG_ROW 19U
#define PKG_BOX 11U
#define PKG_PAD 6U
#define PKG_DETAIL 74U

static struct opengat_package packages[OPENGAT_PACKAGES_MAX];
static uint32_t package_count;
static uint32_t selected;

static void copy(char *out, const char *text, uint32_t capacity)
{
    uint32_t at = 0U;

    while (text != NULL && text[at] != '\0' && at + 1U < capacity) {
        out[at] = text[at];
        ++at;
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

void opengat_packages_reset(void)
{
    package_count = 0U;
    selected = 0U;
}

bool opengat_packages_add(const char *name, const char *summary,
    const char *menu_name, bool installed)
{
    if (package_count >= OPENGAT_PACKAGES_MAX || name == NULL) {
        return false;
    }
    copy(packages[package_count].name, name, OPENGAT_PACKAGES_NAME_BYTES);
    copy(packages[package_count].summary, summary,
         OPENGAT_PACKAGES_TEXT_BYTES);
    copy(packages[package_count].menu_name,
         menu_name == NULL ? "" : menu_name, OPENGAT_PACKAGES_NAME_BYTES);
    packages[package_count].installed = installed;
    packages[package_count].mark = OPENGAT_PACKAGE_NONE;
    ++package_count;
    return true;
}

uint32_t opengat_packages_count(void)
{
    return package_count;
}

const struct opengat_package *opengat_packages_at(uint32_t index)
{
    if (index >= package_count) {
        return NULL;
    }
    return &packages[index];
}

/*
 * A mark that would not change anything is REFUSED rather than stored:
 * marking an installed package for install is a mark that does nothing,
 * and a count of pending changes that includes it is a lie about how
 * much Apply will do.
 */
void opengat_packages_mark(uint32_t index, enum opengat_package_mark mark)
{
    if (index >= package_count) {
        return;
    }
    if (mark == OPENGAT_PACKAGE_INSTALL && packages[index].installed) {
        return;
    }
    if (mark == OPENGAT_PACKAGE_REMOVE && !packages[index].installed) {
        return;
    }
    packages[index].mark = mark;
}

uint32_t opengat_packages_marked(void)
{
    uint32_t count = 0U;
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (packages[at].mark != OPENGAT_PACKAGE_NONE) {
            ++count;
        }
    }
    return count;
}

uint32_t opengat_packages_apply(void)
{
    uint32_t changed = 0U;
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (packages[at].mark == OPENGAT_PACKAGE_NONE) {
            continue;
        }
        packages[at].installed =
            packages[at].mark == OPENGAT_PACKAGE_INSTALL;
        packages[at].mark = OPENGAT_PACKAGE_NONE;
        ++changed;
    }
    return changed;
}

bool opengat_packages_installed(const char *name)
{
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (same(packages[at].name, name)) {
            return packages[at].installed;
        }
    }
    return false;
}

void opengat_packages_select(uint32_t index)
{
    if (index < package_count) {
        selected = index;
    }
}

uint32_t opengat_packages_selected(void)
{
    return selected;
}

static void box_at(struct opengat_surface *surface, struct opengat_rect clip,
    uint32_t x, uint32_t y, const struct opengat_package *package)
{
    uint32_t edge;
    uint32_t ink = OPENGAT_FG;
    struct opengat_rect box = { x, y, PKG_BOX, PKG_BOX };

    /*
     * OpenGAT's package state box: empty when the package is not installed,
     * filled when it is, and carrying the MARK when one is pending.  The
     * pending state is drawn differently from the settled one - a mark
     * that looked like the finished state would make Apply look like it
     * had already run.
     */
    opengat_surface_fill(surface, clip, box,
        package->installed ? OPENGAT_BASE_PRELIGHT : OPENGAT_BASE);
    for (edge = 0U; edge < PKG_BOX; ++edge) {
        opengat_surface_plot(surface, clip, x + edge, y, OPENGAT_LINE);
        opengat_surface_plot(surface, clip, x + edge, y + PKG_BOX - 1U,
                           OPENGAT_LINE);
        opengat_surface_plot(surface, clip, x, y + edge, OPENGAT_LINE);
        opengat_surface_plot(surface, clip, x + PKG_BOX - 1U, y + edge,
                           OPENGAT_LINE);
    }
    if (package->mark == OPENGAT_PACKAGE_INSTALL) {
        /* a plus: this is coming */
        for (edge = 2U; edge + 2U < PKG_BOX; ++edge) {
            opengat_surface_plot(surface, clip, x + edge, y + PKG_BOX / 2U,
                               ink);
            opengat_surface_plot(surface, clip, x + PKG_BOX / 2U, y + edge,
                               ink);
        }
        return;
    }
    if (package->mark == OPENGAT_PACKAGE_REMOVE) {
        /* a minus: this is going */
        for (edge = 2U; edge + 2U < PKG_BOX; ++edge) {
            opengat_surface_plot(surface, clip, x + edge, y + PKG_BOX / 2U,
                               ink);
        }
        return;
    }
    if (package->installed) {
        /* settled: a square, solid, saying it is simply here */
        for (edge = 3U; edge + 3U < PKG_BOX; ++edge) {
            uint32_t span;

            for (span = 3U; span + 3U < PKG_BOX; ++span) {
                opengat_surface_plot(surface, clip, x + edge, y + span, ink);
            }
        }
    }
}

void opengat_packages_draw(struct opengat_surface *surface,
    const struct opengat_window *window)
{
    struct opengat_rect client;
    struct opengat_rect list;
    struct opengat_rect detail;
    uint32_t at;
    uint32_t edge;

    if (window == NULL || !opengat_surface_valid(surface)) {
        return;
    }
    client = opengat_window_client(window);
    opengat_surface_fill(surface, client, client, OPENGAT_BG);

    /* the toolbar: the OpenGAT package actions, and Apply among them */
    {
        static const char *const TOOLS[3] = { "Reload", "Mark All",
                                              "Apply" };
        uint32_t pen = client.x + PKG_PAD;

        for (at = 0U; at < 3U; ++at) {
            uint32_t width = opengat_font_width(TOOLS[at]) + 16U;
            struct opengat_rect button;

            button.x = pen;
            button.y = client.y + 4U;
            button.width = width;
            button.height = 22U;
            opengat_surface_fill(surface, client, button, OPENGAT_BG);
            for (edge = 0U; edge < width; ++edge) {
                opengat_surface_plot(surface, client, button.x + edge,
                                   button.y, OPENGAT_LINE_LIGHT);
                opengat_surface_plot(surface, client, button.x + edge,
                    button.y + button.height - 1U, OPENGAT_LINE);
            }
            for (edge = 0U; edge < button.height; ++edge) {
                opengat_surface_plot(surface, client, button.x,
                                   button.y + edge, OPENGAT_LINE_LIGHT);
                opengat_surface_plot(surface, client,
                    button.x + width - 1U, button.y + edge, OPENGAT_LINE);
            }
            opengat_font_draw(surface, client, button.x + 8U,
                            button.y + 15U, TOOLS[at], OPENGAT_FG);
            pen += width + 5U;
        }
    }

    list.x = client.x;
    list.y = client.y + PKG_TOOLBAR;
    list.width = client.width;
    list.height = client.height > PKG_TOOLBAR + PKG_DETAIL ?
        client.height - PKG_TOOLBAR - PKG_DETAIL : 0U;
    opengat_surface_fill(surface, client, list, OPENGAT_BASE);

    for (at = 0U; at < package_count; ++at) {
        uint32_t top = list.y + at * PKG_ROW;
        bool lit = at == selected;

        if (top + PKG_ROW > list.y + list.height) {
            break;
        }
        if (lit) {
            struct opengat_rect band = { list.x, top, list.width, PKG_ROW };

            opengat_surface_fill(surface, list, band, OPENGAT_SEL_BG);
        }
        box_at(surface, list, list.x + PKG_PAD, top + 4U, &packages[at]);
        opengat_font_draw(surface, list, list.x + PKG_PAD + PKG_BOX + 8U,
            top + 14U, packages[at].name,
            lit ? OPENGAT_SEL_FG : OPENGAT_TEXT);
        opengat_font_draw(surface, list, list.x + 150U, top + 14U,
            packages[at].summary, lit ? OPENGAT_SEL_FG : OPENGAT_TEXT);
    }

    /* the detail pane */
    detail.x = client.x;
    detail.y = list.y + list.height;
    detail.width = client.width;
    detail.height = PKG_DETAIL;
    opengat_surface_fill(surface, client, detail, OPENGAT_BG);
    for (edge = 0U; edge < detail.width; ++edge) {
        opengat_surface_plot(surface, client, detail.x + edge, detail.y,
                           OPENGAT_LINE);
    }
    if (selected < package_count) {
        opengat_font_draw(surface, detail, detail.x + PKG_PAD,
                        detail.y + 18U, packages[selected].name,
                        OPENGAT_FG);
        opengat_font_draw(surface, detail, detail.x + PKG_PAD,
                        detail.y + 36U, packages[selected].summary,
                        OPENGAT_TEXT);
        opengat_font_draw(surface, detail, detail.x + PKG_PAD,
            detail.y + 54U,
            packages[selected].installed ? "Installed" : "Not installed",
            OPENGAT_TEXT);
    }
}

/*
 * The self test asks what makes this a package manager rather than a list
 * of switches: does marking leave the package alone until Apply, does
 * Apply actually change it, and is a mark that would do nothing refused?
 */
bool opengat_packages_self_test(void)
{
    opengat_packages_reset();
    if (!opengat_packages_add("leafpad", "A simple text editor", "Leafpad",
                            true) ||
            !opengat_packages_add("galculator", "A calculator",
                                "Galculator", false)) {
        return false;
    }
    /* Marking does NOT install. */
    opengat_packages_mark(1U, OPENGAT_PACKAGE_INSTALL);
    if (opengat_packages_installed("galculator")) {
        return false;
    }
    if (opengat_packages_marked() != 1U) {
        return false;
    }
    /* A mark that would change nothing is refused, so the pending count
     * stays honest. */
    opengat_packages_mark(0U, OPENGAT_PACKAGE_INSTALL);
    if (opengat_packages_marked() != 1U) {
        return false;
    }
    if (opengat_packages_apply() != 1U) {
        return false;
    }
    if (!opengat_packages_installed("galculator")) {
        return false;
    }
    if (opengat_packages_marked() != 0U) {
        return false;
    }
    /* And removal goes the other way. */
    opengat_packages_mark(0U, OPENGAT_PACKAGE_REMOVE);
    if (opengat_packages_apply() != 1U) {
        return false;
    }
    if (opengat_packages_installed("leafpad")) {
        return false;
    }
    opengat_packages_reset();
    return true;
}
