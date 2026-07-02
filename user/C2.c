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

    for(int i = 0; i<20; i++) getpid2();
    hello();
    int num = getsyscount();

    // 1 -> exec(), 1 -> sbrk(), 20 -> getpid2(), 1 -> hello(), 1 -> getsyscount() itself
    int validNumberOfChildren = 1 + 1 + 20 + 1 + 1;
    
    check(num = validNumberOfChildren, "getsyscount()");
}

int main(){
    printf("Testing started for getsyscount()\n");
    test_case();
    printf("All tests passed!\n");
}

