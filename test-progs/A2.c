#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
    int pid, pid2;
    pid = getpid();
    pid2 = getpid2();

    printf("PID using getpid(): %d\n",pid);
    printf("PID using gepid2(): %d\n",pid2);
    exit(0);
}