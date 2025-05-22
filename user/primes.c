/*  Write a concurrent prime sieve program for xv6 using pipes and the design illustrated in the picture halfway down this page and the surrounding text. 
https://swtch.com/~rsc/thread/
This idea is due to Doug McIlroy, inventor of Unix pipes.*/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Prototypes
void sieve(int input_fd);

// The program should take a single command-line argument, the maximum number to sieve.
int main(int argc, char *argv[]) {

    if (argc != 2) {printf("Usage: primes <int>\n"); exit(1);}
    int n = atoi(argv[1]);

    int p[2];  
    pipe(p);
    if (pipe(p) < 0) {printf("pipe failed\n"); exit(1);}

    if (fork() == 0) {
        // Child runs the sieve         
        close(p[1]);
        sieve(p[0]);
        close(p[0]);
        exit(0);
    }
    else {
        // Parent feeds numbers into the pipe
        close(p[0]);
        for (int i = 2; i <= n; i++)  write(p[1], &i, sizeof(int));
        close(p[1]);
        wait(0);
    }

    exit(0);
}

void sieve(int input_fd){
    // reads and prints first number
    int prime;
    if (read(input_fd, &prime, sizeof(int)) == 0) exit(0);
    printf("prime: %d\n", prime);

    int p[2];
    pipe(p);
    if (pipe(p) < 0) {printf("pipe failed\n"); exit(1);}

    if (fork() == 0) {
        //child filter for next prime
        close(p[1]);
        sieve(p[0]);
        close(p[0]);
        exit(0);
    } else {
        close(p[0]);
        int num;
        while (read(input_fd, &num, sizeof(int))) {
            if ((num % prime) != 0) write(p[1], &num, sizeof(int));
        }
        close(p[1]);
        close(input_fd);
        wait(0);          
    }
    exit(0);
}