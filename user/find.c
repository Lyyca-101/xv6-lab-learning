#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"


char*
fmtname(char *path)
{
    static char buf[DIRSIZ+1];
    char *p;

    // Find first character after last slash.
    for(p=path+strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if(strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    buf[strlen(p)] = '\0';
    return buf;
}

void
find(char *path,char *file)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    //printf("debug: %s %s\n",path,file);

    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
        case T_FILE:
            if(!strcmp(fmtname(path),file)){
/*                 char buf[512];
                memcpy(buf,path,strlen(path));
                buf[strlen(path)]='\n';
                buf[strlen(path)+1]='\0';
                write(1,buf,strlen(path)+2); */
                printf("%s\n",path);
            }
            break;

        case T_DIR:
            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
                printf("ls: path too long\n");
                break;
            }
            strcpy(buf, path);
            p = buf+strlen(buf);
            *p++ = '/';
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0)
                    continue;
                if(!strcmp(de.name,".")||!strcmp(de.name,".."))
                    continue;
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                find(buf,file);
            }
            break;
    }
    close(fd);
}

int
main(int argc, char *argv[])
{

    if(argc < 2){
        fprintf(2, "Usage: find directory file...\n");
        exit(1);
    }

    find(argv[1],argv[2]);

    exit(1);
}