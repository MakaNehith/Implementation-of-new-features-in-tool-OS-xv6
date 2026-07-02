#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096 

void print_stats(char *prefix, int pid){
  struct vmstats st;
  struct mlfqinfo ml;
  if(getvmstats(pid, &st) == 0 && getmlfqinfo(pid, &ml) == 0){
    printf("%s [PID %d] -> Faults: %d | Evicted: %d | Swapped Out: %d | Swapped In: %d | Resident: %d\n",
           prefix, pid, st.page_faults, st.pages_evicted, st.pages_swapped_out, st.pages_swapped_in, st.resident_pages);
    printf("Level: %d\n",ml.level);
  }
  else{
    printf("%s: getvmstats failed for PID %d\n", prefix, pid);
  }
}

// Test 2: Priority-based eviction
void test_priority_eviction() {
  printf("\n--- Starting Test 2: Priority-Based Eviction ---\n");
  
  int pid = fork();
  if(pid < 0){
    printf("Fork failed!\n");
    exit(1);
  }

  // Use a smaller number of pages to prevent OOM while allowing concurrent competition
  // Adjust this number up or down depending on your physical memory limits
  int test_pages = 500; 

  if(pid == 0){
    // CHILD PROCESS: We want this to be the LOWER priority process.
    for(int i = 0; i < 1000000000; i++); 
    
    // Allocate memory and fault
    char *mem = sbrklazy(test_pages*PGSIZE);
    for(int i = 0; i<test_pages; i++){
      mem[i * PGSIZE] = 'C';
    }
    
    print_stats("Child (Low Priority)", getpid());
    exit(0);
  }
  else{
    // PARENT PROCESS: We want this to be the HIGHER priority process.
    pause(10); 
    
    // Allocate memory and fault
    char *mem = sbrklazy(test_pages*PGSIZE);
    for(int i = 0; i < test_pages; i++){
      mem[i * PGSIZE] = 'P';
    }
    
    // Wait for the child AFTER the parent has allocated its own memory.
    // This forces both processes to exist in memory at the exact same time,
    // triggering a true priority-based eviction battle.
    wait(0); 
    
    print_stats("Parent (High Priority)", getpid());
    printf("Test 2 Complete.\n");
  }
}

int main(){
  test_priority_eviction();
  exit(0);
}