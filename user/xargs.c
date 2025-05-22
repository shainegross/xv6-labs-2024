// user/xargs.c - xv6-compatible version (cleaned)

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAXARGS 32
#define MAXLINE 512

int main(int argc, char *argv[]) {
    char buf[MAXLINE];
    char *newargv[MAXARGS];
    int n;

    // Copy initial arguments
    int base = 0;
    for (int i = 1; i < argc; i++) {
        newargv[base++] = argv[i];
    }

    // Read from stdin
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        int i = 0;
        while (i < n) {
            // Skip leading spaces
            while (i < n && buf[i] == ' ') i++;

            // End of line?
            if (i >= n || buf[i] == '\n') {
                i++;
                continue;
            }

            // Parse tokens
            int arg_index = base;
            while (i < n && buf[i] != '\n') {
                char *p = &buf[i];
                while (i < n && buf[i] != ' ' && buf[i] != '\n') i++;
                if (i < n) buf[i++] = 0; // Null-terminate
                if (arg_index < MAXARGS - 1) {
                    newargv[arg_index++] = p;
                }
            }

            newargv[arg_index] = 0; // Null-terminate argv array

            if (fork() == 0) {
                exec(newargv[0], newargv);
                fprintf(2, "xargs: exec failed\n");
                exit(1);
            }
            wait(0);

            // Skip newline
            if (i < n && buf[i] == '\n') i++;
        }
    }

    exit(0);
}
