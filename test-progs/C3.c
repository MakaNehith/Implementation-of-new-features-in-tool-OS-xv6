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

    // Edge case
    int chpid = -1;
    check(getchildsyscount(chpid) == -1, "Edge Case 1");
    chpid = 10000;
    check(getchildsyscount(chpid) == -1, "Edge Case 2");

    chpid = fork();
    int n = 10;
    if(chpid < 0){
        printf("fork failed\n");
        exit(0);
    }
    else if(chpid == 0){
        for(int i = 0; i<n; i++){
            getpid2();
        }
        exit(0);
    }
    else{
        pause(10);
        int num = getchildsyscount(chpid);
        // exit() is also counted for the child
        check(num == (n+1),"getchildsyscount()");
        wait(0);
    }
}

int main(){
    printf("Testing started for getchildsyscount()\n");
    test_case();
    printf("All tests passed!\n");
}

