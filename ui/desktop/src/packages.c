/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd_desktop/packages.h>

#include <rsd_desktop/font.h>
#include <rsd_desktop/oxygen.h>
#include <rsd_desktop/theme.h>

#define PKG_TOOLBAR 30U
#define PKG_ROW 19U
#define PKG_BOX 11U
#define PKG_PAD 6U
#define PKG_DETAIL 74U

static struct rsd_package packages[RSD_PACKAGES_MAX];
static uint32_t package_count;
static uint32_t selected;
static bool (*live_load)(void);
static bool live_ready;
static uint32_t page;

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

void rsd_packages_reset(void)
{
    package_count = 0U;
    selected = 0U;
    live_load = NULL;
    live_ready = false;
    page = 0U;
}

void rsd_packages_use_live_source(bool (*load)(void))
{
    live_load = load;
    package_count = 0U;
    selected = 0U;
    live_ready = false;
    page = 0U;
}

bool rsd_packages_refresh(void)
{
    if (live_load == NULL) {
        return false;
    }
    package_count = 0U;
    selected = 0U;
    page = 0U;
    live_ready = live_load();
    if (!live_ready) {
        package_count = 0U;
    }
    return live_ready;
}

bool rsd_packages_live(void)
{
    return live_load != NULL;
}

bool rsd_packages_add(const char *name, const char *summary,
    const char *menu_name, bool installed)
{
    if (package_count >= RSD_PACKAGES_MAX || name == NULL) {
        return false;
    }
    copy(packages[package_count].name, name, RSD_PACKAGES_NAME_BYTES);
    copy(packages[package_count].summary, summary,
         RSD_PACKAGES_TEXT_BYTES);
    copy(packages[package_count].menu_name,
         menu_name == NULL ? "" : menu_name, RSD_PACKAGES_NAME_BYTES);
    packages[package_count].installed = installed;
    packages[package_count].mark = RSD_PACKAGE_NONE;
    ++package_count;
    return true;
}

/* How many are ON the machine, which is not how many are listed: the
 * list is the repository and `installed` is the answer to the question
 * a fetch asks. */
uint32_t rsd_packages_installed_count(void)
{
    uint32_t found = 0U;
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (packages[at].installed) {
            ++found;
        }
    }
    return found;
}

uint32_t rsd_packages_count(void)
{
    return package_count;
}

const struct rsd_package *rsd_packages_at(uint32_t index)
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
void rsd_packages_mark(uint32_t index, enum rsd_package_mark mark)
{
    if (live_load != NULL || index >= package_count) {
        return;
    }
    if (mark == RSD_PACKAGE_INSTALL && packages[index].installed) {
        return;
    }
    if (mark == RSD_PACKAGE_REMOVE && !packages[index].installed) {
        return;
    }
    packages[index].mark = mark;
}

uint32_t rsd_packages_marked(void)
{
    uint32_t count = 0U;
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (packages[at].mark != RSD_PACKAGE_NONE) {
            ++count;
        }
    }
    return count;
}

uint32_t rsd_packages_apply(void)
{
    uint32_t changed = 0U;
    uint32_t at;

    if (live_load != NULL) {
        return 0U;
    }
    for (at = 0U; at < package_count; ++at) {
        if (packages[at].mark == RSD_PACKAGE_NONE) {
            continue;
        }
        packages[at].installed =
            packages[at].mark == RSD_PACKAGE_INSTALL;
        packages[at].mark = RSD_PACKAGE_NONE;
        ++changed;
    }
    return changed;
}

bool rsd_packages_installed(const char *name)
{
    uint32_t at;

    for (at = 0U; at < package_count; ++at) {
        if (same(packages[at].name, name)) {
            return packages[at].installed;
        }
    }
    return false;
}

void rsd_packages_select(uint32_t index)
{
    if (index < package_count) {
        selected = index;
    }
}

uint32_t rsd_packages_selected(void)
{
    return selected;
}

static uint32_t page_rows(const struct rsd_window *window)
{
    struct rsd_rect client = rsd_window_client(window);
    uint32_t list_height = client.height > PKG_TOOLBAR + PKG_DETAIL ?
        client.height - PKG_TOOLBAR - PKG_DETAIL : 0U;
    uint32_t rows = list_height / PKG_ROW;

    return rows == 0U ? 1U : rows;
}

uint32_t rsd_packages_first_visible(const struct rsd_window *window)
{
    return window == NULL ? 0U : page * page_rows(window);
}

bool rsd_packages_turn_page(const struct rsd_window *window, bool next)
{
    uint32_t rows;

    if (window == NULL) {
        return false;
    }
    rows = page_rows(window);
    if (next) {
        if ((page + 1U) * rows >= package_count) {
            return false;
        }
        ++page;
    } else {
        if (page == 0U) {
            return false;
        }
        --page;
    }
    selected = page * rows;
    return true;
}

bool rsd_packages_page_bounds(const struct rsd_window *window, bool next,
    struct rsd_rect *out)
{
    struct rsd_rect client;

    if (window == NULL || out == NULL) {
        return false;
    }
    client = rsd_window_client(window);
    if (client.width < 250U) {
        return false;
    }
    *out = (struct rsd_rect){ client.x + client.width - (next ? 58U : 112U),
        client.y + 4U, 50U, 22U };
    return true;
}

static void box_at(struct rsd_surface *surface, struct rsd_rect clip,
    uint32_t x, uint32_t y, const struct rsd_package *package)
{
    uint32_t edge;
    uint32_t ink = RSD_FG;
    struct rsd_rect box = { x, y, PKG_BOX, PKG_BOX };

    /*
     * Synaptic's state box: empty when the package is not installed,
     * filled when it is, and carrying the MARK when one is pending.  The
     * pending state is drawn differently from the settled one - a mark
     * that looked like the finished state would make Apply look like it
     * had already run.
     */
    rsd_surface_fill(surface, clip, box,
        package->installed ? RSD_BASE_PRELIGHT : RSD_BASE);
    for (edge = 0U; edge < PKG_BOX; ++edge) {
        rsd_surface_plot(surface, clip, x + edge, y, RSD_LINE);
        rsd_surface_plot(surface, clip, x + edge, y + PKG_BOX - 1U,
                           RSD_LINE);
        rsd_surface_plot(surface, clip, x, y + edge, RSD_LINE);
        rsd_surface_plot(surface, clip, x + PKG_BOX - 1U, y + edge,
                           RSD_LINE);
    }
    if (package->mark == RSD_PACKAGE_INSTALL) {
        /* a plus: this is coming */
        for (edge = 2U; edge + 2U < PKG_BOX; ++edge) {
            rsd_surface_plot(surface, clip, x + edge, y + PKG_BOX / 2U,
                               ink);
            rsd_surface_plot(surface, clip, x + PKG_BOX / 2U, y + edge,
                               ink);
        }
        return;
    }
    if (package->mark == RSD_PACKAGE_REMOVE) {
        /* a minus: this is going */
        for (edge = 2U; edge + 2U < PKG_BOX; ++edge) {
            rsd_surface_plot(surface, clip, x + edge, y + PKG_BOX / 2U,
                               ink);
        }
        return;
    }
    if (package->installed) {
        /* settled: a square, solid, saying it is simply here */
        for (edge = 3U; edge + 3U < PKG_BOX; ++edge) {
            uint32_t span;

            for (span = 3U; span + 3U < PKG_BOX; ++span) {
                rsd_surface_plot(surface, clip, x + edge, y + span, ink);
            }
        }
    }
}

bool rsd_packages_apply_bounds(const struct rsd_window *window,
    struct rsd_rect *out)
{
    struct rsd_rect client;

    if (window == NULL || out == NULL) {
        return false;
    }
    client = rsd_window_client(window);
    out->x = client.x + PKG_PAD;
    out->y = client.y + 4U;
    out->width = rsd_font_width("Apply") + 16U;
    out->height = 22U;
    return out->width < client.width;
}

void rsd_packages_draw(struct rsd_surface *surface,
    const struct rsd_window *window)
{
    struct rsd_rect client;
    struct rsd_rect list;
    struct rsd_rect detail;
    uint32_t at;
    uint32_t edge;

    if (window == NULL || !rsd_surface_valid(surface)) {
        return;
    }
    client = rsd_window_client(window);
    rsd_surface_fill(surface, client, client, RSD_BG);

    /* Apply belongs to the standalone preview model only. */
    {
        struct rsd_rect button;

        if (live_load == NULL && rsd_packages_apply_bounds(window, &button)) {
            rsd_oxy_button(surface, client, button, RSD_BG, false);
            rsd_font_draw(surface, client, button.x + 8U,
                button.y + (button.height + rsd_font_ascent()) / 2U - 1U,
                "Apply", RSD_FG);
        }
    }

    list.x = client.x;
    list.y = client.y + PKG_TOOLBAR;
    list.width = client.width;
    list.height = client.height > PKG_TOOLBAR + PKG_DETAIL ?
        client.height - PKG_TOOLBAR - PKG_DETAIL : 0U;
    rsd_surface_fill(surface, client, list, RSD_BASE);

    if (live_load != NULL) {
        rsd_font_draw(surface, client, client.x + PKG_PAD,
            client.y + 19U, "Installed packages - read only (Ctrl+R reload)",
            RSD_TEXT);
        if (!live_ready) {
            rsd_font_draw(surface, list, list.x + PKG_PAD,
                list.y + 20U, "Package database unavailable", RSD_TEXT);
        }
    }
    if (package_count > page_rows(window)) {
        struct rsd_rect previous;
        struct rsd_rect following;

        if (rsd_packages_page_bounds(window, false, &previous) &&
                rsd_packages_page_bounds(window, true, &following)) {
            rsd_oxy_button(surface, client, previous, RSD_BG, false);
            rsd_oxy_button(surface, client, following, RSD_BG, false);
            rsd_font_draw(surface, client, previous.x + 7U,
                previous.y + 17U, "Prev", RSD_FG);
            rsd_font_draw(surface, client, following.x + 7U,
                following.y + 17U, "Next", RSD_FG);
        }
    }

    for (at = rsd_packages_first_visible(window); at < package_count; ++at) {
        uint32_t top = list.y +
            (at - rsd_packages_first_visible(window)) * PKG_ROW;
        bool lit = at == selected;

        if (top + PKG_ROW > list.y + list.height) {
            break;
        }
        if (lit) {
            struct rsd_rect band = { list.x, top, list.width, PKG_ROW };

            rsd_surface_fill(surface, list, band, RSD_SEL_BG);
        }
        box_at(surface, list, list.x + PKG_PAD, top + 4U, &packages[at]);
        rsd_font_draw(surface, list, list.x + PKG_PAD + PKG_BOX + 8U,
            top + 14U, packages[at].name,
            lit ? RSD_SEL_FG : RSD_TEXT);
        rsd_font_draw(surface, list, list.x + 150U, top + 14U,
            packages[at].summary, lit ? RSD_SEL_FG : RSD_TEXT);
    }

    /* the detail pane */
    detail.x = client.x;
    detail.y = list.y + list.height;
    detail.width = client.width;
    detail.height = PKG_DETAIL;
    rsd_surface_fill(surface, client, detail, RSD_BG);
    for (edge = 0U; edge < detail.width; ++edge) {
        rsd_surface_plot(surface, client, detail.x + edge, detail.y,
                           RSD_LINE);
    }
    if (selected < package_count) {
        rsd_font_draw(surface, detail, detail.x + PKG_PAD,
                        detail.y + 18U, packages[selected].name,
                        RSD_FG);
        rsd_font_draw(surface, detail, detail.x + PKG_PAD,
                        detail.y + 36U, packages[selected].summary,
                        RSD_TEXT);
        rsd_font_draw(surface, detail, detail.x + PKG_PAD,
            detail.y + 54U,
            packages[selected].installed ? "Installed" : "Not installed",
            RSD_TEXT);
    }
}

/*
 * The self test asks what makes this a package manager rather than a list
 * of switches: does marking leave the package alone until Apply, does
 * Apply actually change it, and is a mark that would do nothing refused?
 */
bool rsd_packages_self_test(void)
{
    rsd_packages_reset();
    if (!rsd_packages_add("leafpad", "A simple text editor", "Leafpad",
                            true) ||
            !rsd_packages_add("galculator", "A calculator",
                                "Galculator", false)) {
        return false;
    }
    /* Marking does NOT install. */
    rsd_packages_mark(1U, RSD_PACKAGE_INSTALL);
    if (rsd_packages_installed("galculator")) {
        return false;
    }
    if (rsd_packages_marked() != 1U) {
        return false;
    }
    /* A mark that would change nothing is refused, so the pending count
     * stays honest. */
    rsd_packages_mark(0U, RSD_PACKAGE_INSTALL);
    if (rsd_packages_marked() != 1U) {
        return false;
    }
    if (rsd_packages_apply() != 1U) {
        return false;
    }
    if (!rsd_packages_installed("galculator")) {
        return false;
    }
    if (rsd_packages_marked() != 0U) {
        return false;
    }
    /* And removal goes the other way. */
    rsd_packages_mark(0U, RSD_PACKAGE_REMOVE);
    if (rsd_packages_apply() != 1U) {
        return false;
    }
    if (rsd_packages_installed("leafpad")) {
        return false;
    }
    rsd_packages_reset();
    return true;
}
