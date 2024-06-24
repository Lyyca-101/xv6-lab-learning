#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/param.h"


int
main(int argc, char *argv[])
{
    char *xargs_argv[MAXARG];
    char buf[512],*p;
    char ch;
    int i,count = 0;

    if(argc < 2){
        fprintf(2, "Usage: xargs command [option]...\n");
        exit(1);
    }

    memset(xargs_argv,0,sizeof(xargs_argv));

    for(i = 1;i < argc; i++)
        xargs_argv[count++] =  argv[i];
    
    while(read(0,&ch,1)>0){
        *p++=ch;
        if(ch=='\n'){
            char *arg;
            p--;
            *p='\0';
            arg = (char*)malloc(strlen(buf) + 1);
            memcpy(arg,buf,strlen(buf) + 1);
            xargs_argv[count++] = arg;
            p = buf;
        }
    }
    if(p != buf){
        *p = '\0';
        char *arg = (char*)malloc(strlen(buf) + 1);
        
        memcpy(arg,buf,strlen(buf) + 1);
        xargs_argv[count++] = arg;
    }

/*     for(--count;count>=0;count--){
        printf("%s\n",xargs_argv[count]);
    } */

    if(fork()==0){
        exec(xargs_argv[0],xargs_argv);
        fprintf(2, "exec %s failed\n", xargs_argv[0]);
        exit(1);
    }

    wait(0);
    exit(0);
}