#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
  while(1){
    printf("level: %d\n", getlevel());
    pause(5);
  }
}