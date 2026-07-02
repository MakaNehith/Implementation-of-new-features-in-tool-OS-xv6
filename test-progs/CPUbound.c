#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
  int x = 0;

  while(1){
    for(int i = 0; i<400000000; i++){
      x += i;
    }
    int lvl = getlevel();
    printf("%d",lvl);
  }

}