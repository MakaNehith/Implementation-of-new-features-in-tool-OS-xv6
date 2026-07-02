#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
    int pid = fork();
    if(pid < 0){
        printf("Fork failed\n");
        exit(-1);
    } 
    else if(pid == 0){
        int x = 0;
        while(1){
            for(int i = 0; i<800000000; i++){
            x += i;
            }
            int lvl = getlevel();
            printf("C%d\n",lvl);
        }
    }
    else{
        while(1){
            printf("SYSCALL %d\n", getlevel());
            pause(5);
        }
    }
    wait(0);
    exit(0);
}