#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Formatting of the file is done using format document in vs code

// Testing of FCFS and SSTF disk policies

#define PGSIZE 4096
#define NUM_CHILDREN 4
#define PAGES_PER_CHILD 250

// The workload that each child will run concurrently
void run_workload(int child_id, int pipe_write_fd)
{
    char *mem = sbrklazy(PAGES_PER_CHILD * PGSIZE);
    if (mem == (char *)-1)
    {
        printf("FAIL: Child %d sbrklazy failed!\n", child_id);
        exit(1);
    }

    // Phase 1: Sequential Write (Forces eviction/swap-out as RAM fills up)
    for (int i = 0; i < PAGES_PER_CHILD; i++)
    {
        mem[i * PGSIZE] = 'A' + child_id;
    }

    // Phase 2: Erratic Read (Forces erratic swap-ins)
    for (int i = 0; i < PAGES_PER_CHILD / 2; i++)
    {
        volatile char c1 = mem[i * PGSIZE];
        volatile char c2 = mem[(PAGES_PER_CHILD - 1 - i) * PGSIZE];
        (void)c1;
        (void)c2;
    }

    // Gather Stats for this specific child
    struct vmstats d_stats;
    uint64 latency_to_report = 0;

    if (getvmstats(getpid(), &d_stats) == 0)
    {
        printf("  -> Child %d | Reads: %d | Writes: %d | Avg Latency: %ld ticks\n",
               child_id, d_stats.disk_reads, d_stats.disk_writes, d_stats.avg_disk_latency);
        latency_to_report = d_stats.avg_disk_latency;
    }
    else
    {
        printf("  -> Child %d failed to get disk stats.\n", child_id);
    }

    // Send the latency back to the parent through the pipe
    write(pipe_write_fd, &latency_to_report, sizeof(latency_to_report));
    close(pipe_write_fd);

    exit(0);
}

// Function to spawn the children and wait for them
void run_concurrent_test(int policy, char *name)
{
    setdisksched(policy);
    printf("\n--- Starting %s Concurrent Benchmark ---\n", name);

    int fd[2];
    if (pipe(fd) < 0)
    {
        printf("Pipe creation failed!\n");
        exit(1);
    }

    // run the children simultaneously
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
            close(fd[0]);
            run_workload(i, fd[1]);
        }
    }

    close(fd[1]);

    uint64 total_latency = 0;
    uint64 child_latency = 0;

    // Parent waits for all children to finish their thrashing storm
    for (int i = 0; i < NUM_CHILDREN; i++)
    {
        wait(0);
        // Read the latency sent by each child
        if (read(fd[0], &child_latency, sizeof(child_latency)) > 0)
        {
            total_latency += child_latency;
        }
    }

    close(fd[0]);

    uint64 overall_avg = total_latency / NUM_CHILDREN;

    printf("=== %s Benchmark Complete ===\n", name);
    printf(">>> OVERALL Average Latency for %s: %ld ticks <<<\n", name, overall_avg);
}

int main()
{
    printf("=== Multi-Process Disk Scheduling Benchmark ===\n");

    run_concurrent_test(0, "FCFS");

    pause(20);

    run_concurrent_test(1, "SSTF");

    printf("\n=== All Testing Complete ===\n");
    exit(0);
}