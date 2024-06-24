#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PIPE_READ  0
#define PIPE_WRITE 1

int
main(int argc, char *argv[])
{
  int p2c[2];
  int c2p[2];
  char ch = 'p';
  int pid;

  pipe(p2c);
  pipe(c2p);

  if((pid = fork()) < 0){
    fprintf(2, "Failed to fork a child!\n");
    goto failed;
  }

  if(pid == 0){
    /* child process */
    close(p2c[PIPE_WRITE]);
    close(c2p[PIPE_READ]);
    if(read(p2c[PIPE_READ],&ch,sizeof(ch)) == sizeof(ch)){
      printf("%d: received ping\nwhich is %c\n",getpid(),ch);
    }
    write(c2p[PIPE_WRITE],&ch,sizeof(ch));
    close(p2c[PIPE_READ]);
    close(c2p[PIPE_WRITE]);
    exit(0);
  }else{
    /* parent process */
    close(p2c[PIPE_READ]);
    close(c2p[PIPE_WRITE]);
    write(p2c[PIPE_WRITE],&ch,sizeof(ch));
    if(read(c2p[PIPE_READ],&ch,sizeof(ch)) == sizeof(ch)){
      printf("%d: received pong\nwhich is %c\n",getpid(),ch);
    }
    close(p2c[PIPE_WRITE]);
    close(c2p[PIPE_READ]);
  }

  exit(0);

failed:
  close(p2c[0]);
  close(p2c[1]);
  close(c2p[0]);
  close(c2p[1]);
  exit(1);
}
