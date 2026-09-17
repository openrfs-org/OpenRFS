/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/de/packages.h>

#include <openrfs/de/font.h>
#include <openrfs/de/theme.h>

#define PKG_TOOLBAR 30U
#define PKG_ROW 19U
#define PKG_BOX 11U
#define PKG_PAD 6U
#define PKG_DETAIL 74U

static struct openrfs_package packages[OPENRFS_PACKAGES_MAX];
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

void openrfs_packages_reset(void)
{
    package_count = 0U;
    selected = 0U;
}

bool openrfs_packages_add(const char *name, const char *summary,
    const char *menu_name, bool installed)
{
    if (package_count >= OPENRFS_PACKAGES_MAX || name == NULL) {
        return false;
    }
    copy(packages[package_count].name, name, OPENRFS_PACKAGES_NAME_BYTES);
    copy(packages[package_count].summary, summary,
         OPENRFS_PACKAGES_TEXT_BYTES);
    copy(packages[package_count].menu_name,
         menu_name == NULL ? "" : menu_name, OPENRFS_PACKAGES_NAME_BYTES);
    packages[package_count].installed = installed;
    packages[package_count].mark = OPENRFS_PACKAGE_NONE;
    ++package_count;
    return true;
}

uint32_t openrfs_packages_count(void)
{
    return package_count;
}

const struct openrfs_package *openrfs_packages_at(uint32_t index)
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
void openrfs_packages_mark(uint32_t index, enum openrfs_package_mark mark)
{
    if (index >= package_count) {
        return;
    }
    if (mark == OPENRFS_PACKAGE_INSTALL && packages[index].installed) {
        return;
    }
    if (mark == OPENRFS_PACKAGE_REMOVE && !packages[index].installed) {
        return;
    }
    packages[index].mark = mark;
}

uint32_t openrfs_packages_marked(void)
{
    uint32_t count = 0U;
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (packages[at].mark != OPENRFS_PACKAGE_NONE) {
            ++count;
        }
    }
    return count;
}

uint32_t openrfs_packages_apply(void)
{
    uint32_t changed = 0U;
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (packages[at].mark == OPENRFS_PACKAGE_NONE) {
            continue;
        }
        packages[at].installed =
            packages[at].mark == OPENRFS_PACKAGE_INSTALL;
        packages[at].mark = OPENRFS_PACKAGE_NONE;
        ++changed;
    }
    return changed;
}

bool openrfs_packages_installed(const char *name)
{
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (same(packages[at].name, name)) {
            return packages[at].installed;
        }
    }
    return false;
}

void openrfs_packages_select(uint32_t index)
{
    if (index < package_count) {
        selected = index;
    }
}

uint32_t openrfs_packages_selected(void)
{
    return selected;
}

static void box_at(struct openrfs_surface *surface, struct openrfs_rect clip,
    uint32_t x, uint32_t y, const struct openrfs_package *package)
{
    uint32_t edge;
    uint32_t ink = OPENRFS_FG;
    struct openrfs_rect box = { x, y, PKG_BOX, PKG_BOX };

    /*
     * OpenRFS's package state box: empty when the package is not installed,
     * filled when it is, and carrying the MARK when one is pending.  The
     * pending state is drawn differently from the settled one - a mark
     * that looked like the finished state would make Apply look like it
     * had already run.
     */
    openrfs_surface_fill(surface, clip, box,
        package->installed ? OPENRFS_BASE_PRELIGHT : OPENRFS_BASE);
    for (edge = 0U; edge < PKG_BOX; ++edge) {
        openrfs_surface_plot(surface, clip, x + edge, y, OPENRFS_LINE);
        openrfs_surface_plot(surface, clip, x + edge, y + PKG_BOX - 1U,
                           OPENRFS_LINE);
        openrfs_surface_plot(surface, clip, x, y + edge, OPENRFS_LINE);
        openrfs_surface_plot(surface, clip, x + PKG_BOX - 1U, y + edge,
                           OPENRFS_LINE);
    }
    if (package->mark == OPENRFS_PACKAGE_INSTALL) {
        /* a plus: this is coming */
        for (edge = 2U; edge + 2U < PKG_BOX; ++edge) {
            openrfs_surface_plot(surface, clip, x + edge, y + PKG_BOX / 2U,
                               ink);
            openrfs_surface_plot(surface, clip, x + PKG_BOX / 2U, y + edge,
                               ink);
        }
        return;
    }
    if (package->mark == OPENRFS_PACKAGE_REMOVE) {
        /* a minus: this is going */
        for (edge = 2U; edge + 2U < PKG_BOX; ++edge) {
            openrfs_surface_plot(surface, clip, x + edge, y + PKG_BOX / 2U,
                               ink);
        }
        return;
    }
    if (package->installed) {
        /* settled: a square, solid, saying it is simply here */
        for (edge = 3U; edge + 3U < PKG_BOX; ++edge) {
            uint32_t span;

            for (span = 3U; span + 3U < PKG_BOX; ++span) {
                openrfs_surface_plot(surface, clip, x + edge, y + span, ink);
            }
        }
    }
}

void openrfs_packages_draw(struct openrfs_surface *surface,
    const struct openrfs_window *window)
{
    struct openrfs_rect client;
    struct openrfs_rect list;
    struct openrfs_rect detail;
    uint32_t at;
    uint32_t edge;

    if (window == NULL || !openrfs_surface_valid(surface)) {
        return;
    }
    client = openrfs_window_client(window);
    openrfs_surface_fill(surface, client, client, OPENRFS_BG);

    /* the toolbar: the OpenRFS package actions, and Apply among them */
    {
        static const char *const TOOLS[3] = { "Reload", "Mark All",
                                              "Apply" };
        uint32_t pen = client.x + PKG_PAD;

        for (at = 0U; at < 3U; ++at) {
            uint32_t width = openrfs_font_width(TOOLS[at]) + 16U;
            struct openrfs_rect button;

            button.x = pen;
            button.y = client.y + 4U;
            button.width = width;
            button.height = 22U;
            openrfs_surface_fill(surface, client, button, OPENRFS_BG);
            for (edge = 0U; edge < width; ++edge) {
                openrfs_surface_plot(surface, client, button.x + edge,
                                   button.y, OPENRFS_LINE_LIGHT);
                openrfs_surface_plot(surface, client, button.x + edge,
                    button.y + button.height - 1U, OPENRFS_LINE);
            }
            for (edge = 0U; edge < button.height; ++edge) {
                openrfs_surface_plot(surface, client, button.x,
                                   button.y + edge, OPENRFS_LINE_LIGHT);
                openrfs_surface_plot(surface, client,
                    button.x + width - 1U, button.y + edge, OPENRFS_LINE);
            }
            openrfs_font_draw(surface, client, button.x + 8U,
                            button.y + 15U, TOOLS[at], OPENRFS_FG);
            pen += width + 5U;
        }
    }

    list.x = client.x;
    list.y = client.y + PKG_TOOLBAR;
    list.width = client.width;
    list.height = client.height > PKG_TOOLBAR + PKG_DETAIL ?
        client.height - PKG_TOOLBAR - PKG_DETAIL : 0U;
    openrfs_surface_fill(surface, client, list, OPENRFS_BASE);

    for (at = 0U; at < package_count; ++at) {
        uint32_t top = list.y + at * PKG_ROW;
        bool lit = at == selected;

        if (top + PKG_ROW > list.y + list.height) {
            break;
        }
        if (lit) {
            struct openrfs_rect band = { list.x, top, list.width, PKG_ROW };

            openrfs_surface_fill(surface, list, band, OPENRFS_SEL_BG);
        }
        box_at(surface, list, list.x + PKG_PAD, top + 4U, &packages[at]);
        openrfs_font_draw(surface, list, list.x + PKG_PAD + PKG_BOX + 8U,
            top + 14U, packages[at].name,
            lit ? OPENRFS_SEL_FG : OPENRFS_TEXT);
        openrfs_font_draw(surface, list, list.x + 150U, top + 14U,
            packages[at].summary, lit ? OPENRFS_SEL_FG : OPENRFS_TEXT);
    }

    /* the detail pane */
    detail.x = client.x;
    detail.y = list.y + list.height;
    detail.width = client.width;
    detail.height = PKG_DETAIL;
    openrfs_surface_fill(surface, client, detail, OPENRFS_BG);
    for (edge = 0U; edge < detail.width; ++edge) {
        openrfs_surface_plot(surface, client, detail.x + edge, detail.y,
                           OPENRFS_LINE);
    }
    if (selected < package_count) {
        openrfs_font_draw(surface, detail, detail.x + PKG_PAD,
                        detail.y + 18U, packages[selected].name,
                        OPENRFS_FG);
        openrfs_font_draw(surface, detail, detail.x + PKG_PAD,
                        detail.y + 36U, packages[selected].summary,
                        OPENRFS_TEXT);
        openrfs_font_draw(surface, detail, detail.x + PKG_PAD,
            detail.y + 54U,
            packages[selected].installed ? "Installed" : "Not installed",
            OPENRFS_TEXT);
    }
}

/*
 * The self test asks what makes this a package manager rather than a list
 * of switches: does marking leave the package alone until Apply, does
 * Apply actually change it, and is a mark that would do nothing refused?
 */
bool openrfs_packages_self_test(void)
{
    openrfs_packages_reset();
    if (!openrfs_packages_add("leafpad", "A simple text editor", "Leafpad",
                            true) ||
            !openrfs_packages_add("galculator", "A calculator",
                                "Galculator", false)) {
        return false;
    }
    /* Marking does NOT install. */
    openrfs_packages_mark(1U, OPENRFS_PACKAGE_INSTALL);
    if (openrfs_packages_installed("galculator")) {
        return false;
    }
    if (openrfs_packages_marked() != 1U) {
        return false;
    }
    /* A mark that would change nothing is refused, so the pending count
     * stays honest. */
    openrfs_packages_mark(0U, OPENRFS_PACKAGE_INSTALL);
    if (openrfs_packages_marked() != 1U) {
        return false;
    }
    if (openrfs_packages_apply() != 1U) {
        return false;
    }
    if (!openrfs_packages_installed("galculator")) {
        return false;
    }
    if (openrfs_packages_marked() != 0U) {
        return false;
    }
    /* And removal goes the other way. */
    openrfs_packages_mark(0U, OPENRFS_PACKAGE_REMOVE);
    if (openrfs_packages_apply() != 1U) {
        return false;
    }
    if (openrfs_packages_installed("leafpad")) {
        return false;
    }
    openrfs_packages_reset();
    return true;
}
