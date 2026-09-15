/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/de/packages.h>

#include <trait/de/font.h>
#include <trait/de/theme.h>

#define PKG_TOOLBAR 30U
#define PKG_ROW 19U
#define PKG_BOX 11U
#define PKG_PAD 6U
#define PKG_DETAIL 74U

static struct trait_package packages[TRAIT_PACKAGES_MAX];
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

void trait_packages_reset(void)
{
    package_count = 0U;
    selected = 0U;
}

bool trait_packages_add(const char *name, const char *summary,
    const char *menu_name, bool installed)
{
    if (package_count >= TRAIT_PACKAGES_MAX || name == NULL) {
        return false;
    }
    copy(packages[package_count].name, name, TRAIT_PACKAGES_NAME_BYTES);
    copy(packages[package_count].summary, summary,
         TRAIT_PACKAGES_TEXT_BYTES);
    copy(packages[package_count].menu_name,
         menu_name == NULL ? "" : menu_name, TRAIT_PACKAGES_NAME_BYTES);
    packages[package_count].installed = installed;
    packages[package_count].mark = TRAIT_PACKAGE_NONE;
    ++package_count;
    return true;
}

uint32_t trait_packages_count(void)
{
    return package_count;
}

const struct trait_package *trait_packages_at(uint32_t index)
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
void trait_packages_mark(uint32_t index, enum trait_package_mark mark)
{
    if (index >= package_count) {
        return;
    }
    if (mark == TRAIT_PACKAGE_INSTALL && packages[index].installed) {
        return;
    }
    if (mark == TRAIT_PACKAGE_REMOVE && !packages[index].installed) {
        return;
    }
    packages[index].mark = mark;
}

uint32_t trait_packages_marked(void)
{
    uint32_t count = 0U;
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (packages[at].mark != TRAIT_PACKAGE_NONE) {
            ++count;
        }
    }
    return count;
}

uint32_t trait_packages_apply(void)
{
    uint32_t changed = 0U;
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (packages[at].mark == TRAIT_PACKAGE_NONE) {
            continue;
        }
        packages[at].installed =
            packages[at].mark == TRAIT_PACKAGE_INSTALL;
        packages[at].mark = TRAIT_PACKAGE_NONE;
        ++changed;
    }
    return changed;
}

bool trait_packages_installed(const char *name)
{
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (same(packages[at].name, name)) {
            return packages[at].installed;
        }
    }
    return false;
}

void trait_packages_select(uint32_t index)
{
    if (index < package_count) {
        selected = index;
    }
}

uint32_t trait_packages_selected(void)
{
    return selected;
}

static void box_at(struct trait_surface *surface, struct trait_rect clip,
    uint32_t x, uint32_t y, const struct trait_package *package)
{
    uint32_t edge;
    uint32_t ink = TRAIT_FG;
    struct trait_rect box = { x, y, PKG_BOX, PKG_BOX };

    /*
     * Trait OS's package state box: empty when the package is not installed,
     * filled when it is, and carrying the MARK when one is pending.  The
     * pending state is drawn differently from the settled one - a mark
     * that looked like the finished state would make Apply look like it
     * had already run.
     */
    trait_surface_fill(surface, clip, box,
        package->installed ? TRAIT_BASE_PRELIGHT : TRAIT_BASE);
    for (edge = 0U; edge < PKG_BOX; ++edge) {
        trait_surface_plot(surface, clip, x + edge, y, TRAIT_LINE);
        trait_surface_plot(surface, clip, x + edge, y + PKG_BOX - 1U,
                           TRAIT_LINE);
        trait_surface_plot(surface, clip, x, y + edge, TRAIT_LINE);
        trait_surface_plot(surface, clip, x + PKG_BOX - 1U, y + edge,
                           TRAIT_LINE);
    }
    if (package->mark == TRAIT_PACKAGE_INSTALL) {
        /* a plus: this is coming */
        for (edge = 2U; edge + 2U < PKG_BOX; ++edge) {
            trait_surface_plot(surface, clip, x + edge, y + PKG_BOX / 2U,
                               ink);
            trait_surface_plot(surface, clip, x + PKG_BOX / 2U, y + edge,
                               ink);
        }
        return;
    }
    if (package->mark == TRAIT_PACKAGE_REMOVE) {
        /* a minus: this is going */
        for (edge = 2U; edge + 2U < PKG_BOX; ++edge) {
            trait_surface_plot(surface, clip, x + edge, y + PKG_BOX / 2U,
                               ink);
        }
        return;
    }
    if (package->installed) {
        /* settled: a square, solid, saying it is simply here */
        for (edge = 3U; edge + 3U < PKG_BOX; ++edge) {
            uint32_t span;

            for (span = 3U; span + 3U < PKG_BOX; ++span) {
                trait_surface_plot(surface, clip, x + edge, y + span, ink);
            }
        }
    }
}

void trait_packages_draw(struct trait_surface *surface,
    const struct trait_window *window)
{
    struct trait_rect client;
    struct trait_rect list;
    struct trait_rect detail;
    uint32_t at;
    uint32_t edge;

    if (window == NULL || !trait_surface_valid(surface)) {
        return;
    }
    client = trait_window_client(window);
    trait_surface_fill(surface, client, client, TRAIT_BG);

    /* the toolbar: the Trait OS package actions, and Apply among them */
    {
        static const char *const TOOLS[3] = { "Reload", "Mark All",
                                              "Apply" };
        uint32_t pen = client.x + PKG_PAD;

        for (at = 0U; at < 3U; ++at) {
            uint32_t width = trait_font_width(TOOLS[at]) + 16U;
            struct trait_rect button;

            button.x = pen;
            button.y = client.y + 4U;
            button.width = width;
            button.height = 22U;
            trait_surface_fill(surface, client, button, TRAIT_BG);
            for (edge = 0U; edge < width; ++edge) {
                trait_surface_plot(surface, client, button.x + edge,
                                   button.y, TRAIT_LINE_LIGHT);
                trait_surface_plot(surface, client, button.x + edge,
                    button.y + button.height - 1U, TRAIT_LINE);
            }
            for (edge = 0U; edge < button.height; ++edge) {
                trait_surface_plot(surface, client, button.x,
                                   button.y + edge, TRAIT_LINE_LIGHT);
                trait_surface_plot(surface, client,
                    button.x + width - 1U, button.y + edge, TRAIT_LINE);
            }
            trait_font_draw(surface, client, button.x + 8U,
                            button.y + 15U, TOOLS[at], TRAIT_FG);
            pen += width + 5U;
        }
    }

    list.x = client.x;
    list.y = client.y + PKG_TOOLBAR;
    list.width = client.width;
    list.height = client.height > PKG_TOOLBAR + PKG_DETAIL ?
        client.height - PKG_TOOLBAR - PKG_DETAIL : 0U;
    trait_surface_fill(surface, client, list, TRAIT_BASE);

    for (at = 0U; at < package_count; ++at) {
        uint32_t top = list.y + at * PKG_ROW;
        bool lit = at == selected;

        if (top + PKG_ROW > list.y + list.height) {
            break;
        }
        if (lit) {
            struct trait_rect band = { list.x, top, list.width, PKG_ROW };

            trait_surface_fill(surface, list, band, TRAIT_SEL_BG);
        }
        box_at(surface, list, list.x + PKG_PAD, top + 4U, &packages[at]);
        trait_font_draw(surface, list, list.x + PKG_PAD + PKG_BOX + 8U,
            top + 14U, packages[at].name,
            lit ? TRAIT_SEL_FG : TRAIT_TEXT);
        trait_font_draw(surface, list, list.x + 150U, top + 14U,
            packages[at].summary, lit ? TRAIT_SEL_FG : TRAIT_TEXT);
    }

    /* the detail pane */
    detail.x = client.x;
    detail.y = list.y + list.height;
    detail.width = client.width;
    detail.height = PKG_DETAIL;
    trait_surface_fill(surface, client, detail, TRAIT_BG);
    for (edge = 0U; edge < detail.width; ++edge) {
        trait_surface_plot(surface, client, detail.x + edge, detail.y,
                           TRAIT_LINE);
    }
    if (selected < package_count) {
        trait_font_draw(surface, detail, detail.x + PKG_PAD,
                        detail.y + 18U, packages[selected].name,
                        TRAIT_FG);
        trait_font_draw(surface, detail, detail.x + PKG_PAD,
                        detail.y + 36U, packages[selected].summary,
                        TRAIT_TEXT);
        trait_font_draw(surface, detail, detail.x + PKG_PAD,
            detail.y + 54U,
            packages[selected].installed ? "Installed" : "Not installed",
            TRAIT_TEXT);
    }
}

/*
 * The self test asks what makes this a package manager rather than a list
 * of switches: does marking leave the package alone until Apply, does
 * Apply actually change it, and is a mark that would do nothing refused?
 */
bool trait_packages_self_test(void)
{
    trait_packages_reset();
    if (!trait_packages_add("leafpad", "A simple text editor", "Leafpad",
                            true) ||
            !trait_packages_add("galculator", "A calculator",
                                "Galculator", false)) {
        return false;
    }
    /* Marking does NOT install. */
    trait_packages_mark(1U, TRAIT_PACKAGE_INSTALL);
    if (trait_packages_installed("galculator")) {
        return false;
    }
    if (trait_packages_marked() != 1U) {
        return false;
    }
    /* A mark that would change nothing is refused, so the pending count
     * stays honest. */
    trait_packages_mark(0U, TRAIT_PACKAGE_INSTALL);
    if (trait_packages_marked() != 1U) {
        return false;
    }
    if (trait_packages_apply() != 1U) {
        return false;
    }
    if (!trait_packages_installed("galculator")) {
        return false;
    }
    if (trait_packages_marked() != 0U) {
        return false;
    }
    /* And removal goes the other way. */
    trait_packages_mark(0U, TRAIT_PACKAGE_REMOVE);
    if (trait_packages_apply() != 1U) {
        return false;
    }
    if (trait_packages_installed("leafpad")) {
        return false;
    }
    trait_packages_reset();
    return true;
}
