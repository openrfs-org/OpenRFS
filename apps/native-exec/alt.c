/* SPDX-License-Identifier: GPL-3.0-only */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv, char **environment)
{
    char first_byte = 0;
    int kept;
    int closed;

    if (argc == 2 && argv != NULL && environment != NULL &&
        environment[0] != NULL && environment[1] == NULL &&
        strcmp(environment[0], "PIPE_STAGE=1") == 0) {
        if (strcmp(argv[1], "producer") == 0) {
            if (puts("PIPELINE-PAYLOAD") == EOF || fflush(stdout) != 0)
                return 22;
            return 0;
        }
        if (strcmp(argv[1], "consumer") == 0) {
            char line[32];

            if (fgets(line, sizeof(line), stdin) == NULL ||
                strcmp(line, "PIPELINE-PAYLOAD\n") != 0) return 23;
            if (fgetc(stdin) != EOF || !feof(stdin)) return 23;
            puts("OPENRFS PROCESS exec pipeline consumer observed PASS");
            return 0;
        }
    }

    if (argc != 5 || argv == NULL ||
        strcmp(argv[0], "EXECALT.APP") != 0 ||
        environment == NULL || environment[0] == NULL ||
        environment[1] == NULL || environment[2] != NULL ||
        strcmp(environment[0], "EXEC_STAGE=second") != 0 ||
        strcmp(environment[1], "VALUE=") != 0 ||
        getpid() != atoi(argv[1]) || getppid() != atoi(argv[2]))
        return 20;
    kept = atoi(argv[3]);
    closed = atoi(argv[4]);
    if (kept < 3 || closed < 3 || fcntl(kept, F_GETFD) != 0 ||
        read(kept, &first_byte, 1U) != 1 || first_byte != 'O' ||
        fcntl(closed, F_GETFD) != -1 || errno != EBADF ||
        close(kept) != 0)
        return 21;
    puts("OPENRFS PROCESS distinct replacement image observed PASS");
    return 0;
}
