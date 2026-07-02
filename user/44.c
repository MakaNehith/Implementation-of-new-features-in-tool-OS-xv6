#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_PAGES 500


// Edge Case 1: System Call Input Validation
void test_syscall_bounds()
{
    printf("\n=== Edge Case 1: System Call Input Bounds Testing ===\n");

    int res;

    // 1. Test setraidlevel
    printf("Testing setraidlevel() with invalid level (99)...\n");
    res = setraidlevel(99);
    if (res != -1)
    {
        printf("[!] FAIL: setraidlevel(99) returned %d (Expected: -1)\n", res);
        exit(1);
    }
    printf(" -> PASSED: Invalid RAID level rejected.\n");

    // 2. Test setfaileddisk
    printf("Testing setfaileddisk() with invalid disk (99)...\n");
    res = setfaileddisk(99);
    if (res != -1)
    {
        printf("[!] FAIL: setfaileddisk(99) returned %d (Expected: -1)\n", res);
        exit(1);
    }
    printf(" -> PASSED: Invalid failed disk rejected.\n");

    // 3. Test setdisksched
    // Assuming 0 = FCFS, 1 = SSTF. A policy like 99 should be rejected.
    printf("Testing setdisksched() with invalid policy (99)...\n");
    res = setdisksched(99);
    if (res != -1)
    {
        printf("[!] FAIL: setdisksched(99) returned %d (Expected: -1)\n", res);
        exit(1);
    }
    printf(" -> PASSED: Invalid disk scheduling policy rejected.\n");

    printf("\n>>> Syscall Bounds Testing PASSED! <<<\n");
}

// Edge Case 2: Memory Shrink & Swap Leak Prevention
void test_swap_dealloc()
{
    printf("\n=== Edge Case 2: Memory Shrink & Swap Deallocation ===\n");
    printf("Goal: Verify sbrk(-size) frees swapped pages without leaking disk space.\n\n");

    int alloc_pages = 200;

    printf("[Dealloc] Allocating %d pages...\n", alloc_pages);
    char *mem = sbrklazy(alloc_pages * PGSIZE);
    if (mem == (char *)-1)
    {
        printf("[!] FAIL: sbrklazy failed!\n");
        exit(1);
    }

    printf("[Dealloc] Writing to %d pages to force swap-outs...\n", alloc_pages);
    for (int i = 0; i < alloc_pages; i++)
    {
        mem[i * PGSIZE] = 'A';
    }

    printf("[Dealloc] Shrinking memory by %d pages (sbrk negative)...\n", alloc_pages);
    // This should trigger uvmdealloc -> uvmunmap -> swapfree
    char *shrink_res = sbrklazy(-(alloc_pages * PGSIZE));
    if (shrink_res == (char *)-1)
    {
        printf("[!] FAIL: sbrk shrink failed!\n");
        exit(1);
    }

    printf("[Dealloc] Re-allocating %d pages...\n", alloc_pages);

    // If swapfree() wasn't called during the shrink, this might fail or cause
    // a swap space exhaustion panic prematurely.
    mem = sbrklazy(alloc_pages * PGSIZE);
    if (mem == (char *)-1)
    {
        printf("[!] FAIL: Re-allocation failed! You might have a swap space leak!\n");
        exit(1);
    }

    // Clean up
    sbrklazy(-(alloc_pages * PGSIZE));

    printf("\n>>> Swap Deallocation Testing PASSED! <<<\n");
}

// Edge Case 3: uvmcopy & swap_copy Integrity
void test_fork_swap_copy()
{
    printf("\n=== Edge Case 3: Fork & Swap Integrity Verification ===\n");
    printf("Goal: Verify that fork() correctly copies swapped-out pages via uvmcopy.\n\n");

    printf("[Parent] Allocating %d pages using sbrklazy...\n", NUM_PAGES);
    char *mem = sbrklazy(NUM_PAGES * PGSIZE);

    if (mem == (char *)-1 || mem == 0)
    {
        printf("[!] Parent sbrk failed!\n");
        exit(1);
    }

    // Write data to force allocations and swap-outs
    printf("[Parent] Writing data to fill RAM and force swap-outs...\n");
    for (int i = 0; i < NUM_PAGES; i++)
    {
        int *page_start = (int *)(mem + i * PGSIZE);
        *page_start = (0xDEADBEEF ^ i);
    }

    printf("[Parent] Data written. Pages are scattered across RAM and SSD.\n");
    printf("[Parent] Calling fork()... (This stresses uvmcopy and swap_copy)\n");

    int pid = fork();

    if (pid < 0)
    {
        printf("[!] Fork failed!\n");
        exit(1);
    }

    if (pid == 0)
    {
        // CHILD PROCESS (Silent unless there is an error)
        for (int i = 0; i < NUM_PAGES; i++)
        {
            int *page_start = (int *)(mem + i * PGSIZE);
            int expected = (0xDEADBEEF ^ i);

            if (*page_start != expected)
            {
                // Only print if something goes catastrophically wrong
                printf("\n[!] CRITICAL ERROR [Child]: Data corruption at page %d!\n", i);
                printf("[!] Expected: %x, Got: %x\n", expected, *page_start);
                exit(1);
            }
        }
        // Exit on success
        exit(0);
    }
    else
    {
        // PARENT PROCESS
        printf("[Parent] Fork successful! Both processes are now concurrently reading back data...\n");

        for (int i = 0; i < NUM_PAGES; i++)
        {
            int *page_start = (int *)(mem + i * PGSIZE);
            int expected = (0xDEADBEEF ^ i);

            if (*page_start != expected)
            {
                printf("\n[!] CRITICAL ERROR [Parent]: Data corruption at page %d!\n", i);
                printf("[!] Expected: %x, Got: %x\n", expected, *page_start);
                exit(1);
            }
        }

        // Wait for child to finish 
        int status;
        wait(&status);

        if (status != 0)
        {
            printf("\n[!] TEST FAILED: Child process crashed during verification.\n");
            exit(1);
        }

        // Once wait() completes successfully, we know both are safe.
        printf("[Parent] Parent data verification PASSED!\n");
        printf("[Parent] Child exited with status 0 (Child data verification PASSED!).\n");
        printf("\n>>> Fork & Swap Copy Testing PASSED! <<<\n");
    }
}

int main(int argc, char *argv[])
{

    // 1. Check if syscalls properly reject bad inputs
    test_syscall_bounds();

    // 2. Check if uvmdealloc successfully frees swap space to prevent leaks
    test_swap_dealloc();

    // 3. Check if fork() properly handles swapped pages (uvmcopy / swap_copy)
    test_fork_swap_copy();

    printf("\n*************************\n");
    printf("  ALL EDGE CASES PASSED!\n");
    printf("*************************\n\n");

    exit(0);
}