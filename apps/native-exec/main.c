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
    for (int wrapper = 0; wrapper < 2; ++wrapper) {
        child = fork();
        if (child < 0) return 24;
        if (child == 0) {
            if (wrapper == 0) {
                char *arguments[] = {"EXECALT.APP", "inherited-v", NULL};

                (void)execv("EXECALT.MAN", arguments);
            } else {
                (void)execl("EXECALT.MAN", "EXECALT.APP", "inherited-l",
                    (char *)NULL);
            }
            _Exit(25);
        }
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) return 26;
    }
    puts("OPENRFS PROCESS execv execl inherited environment PASS");
    if (fflush(NULL) != 0) return 15;
    int ends[2];

    if (pipe(ends) != 0) return 16;
    const int producer = fork();

    if (producer < 0) return 17;
    if (producer == 0) {
        char *replacement_environment[] = {"PIPE_STAGE=1", NULL};

        if (close(ends[0]) != 0 ||
            dup2(ends[1], STDOUT_FILENO) != STDOUT_FILENO ||
            close(ends[1]) != 0) _Exit(18);
        (void)execle("EXECALT.MAN", "EXECALT.APP", "producer",
            (char *)NULL, replacement_environment);
        _Exit(19);
    }
    const int consumer = fork();

    if (consumer < 0) return 20;
    if (consumer == 0) {
        char *arguments[] = {"EXECALT.APP", "consumer", NULL};
        char *replacement_environment[] = {"PIPE_STAGE=1", NULL};

        if (close(ends[1]) != 0 ||
            dup2(ends[0], STDIN_FILENO) != STDIN_FILENO ||
            close(ends[0]) != 0) _Exit(21);
        (void)execve("EXECALT.MAN", arguments, replacement_environment);
        _Exit(22);
    }
    if (close(ends[0]) != 0 || close(ends[1]) != 0 ||
        waitpid(producer, &status, 0) != producer ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        waitpid(consumer, &status, 0) != consumer ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 23;
    puts("OPENRFS PROCESS fork exec pipeline and wait PASS");
    return 0;
}
