#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Formatting of the file is done using format document in vs code

// Verification of swap-space exhaustion

#define PGSIZE 4096
// 3000 pages exceeds 512 physical frames + SSD swap slots
#define EXHAUST_PAGES 3000 

void run_oom_workload()
{
    printf("  [Child] Requesting %d pages lazily...\n", EXHAUST_PAGES);

    // Call sbrklazy. Since it's lazy, this usually succeeds instantly because 
    // it just increments p->sz without actually claiming physical memory.
    char *mem = sbrklazy(EXHAUST_PAGES * PGSIZE);
    
    if (mem == (char *)-1 || mem == 0)
    {
        printf("  [Child] sbrklazy failed immediately. (Valid OS behavior!)\n");
        exit(1); 
    }

    printf("  [Child] Lazy allocation successful. Touching memory to force allocations & swaps...\n");

    for (int i = 0; i < EXHAUST_PAGES; i++)
    {
        // Write to the page to trigger the page fault.
        mem[i * PGSIZE] = 'X';

        // Print progress
        if (i % 250 == 0 && i > 0)
        {
            printf("  [Child] Successfully allocated & swapped %d pages...\n", i);
        }
    }

    // The loop should not finish as it exceeds total memory that can given to a process
    printf("  [Child] CRITICAL FAILURE: Successfully allocated %d pages!\n", EXHAUST_PAGES);
    
    exit(0); 
}

int main(int argc, char *argv[])
{
    printf("\n=== Starting Swap Exhaustion (OOM) Benchmark ===\n");
    printf("Goal: The kernel must kill the greedy child process without panicking the OS.\n\n");

    int pid = fork();

    if (pid < 0)
    {
        printf("Fork failed!\n");
        exit(1);
    }

    if (pid == 0)
    {
        run_oom_workload();
    }
    else
    {
        // Parent process waits safely
        int status;
        wait(&status);

        printf("\n");
        // the child was killed by the kernel
        // it will typically have a non-zero exit status.
        if (status != 0)
        {
            printf(">>> TEST PASSED: Child was successfully killed/exited (Status: %d). <<<\n", status);
            printf(">>> The OS gracefully handled Swap Exhaustion without deadlocking! <<<\n\n");
        }
        else
        {
            printf(">>> TEST FAILED: Child completed execution without being killed. <<<\n\n");
        }
    }

    exit(0);
}