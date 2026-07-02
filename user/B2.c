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

    int num = getnumchild();
    check(num == 0, "Process without children");

    int n = 6;
    int chpid[n];
    for(int i = 0; i<n; i++){
        chpid[i] = fork();
        if(chpid[i] < 0){
            printf("Fork failed!\n");
            exit(1);
        }
        if(chpid[i] == 0){
            while(1);
            exit(0);
        }
    }

    num = getnumchild();
    check(num == n, "Process with children");
    for(int i = 0; i<n; i++) kill(chpid[i]);
}

int main(){
    printf("Testing started for getnumchild()\n");
    test_case();
    printf("All tests passed!\n");
}

