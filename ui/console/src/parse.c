/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/shell.h>

/*
 * Quoting, because a filename with a space in it is not two filenames.
 * FAT32 8.3 names cannot contain a space, so this will not matter often
 * - but a parser that is only correct for the easy input is a parser
 * that will be wrong the first time it matters.
 */
void rsd_parse(const char *line, struct rsd_argv *out)
{
    uint32_t at = 0U;
    uint32_t len = 0U;
    char quote = '\0';
    bool in_word = false;

    if (out == NULL) {
        return;
    }
    out->count = 0U;
    out->arg[0][0] = '\0';
    if (line == NULL) {
        return;
    }

    while (line[at] != '\0') {
        char c = line[at++];

        if (quote != '\0') {
            if (c == quote) {
                quote = '\0';
                continue;
            }
        } else if (c == '\'' || c == '"') {
            quote = c;
            in_word = true;          /* "" is an empty argument, not none */
            continue;
        } else if (c == '\\' && line[at] != '\0') {
            c = line[at++];
        } else if (c == ' ' || c == '\t') {
            if (in_word) {
                out->arg[out->count][len] = '\0';
                ++out->count;
                len = 0U;
                in_word = false;
                if (out->count >= RSD_ARGS) {
                    return;
                }
            }
            continue;
        }

        in_word = true;
        if (len + 1U < RSD_ARG_MAX) {
            out->arg[out->count][len++] = c;
        }
    }

    if (in_word) {
        out->arg[out->count][len] = '\0';
        ++out->count;
    }
}
