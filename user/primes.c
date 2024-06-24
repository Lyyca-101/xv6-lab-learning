#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PIPE_READ  0
#define PIPE_WRITE 1


void sieve(int lp[2]);

int
main(int argc, char *argv[])
{
    int i;
    int pid;
    int p[2];

    pipe(p);

    pid = fork();

    if(pid == 0){
        sieve(p);
    }else if(pid < 0){
        printf("prime: fork failed!\n");
        close(p[0]);
        close(p[1]);
        exit(1);
    }

    close(p[PIPE_READ]);

    for(i = 2;i <= 35; i++){
        write(p[PIPE_WRITE],&i,sizeof(i));
    }
    close(p[PIPE_WRITE]);
    wait(0);
    exit(0);
}

void
sieve(int lp[2])
{
    int pid;
    int num;
    int p;
    int flag = 0;
    int rp[2];

    pipe(rp);
    close(lp[PIPE_WRITE]);

    read(lp[PIPE_READ],&num,sizeof(num));
    p = num;

    printf("prime %d\n",num);

    while(read(lp[PIPE_READ],&num,sizeof(num)) > 0){
        //printf("[%d]debug: %d\n",getpid(),num);
        if(!flag){
            pid = fork();
            if(pid == 0){
                close(lp[PIPE_READ]);
                sieve(rp);
            }else if(pid < 0){
                printf("prime: fork failed!\n");
                close(rp[0]);
                close(rp[1]);
                exit(1);
            }
            /* 
               close the read side of right pipe in father process
               after the child process is created.
               because the child process needs the read side. 
            */
            close(rp[PIPE_READ]);
            flag = 1;
        }
        if(num % p != 0){
            write(rp[PIPE_WRITE],&num,sizeof(num));
        }
    }
    close(rp[PIPE_WRITE]);
    close(lp[PIPE_READ]);
    wait(0);
    exit(0);
}