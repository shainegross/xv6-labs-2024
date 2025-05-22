#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(2, "Usage: sleep seconds\n");
        exit(1);
    }

    int i, seconds;

    for (i = 1; i < argc; i++) {
        seconds = atoi(argv[i]);
        if (seconds < 0) seconds = 0;
        sleep(seconds);
    }
    exit(0);
}
