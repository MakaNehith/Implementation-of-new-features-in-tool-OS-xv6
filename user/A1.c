#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
    int rt = hello();
    printf("Return value of hello(): %d\n",rt);
    exit(0);
}