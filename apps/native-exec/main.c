/* SPDX-License-Identifier: GPL-3.0-only */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv, char **environment)
{
    int status;
    const int parent_pid = getpid();
    int child;

    if (argc != 1 || argv == NULL ||
        strcmp(argv[0], "EXECMAIN.APP") != 0 ||
        environment == NULL || environment[0] == NULL)
        return 10;
    child = fork();
    if (child < 0) return 11;
    if (child == 0) {
        char pid_text[16];
        char parent_text[16];
        char kept_text[16];
        char closed_text[16];
        char *arguments[] = {"EXECALT.APP", pid_text, parent_text,
            kept_text, closed_text, NULL};
        char *replacement_environment[] = {"EXEC_STAGE=second", "VALUE=", NULL};
        const int kept = open("System:RESOURCE.TXT", O_RDONLY);
        const int closed = open("System:RESOURCE.TXT", O_RDONLY | O_CLOEXEC);

        if (kept < 3 || closed < 3 ||
            snprintf(pid_text, sizeof(pid_text), "%d", getpid()) <= 0 ||
            snprintf(parent_text, sizeof(parent_text), "%d", parent_pid) <= 0 ||
            snprintf(kept_text, sizeof(kept_text), "%d", kept) <= 0 ||
            snprintf(closed_text, sizeof(closed_text), "%d", closed) <= 0)
            _Exit(12);
        (void)execve("EXECALT.MAN", arguments, replacement_environment);
        printf("OPENRFS PROCESS distinct exec error=%d\n", errno);
        _Exit(13);
    }
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        printf("OPENRFS PROCESS distinct exec child status=%d\n", status);
        return 14;
    }
    puts("OPENRFS PROCESS distinct signed image exec and wait PASS");
    return 0;
}
