/*  Write a user-level program that uses xv6 system calls to ''ping-pong'' a byte between two processes over a pair of pipes, one for each direction. 
The parent should send a byte to the child; the child should print "<pid>: received ping", where <pid> is its process ID, write the byte on the pipe to the parent, and exit; the parent should read the byte from the child,
print "<pid>: received pong", and exit. */

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {

    if (argc != 2) { printf("Usage: pingpong <char>\n"); exit(1);};

    int p[2]; // parent to child 
    pipe(p);

    int q[2]; // child to parent
    pipe(q);

    char buf[2]; //xv6 has some weird buffer issue with %c; this is a workaround
    buf[0] = argv[1][0];
    buf[1] = '\0';

    printf("%s\n", buf);
    printf("buffer: %s\n", buf);

    if (fork() == 0) { 
        // child process
        close(p[1]); // close write end of p
        close(q[0]); // close read end of q

        read(p[0], &buf, 1); // read from parent
        printf("%d: received ping (\"%s\")\n", getpid(), buf);        
        
        write(q[1], &buf, 1); // write to parent

        close(p[0]);
        close(q[1]);
        exit(0);
    }        
    else {   
        //parent process
        close(p[0]); // close write end of p
        close(q[1]); // close read end of q

        write(p[1], &buf, 1); // write to child 
        // printf("%d: sent ping (\"%c\")\n", getpid(), c);

        read(q[0], &buf, 1); // read from child
        printf("%d: received pong (\"%s\")\n", getpid(), buf);

        close(p[1]);
        close(q[0]);

        wait(0);        
        exit(0);
    }

}