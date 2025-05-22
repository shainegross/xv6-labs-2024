/*  Write a simple version of the UNIX find program for xv6: find all the files in a directory tree 
with a specific name. */

#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void find(char *path, char *element) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, O_RDONLY)) < 0){fprintf(2, "find: cannot open %s\n", path); return;}
    if(fstat(fd, &st) < 0) {fprintf(2, "find: cannot stat %s\n", path); close(fd); return;}
    if (st.type != T_DIR) {fprintf(2, "find: %s is not a directory\n", path); close(fd); return;}    
    
    while(read(fd, &de, sizeof(de)) == sizeof(de)){

        if (de.inum == 0) continue;        

        // Build full path
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {fprintf(2, "find: path too long\n"); continue; }
    
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;

        if(stat(buf, &st) < 0){ printf("find: cannot stat %s\n", buf); continue;}

        //printf("%s: %s\n", buf, de.name);
        if (strcmp(de.name, element) == 0) printf("%s\n", buf); // Found match

        if (st.type == T_DIR && strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0) {
            find(buf, element);
        }     
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc < 3) { printf("Usage: find <path> <name>\n"); exit(1);}
    for(int i = 2; i < argc; i++) find(argv[1], argv[i]);
    exit(0);
}
    
