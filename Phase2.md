# System-Call-Aware Multi-Level Feedback Queue Scheduler in xv6

## Modifications made to proc.h
#### Modifications made to struct proc
- struct proc is extended to implement the MLFQ scheduler by including the following fields.
   - level: It describes the current level or queue in which the process is present
   - ticks_consumed: It represents the ticks consumed by the process in a particular level from the time it is scheduled in that level.
   - total_ticks: It represents the total ticks consumed by a process in each queue.
   - times_scheduled: It represents the number of times a process is scheduled.
   - prev_syscallcount: It represents the total number of syscalls by the process before its current time slice. It helps in computing the number of syscalls in the current time slice.
   - isboosted: It helps in moving all the RUNNING processes to queue 0 in all the CPUs.

- A struct mlfq is declared in proc.h which is used to implemented each queue of the MLFQ scheduler.
- A struct mlfqinfo is declared in proc.h which is used in the implementation of the syscall getmlfqinfo which takes struct mlfqinfo* as an argument. It is also declared in user.h so that the syscall getmlfqinfo can be called by the user.
- The time_quantum slice information is stored by declaring an time_quantum array. It is made extern so that it can be used in other files.
  
## System Calls Implemented

### getlevel()
#### DESCRIPTION:
Returns the current MLFQ (Multi-Level Feedback Queue) level of the calling process.

#### RETURN VALUE:
An integer in the range 0–3 representing the current MLFQ level.

#### IMPLEMENTATION:
This system call is implemented in kernel/sysproc.c. It uses the myproc() function
to obtain the struct proc pointer of the currently running process. The process
lock (p->lock) is acquired before accessing the level field to ensure proper
synchronization. The value of p->level is returned after releasing the lock.


### getmlfqinfo(int pid, struct mlfqinfo *info)
#### DESCRIPTION:
Retrieves scheduling statistics of the process with the given PID. The kernel
fills the following structure:

        struct mlfqinfo {
        int level;              // current queue level
        int ticks[4];           // total ticks consumed at each level
        int times_scheduled;    // number of times the process has been scheduled
        int total_syscalls;     // total system calls made
        };

#### RETURN VALUE:
- 0 on success  
- -1 if the given PID is invalid

#### IMPLEMENTATION:
The system call handler sys_getmlfqinfo() in kernel/sysproc.c retrieves the
arguments using argint() and argaddr(), and calls kgetmlfqinfo(pid, info)
implemented in kernel/proc.c. The function searches the process table for the
given PID, and if found, fills the mlfqinfo structure using the process’s
scheduling statistics such as level, ticks consumed at each level,
times_scheduled, and syscallcount. Proper process locking is used while
accessing these fields.

## Experimental Analysis

### MLFQ Scheduler – Experimental Analysis

The following workloads were tested to verify the behavior of the Multi-Level Feedback Queue (MLFQ) scheduler implementation.:

- CPU-bound workload
- Syscall-heavy workload
- Mixed workload

These experiments demonstrate that:

- CPU-bound processes migrate to lower priority queues
- Syscall-heavy processes remain in higher priority queues
- No starvation occurs
- Global priority boost functions correctly

---

## 1. CPU-Bound Workload

### Test Description

The following program creates a CPU-bound workload by performing a long computation loop with minimal system calls. The only syscall used is `getlevel()` to observe the queue level.

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
  int x = 0;

  while(1){
    for(int i = 0; i < 400000000; i++){
      x += i;
    }
    int lvl = getlevel();
    printf("%d", lvl);
  }
}
```

### Observed Output

```
00011111111122222223333333333333333333301111111112222222333333333333333333333333333333333333333
```

### Analysis

The output shows the process initially running in **queue level 0**, which is the highest priority queue. As the process repeatedly consumes its entire time slice without yielding, the scheduler **demotes it to lower priority queues**.

The observed pattern shows:

- The process begins execution at **Level 0**
- After using its full time slice, it moves to **Level 1**
- It is further demoted to **Level 2**
- Eventually it reaches **Level 3**, which is the lowest priority queue

The appearance of `0` again after a sequence of `3`s indicates the occurrence of the **global priority boost**. According to the scheduler policy, every fixed number of timer ticks (128 ticks in this implementation), all runnable processes are moved back to **Level 0**.

This prevents long-running CPU-bound processes from being permanently stuck in the lowest priority queue and ensures fairness.

This experiment confirms that:

- CPU-bound processes migrate to lower queues
- Global priority boost correctly resets process priority
- Long-running processes eventually regain higher priority

---

## 2. Syscall-Heavy Workload

### Test Description

The following program represents a syscall-heavy (interactive) workload. The process repeatedly performs system calls such as `printf()` and `pause()`.

```c
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
```

## Observed Output

```
level: 0
level: 0
level: 0
level: 0
level: 0
level: 0
level: 0
level: 0
level: 0
level: 0
level: 0
```

## Analysis

The output shows that the process consistently remains in **Level 0**, which is the highest priority queue.

This behavior occurs because the process frequently makes system calls resulting in number of syscalls in a partiular time slice more than the number of ticks in that time slice. Hence, the scheduler does not penalize it with priority demotion.

According to MLFQ scheduling principles, processes that frequently perform I/O or system calls are treated as **interactive workloads** and are given higher priority to improve responsiveness.

This experiment demonstrates that:

- Syscall-heavy processes remain in higher priority queues
- Interactive workloads receive better scheduling priority
- The scheduler favors I/O-bound processes over CPU-bound processes

---

# 3. Mixed Workload

## Test Description

This test creates two processes:

1. A **CPU-bound child process** that performs heavy computation.
2. A **syscall-heavy parent process** that repeatedly performs system calls and sleeps.

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
    int pid = fork();

    if(pid < 0){
        printf("Fork failed\n");
        exit(-1);
    } 
    else if(pid == 0){
        int x = 0;
        while(1){
            for(int i = 0; i < 800000000; i++){
                x += i;
            }
            int lvl = getlevel();
            printf("C%d\n", lvl);
        }
    }
    else{
        while(1){
            printf("SYSCALL %d\n", getlevel());
            pause(5);
        }
    }

    wait(0);
    exit(0);
}
```

## Observed Output (Excerpt)

```
SYSCALL 0
SYSCALL 0
C1
C1
SYSCALL 0
C2
SYSCALL 0
C2
SYSCALL 0
C2
...
C3
SYSCALL 0
C3
...
C1
SYSCALL 0
C1
```

## Analysis

The output shows different scheduling behavior for the two processes.

The **syscall-heavy process** consistently prints:

```
SYSCALL 0
```

indicating that it remains in **Level 0**. This occurs because the process frequently makes system calls resulting in number of syscalls in a partiular time slice more than the number of ticks in that time slice. Hence, the scheduler does not penalize it with priority demotion..

The **CPU-bound process** gradually migrates through multiple queue levels:

```
C1 → C2 → C3
```

This indicates that the process continuously consumes its entire time slice and is therefore demoted to lower priority queues.

At certain points, the CPU-bound process moves from **Level 3 back to Level 1 or Level 0**, which occurs due to the **global priority boost mechanism**. This periodic boost ensures that processes in lower queues are not starved and eventually receive higher priority again.

This experiment demonstrates that:

- CPU-bound processes migrate to lower priority queues
- Syscall-heavy processes remain at higher priority levels
- Global priority boosting resets priorities periodically
- Starvation is avoided

---

# Overall Observations

From these experiments, the following properties of the MLFQ scheduler implementation were verified:

1. **CPU-bound processes migrate to lower queues** as they continuously consume CPU time.
2. **Syscall-heavy processes remain in higher priority queues** because of comparison of number of syscalls with number of ticks in a particular time slice.
3. **Global priority boosting works correctly**, periodically resetting priorities of runnable processes.
4. **Starvation does not occur**, since even long-running CPU-bound processes eventually regain higher priority through priority boosting.

These results confirm that the implemented scheduler successfully differentiates between **CPU-bound and interactive workloads**, improving responsiveness for interactive tasks while maintaining fairness among all processes.