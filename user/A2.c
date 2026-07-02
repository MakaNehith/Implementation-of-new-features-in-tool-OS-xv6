#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
    int rt = (int)hello();
    if(rt != 0){
        exit(1);
    }
    exit(0);
}