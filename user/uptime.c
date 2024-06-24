#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int ticks = uptime();
    printf("%d ticks since kernel starting.\n",ticks);
    exit(0);
}