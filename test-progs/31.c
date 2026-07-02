#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NUM_PAGES 1000
#define PGSIZE 4096 

void print_stats(char *prefix, int pid){
  struct vmstats st;
  if(getvmstats(pid, &st) == 0){
    printf("%s [PID %d] -> Faults: %d | Evicted: %d | Swapped Out: %d | Swapped In: %d | Resident: %d\n",
           prefix, pid, st.page_faults, st.pages_evicted, st.pages_swapped_out, st.pages_swapped_in, st.resident_pages);
  }
  else{
    printf("%s: getvmstats failed for PID %d\n", prefix, pid);
  }
}

// Test 1: Allocating memory, forcing faults, eviction, and reusing swapped pages
void test_swap_behavior() {
  printf("\n--- Starting Test 1: Swap & Eviction Behavior ---\n");
  int pid = getpid();
  
  printf("1. Initial state:\n");
  print_stats("Init", pid);

  // Allocate a large memory region using sbrk
  char *mem = sbrklazy(NUM_PAGES * PGSIZE);
  if(mem == (char*)-1){
    printf("sbrk failed!\n");
    exit(1);
  }
  printf("\n2. After sbrk() (Memory allocated, but not yet accessed):\n");
  print_stats("Post-sbrk", pid);

  // Sequentially access pages to trigger page faults and force evictions
  printf("\n3. Writing to memory to trigger page faults and evictions...\n");
  for(int i = 0; i < NUM_PAGES; i++){
    mem[i * PGSIZE] = 'A'; 
  }
  print_stats("Post-Write", pid);

  // Read back the evicted pages to force them to be swapped back in
  printf("\n4. Reading memory to trigger swap-ins (reusing evicted pages)...\n");
  int temp = 0;
  for(int i = 0; i < NUM_PAGES; i++){
    temp += mem[i * PGSIZE]; 
  }
  printf("temp = %d\n",temp);
  print_stats("Post-Read", pid);
  printf("Test 1 Complete.\n");
}

int main(){
  test_swap_behavior();
  exit(0);
}