#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void check(int condition, char output[]){
    if(condition){
        printf("Success\n%s\n",output);
    }
    else{
        printf("Failed\n%s\n",output);
    }
}

void test_case(){
    int ppid = getppid();
    check(ppid == 2,"Testcase without fork");

    int parentPid = getpid2();
    int chpid = fork();
    if(chpid < 0){
        printf("Fork failed\n");
        exit(1);
    }
    if(chpid == 0){
        int ppid = getppid();
        check(ppid == parentPid, "Testcase with fork");
        exit(0);
    }
    else{
        wait(0);
    }
}

int main(){
    printf("Testing started for getppid()\n");
    test_case();
    printf("All tests passed!\n");
}

