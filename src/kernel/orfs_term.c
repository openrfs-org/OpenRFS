/* SPDX-License-Identifier: GPL-3.0-only */
#include <orfs/term.h>

uint32_t orfs_strlen(const char *s)
{
    uint32_t n = 0U;

    while (s != NULL && s[n] != '\0') {
        ++n;
    }
    return n;
}

bool orfs_streq(const char *a, const char *b)
{
    uint32_t at = 0U;

    if (a == NULL || b == NULL) {
        return false;
    }
    while (a[at] != '\0' && b[at] != '\0') {
        if (a[at] != b[at]) {
            return false;
        }
        ++at;
    }
    return a[at] == b[at];
}

bool orfs_strneq(const char *a, const char *b, uint32_t n)
{
    uint32_t at;

    if (a == NULL || b == NULL) {
        return false;
    }
    for (at = 0U; at < n; ++at) {
        if (a[at] != b[at]) {
            return false;
        }
        if (a[at] == '\0') {
            return true;
        }
    }
    return true;
}

uint32_t orfs_strcopy(char *out, uint32_t capacity, const char *s)
{
    uint32_t at = 0U;

    if (out == NULL || capacity == 0U) {
        return 0U;
    }
    while (s != NULL && s[at] != '\0' && at + 1U < capacity) {
        out[at] = s[at];
        ++at;
    }
    out[at] = '\0';
    return at;
}

uint32_t orfs_strcat(char *out, uint32_t capacity, const char *s)
{
    uint32_t at = 0U;

    if (out == NULL || capacity == 0U) {
        return 0U;
    }
    while (at + 1U < capacity && out[at] != '\0') {
        ++at;
    }
    while (s != NULL && *s != '\0' && at + 1U < capacity) {
        out[at++] = *s++;
    }
    out[at] = '\0';
    return at;
}

uint32_t orfs_atou(const char *s)
{
    uint32_t value = 0U;

    while (s != NULL && *s >= '0' && *s <= '9') {
        /* Stop rather than wrap. A field that is all nines is a typo,
         * not a request for four billion mebibytes of swap. */
        if (value > 0xFFFFFFFFU / 10U) {
            return 0xFFFFFFFFU;
        }
        value = value * 10U + (uint32_t)(*s - '0');
        ++s;
    }
    return value;
}

uint32_t orfs_u32(char *out, uint32_t capacity, uint32_t value)
{
    char digits[10];
    uint32_t n = 0U;
    uint32_t at = 0U;

    if (out == NULL || capacity == 0U) {
        return 0U;
    }
    if (value == 0U) {
        digits[n++] = '0';
    }
    while (value != 0U) {
        digits[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n != 0U && at + 1U < capacity) {
        out[at++] = digits[--n];
    }
    out[at] = '\0';
    return at;
}

uint32_t orfs_u32_pad(char *out, uint32_t capacity, uint32_t value,
                      uint32_t width, char pad)
{
    char body[12];
    uint32_t len = orfs_u32(body, sizeof(body), value);
    uint32_t at = 0U;
    uint32_t gap = width > len ? width - len : 0U;

    if (out == NULL || capacity == 0U) {
        return 0U;
    }
    while (gap-- != 0U && at + 1U < capacity) {
        out[at++] = pad;
    }
    for (uint32_t i = 0U; i < len && at + 1U < capacity; ++i) {
        out[at++] = body[i];
    }
    out[at] = '\0';
    return at;
}

void orfs_term_reset(struct orfs_term *t)
{
    uint32_t r;
    uint32_t c;

    if (t == NULL) {
        return;
    }
    for (r = 0U; r < ORFS_ROWS; ++r) {
        for (c = 0U; c < ORFS_COLS; ++c) {
            t->cell[r][c] = ' ';
            t->attr[r][c] = ORFS_NORMAL;
        }
    }
    t->pen = ORFS_NORMAL;
    t->col = 0U;
    t->row = 0U;
    t->cursor = true;
}

void orfs_term_clear(struct orfs_term *t)
{
    orfs_term_reset(t);
}

/*
 * Scrolling moves rows up by one and blanks the last. A console that
 * grew a scrollback would be a different program; this is the screen,
 * and what leaves the top is gone.
 */
static void scroll(struct orfs_term *t)
{
    uint32_t r;
    uint32_t c;

    for (r = 1U; r < ORFS_ROWS; ++r) {
        for (c = 0U; c < ORFS_COLS; ++c) {
            t->cell[r - 1U][c] = t->cell[r][c];
            t->attr[r - 1U][c] = t->attr[r][c];
        }
    }
    for (c = 0U; c < ORFS_COLS; ++c) {
        t->cell[ORFS_ROWS - 1U][c] = ' ';
        t->attr[ORFS_ROWS - 1U][c] = t->pen;
    }
    t->row = ORFS_ROWS - 1U;
}

void orfs_term_newline(struct orfs_term *t)
{
    if (t == NULL) {
        return;
    }
    t->col = 0U;
    if (t->row + 1U >= ORFS_ROWS) {
        scroll(t);
    } else {
        ++t->row;
    }
}

/*
 * The grid holds one byte per cell. 0x20-0x7E is ASCII; 0x80 and up are
 * the frame and block glyphs, which are not characters anyone types but
 * are exactly as much a cell as a letter is. 0x7F is the hole between
 * the two and is dropped like any other unprintable byte.
 */
static bool printable(char c)
{
    uint8_t u = (uint8_t)c;

    return (u >= 0x20U && u <= 0x7EU) || (u >= 0x80U && u <= 0x8EU);
}

static void put(struct orfs_term *t, char c, uint8_t attr)
{
    if (t == NULL) {
        return;
    }
    if (c == '\n') {
        orfs_term_newline(t);
        return;
    }
    if (c == '\t') {
        do {
            put(t, ' ', attr);
        } while ((t->col % 8U) != 0U);
        return;
    }
    if (!printable(c)) {
        return;
    }
    if (t->col >= ORFS_COLS) {
        orfs_term_newline(t);
    }
    t->cell[t->row][t->col] = c;
    t->attr[t->row][t->col] = attr;
    ++t->col;
}

void orfs_term_putc(struct orfs_term *t, char c)
{
    put(t, c, t == NULL ? ORFS_NORMAL : t->pen);
}

void orfs_term_puts(struct orfs_term *t, const char *s)
{
    uint32_t at = 0U;

    while (t != NULL && s != NULL && s[at] != '\0') {
        put(t, s[at], t->pen);
        ++at;
    }
}

/*
 * Bright, not a second attribute. Bit 3 of the foreground is the bit
 * that makes text bright in a VGA attribute byte, so this sets it
 * rather than keeping a second flag that means the same thing.
 */
void orfs_term_puts_bold(struct orfs_term *t, const char *s)
{
    uint32_t at = 0U;
    uint8_t bright;

    if (t == NULL) {
        return;
    }
    bright = ORFS_ATTR(ORFS_ATTR_FG(t->pen) | 0x08U, ORFS_ATTR_BG(t->pen));
    while (s != NULL && s[at] != '\0') {
        put(t, s[at], bright);
        ++at;
    }
}

void orfs_term_pen(struct orfs_term *t, uint8_t attr)
{
    if (t != NULL) {
        t->pen = attr;
    }
}

void orfs_term_put_at(struct orfs_term *t, uint32_t row, uint32_t col,
                      char c, uint8_t attr)
{
    if (t == NULL || row >= ORFS_ROWS || col >= ORFS_COLS || !printable(c)) {
        return;
    }
    t->cell[row][col] = c;
    t->attr[row][col] = attr;
}

void orfs_term_puts_at(struct orfs_term *t, uint32_t row, uint32_t col,
                       const char *s, uint8_t attr)
{
    uint32_t at = 0U;

    while (t != NULL && s != NULL && s[at] != '\0') {
        orfs_term_put_at(t, row, col + at, s[at], attr);
        ++at;
    }
}

void orfs_term_repeat_at(struct orfs_term *t, uint32_t row, uint32_t col,
                         char c, uint32_t count, uint8_t attr)
{
    for (uint32_t i = 0U; i < count; ++i) {
        orfs_term_put_at(t, row, col + i, c, attr);
    }
}

void orfs_term_box_fill(struct orfs_term *t, uint32_t row, uint32_t col,
                        uint32_t rows, uint32_t cols, char c, uint8_t attr)
{
    for (uint32_t r = 0U; r < rows; ++r) {
        orfs_term_repeat_at(t, row + r, col, c, cols, attr);
    }
}

void orfs_term_fill(struct orfs_term *t, char c, uint8_t attr)
{
    if (t == NULL) {
        return;
    }
    orfs_term_box_fill(t, 0U, 0U, ORFS_ROWS, ORFS_COLS, c, attr);
    t->col = 0U;
    t->row = 0U;
}

void orfs_term_erase(struct orfs_term *t, uint32_t count)
{
    if (t == NULL) {
        return;
    }
    while (count-- != 0U && t->col != 0U) {
        --t->col;
        t->cell[t->row][t->col] = ' ';
        t->attr[t->row][t->col] = t->pen;
    }
}

char orfs_term_at(const struct orfs_term *t, uint32_t row, uint32_t col)
{
    if (t == NULL || row >= ORFS_ROWS || col >= ORFS_COLS) {
        return '\0';
    }
    return t->cell[row][col];
}

uint8_t orfs_term_attr_at(const struct orfs_term *t, uint32_t row,
                          uint32_t col)
{
    if (t == NULL || row >= ORFS_ROWS || col >= ORFS_COLS) {
        return ORFS_NORMAL;
    }
    return t->attr[row][col];
}
