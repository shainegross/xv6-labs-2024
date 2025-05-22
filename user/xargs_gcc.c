/* xargs. Unix version. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAXARGS 10
#define MAXBUF 512

int main (int argc, char *argv[]) {

    // Step 1: Copy initial arguments passed to argv
    char *new_argv[MAXARGS +1];
    char buf[MAXBUF];
    int tokenIndex = 0;

    for (int i = 1; (i < argc) & (tokenIndex < MAXARGS); i++) {
        new_argv[i - 1] = argv[i];
    }

    // Appends input directed from stdin to argv 
    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        char *p = buf;
        while (*p) {
            while (*p == ' ') p++; // Skip leading spaces
            if (*p == '\0' || tokenIndex >= MAXARGS - 1) break;
            
            if (*p == '"') {
                p++; // Skip opening quote
                char *start = p;
                while (*p && *p != '"') p++;
                if (*p == '"') *p = '\0'; // Null-terminate
                new_argv[tokenIndex++] = start;
                p++; // Move past closing quote
            } else {
                char *start = p;
                while (*p && *p != ' ') p++;
                if (*p) *p++ = '\0'; // Null-terminate
                new_argv[tokenIndex++] = start;
            }
        }
    }

    int pid = fork();
	if (pid == 0){
        // child
        execvp(new_argv[0], new_argv);
        perror("execvp failed");
        exit(1); 
    }
    else {
        wait(NULL);
    }        
    
    return 0;
}
