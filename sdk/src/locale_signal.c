/* SPDX-License-Identifier: GPL-3.0-only */
#include <locale.h>
#include <openrfs/runtime.h>
#include <signal.h>
#include <unistd.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

char *setlocale(int category, const char *locale)
{
    static char c_locale[] = "C";
    if (category < LC_ALL || category > LC_TIME) { errno = EINVAL; return NULL; }
    if (locale == NULL || strcmp(locale, "C") == 0 || strcmp(locale, "") == 0) return c_locale;
    errno = ENOTSUP;
    return NULL;
}
struct lconv *localeconv(void)
{
    static char decimal[] = ".";
    static struct lconv result = {decimal};
    return &result;
}
sighandler_t signal(int signal_number, sighandler_t handler)
{
    if (signal_number <= 0 || signal_number > 64 ||
        signal_number == SIGKILL || signal_number == 19) {
        errno = EINVAL;
        return SIG_ERR;
    }
    if (handler != SIG_DFL && handler != SIG_IGN) {
        errno = ENOTSUP;
        return SIG_ERR;
    }
    const long result = openrfs_syscall2(OPENRFS_SYS_PROCESS_DISPOSITION,
        (uint64_t)(int64_t)signal_number, handler == SIG_IGN ? 1U : 0U);

    if (result < 0) {
        errno = (int)-result;
        return SIG_ERR;
    }
    return result == 0 ? SIG_DFL : SIG_IGN;
}
int raise(int signal_number)
{
    if (signal_number == SIGABRT) abort();
    return kill(getpid(), signal_number);
}
