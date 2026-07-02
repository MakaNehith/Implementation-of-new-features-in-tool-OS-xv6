#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Formatting of the file is done using format document in vs code

// Testing of raid mappings

#define PGSIZE 4096
#define NUM_PAGES 150
#define NUM_CHILDREN 5

void stress_worker(int id, int raid_level, int is_mid_flight)
{
    char *mem = sbrklazy(NUM_PAGES * PGSIZE);

    if (mem == (char *)-1 || mem == 0)
    {
        printf("\n[!] Child %d: sbrk failed!\n", id);
        exit(1);
    }

    // Step 1: Write data to force swap-outs
    for (int i = 0; i < NUM_PAGES; i++)
    {
        int *page_start = (int *)(mem + i * PGSIZE);
        *page_start = (id << 16) | i;
    }

    pause(10);

    // MID-FLIGHT disk failure LOGIC
    if (is_mid_flight && id == 1) {
        printf("\n  ==================================================\n");
        printf("  [! MID-FLIGHT DISK FAILURE !]\n");
        printf("  Child 1 is calling setfaileddisk(1) NOW.\n");
        printf("  Simulating a catastrophic drive failure while processes are sleeping!\n");
        printf("  ==================================================\n\n");
        setfaileddisk(1);
    }
    
    pause(5); 

    // Step 2: Read data to force swap-ins and verify
    for (int i = 0; i < NUM_PAGES; i++)
    {
        int *page_start = (int *)(mem + i * PGSIZE);
        int expected = (id << 16) | i;

        if (*page_start != expected)
        {
            printf("\n[!] CRITICAL ERROR Child %d: Data corruption at page %d under RAID %d!\n", id, i, raid_level);
            printf("[!] Expected: %x, Got: %x\n", expected, *page_start);
            exit(1);
        }
    }

    exit(0);
}

void run_raid_test(int raid_level, int is_mid_flight)
{
    printf("\n==================================================\n");
    if (is_mid_flight) {
        printf("TESTING RAID LEVEL: %d (MID-FLIGHT ASSASSINATION)\n", raid_level);
        printf("Mode: Write Healthy -> Kill Disk via setfaileddisk() -> Read via Parity\n");
    } else {
        printf("TESTING RAID LEVEL: %d (STATIC FAILURE)\n", raid_level);
        if (raid_level == 0) printf("Mode: Standard Striping (No Fault Tolerance)\n");
        if (raid_level == 1) printf("Mode: Mirroring (Fault Tolerance via Duplicate)\n");
        if (raid_level == 5) printf("Mode: Parity (Fault Tolerance via XOR Reconstruction)\n");
    }
    printf("==================================================\n");

    if (setraidlevel(raid_level) < 0)
    {
        printf("Error: Failed to set RAID level to %d\n", raid_level);
        exit(1);
    }

    // Document and execute the disk failure setup
    if (raid_level == 0 || is_mid_flight) {
        printf("[Setup] Calling setfaileddisk(-1) to ensure ALL disks are HEALTHY.\n");
        setfaileddisk(-1); 
    } else if (raid_level == 1) {
        printf("[Setup] Calling setfaileddisk(1) to simulate STATIC FAILURE on Disk 1.\n");
        setfaileddisk(1);
    } else if (raid_level == 5) {
        printf("[Setup] Calling setfaileddisk(2) to simulate STATIC FAILURE on Disk 2.\n");
        setfaileddisk(2);
    }

    printf("[Parent] Spawning %d children. Each will allocate %d pages.\n", NUM_CHILDREN, NUM_PAGES);
    printf("[Parent] Children are concurrently writing to RAM to force swap-outs...\n");

    for (int i = 0; i < NUM_CHILDREN; i++)
    {
        int pid = fork();
        if (pid < 0)
        {
            printf("Fork failed!\n");
            exit(1);
        }
        if (pid == 0)
        {
            stress_worker(i + 1, raid_level, is_mid_flight);
        }
    }

    // Parent waits for all children
    printf("[Parent] Waiting for children to trigger page faults and swap-ins...\n");
    for (int i = 0; i < NUM_CHILDREN; i++)
    {
        int status;
        wait(&status);
        if (status != 0)
        {
            printf("\n>>> Test FAILED: A child process crashed during RAID %d test! <<<\n", raid_level);
            exit(1);
        }
    }
    
    printf("[Parent] All children reported successful data verification!\n");
    printf(">>> RAID %d Stress Test COMPLETE and VERIFIED! <<<\n", raid_level);
}

int main(int argc, char *argv[])
{
    printf("\nStarting Comprehensive RAID & Swap Verification Suite...\n");
    printf("Note: This suite utilizes the setfaileddisk() syscall to simulate physical\n");
    printf("hardware failures at the disk controller level during runtime.\n");

    // 1. Verify RAID 0 (Basic functionality - ALL HEALTHY)
    run_raid_test(0, 0);

    // 2. Verify RAID 1 (Static Failure: Disk 1 dead from start)
    run_raid_test(1, 0);

    // 3. Verify RAID 5 (Static Failure: Disk 2 dead from start)
    run_raid_test(5, 0);

    // 4. Verify RAID 5 (Dynamic Failure: Writes healthy, reads dead)
    run_raid_test(5, 1);

    // Final cleanup
    setraidlevel(0);
    setfaileddisk(-1);

    printf("\n**************************************************\n");
    printf("ALL RAID LEVELS (0, 1, 5) VERIFIED SUCCESSFULLY!\n");
    printf("MID-FLIGHT RECONSTRUCTION TEST PASSED!\n");
    printf("**************************************************\n\n");

    exit(0);
}