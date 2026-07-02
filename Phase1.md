# Extending xv6 with Custom System Calls

## PART-A:

### A1. hello()
#### DESCRIPTION:
- Prints the message "Hello from the kernel!" to the xv6 console.
#### RETURN VALUE:
- The return value is always 0.
#### IMPLEMENTATION:
- This system call is implemented in kernel/sysproc.c using the printf() function which is present in the kernel. 

### A2. getpid2()
#### DESCRIPTION:
- Returns the process identifier (PID) of the calling process.
#### IMPLEMENTATION:
- This system call is implemented in kernel/sysproc.c in the same way the existing system call getpid() is implemented. That is, it uses myproc() function to get the struct proc pointer of the current running process (calling process) and returns PID from it.

## PART-B

### B1. getppid()
#### DESCRITPION:
- Returns the PID of the parent of the calling process.
#### RETURN VALUE:
- Parent PID, if parent exists
- -1, if the calling process has no parent (for the init process)
#### IMPLEMENTATION:
The core logic for the getppid() system call is implemented in kgetppid()
    function in kernel/proc.c which is called inside its system call handler,
    sys_getppid() in kernel/sysproc.c. wait_lock is acquired before accessing
    the parent pointer of the calling process to ensure that parent-child 
    relationship (parent pointer) remains in correct state. As the init process
    PID is always 1 (in xv6), the case where calling process has no parent is 
    explicitly handled only for the init process matching its PID.

### B2. getnumchild()
#### DESCRIPTION:
- Returns the number of currently alive child processes of the calling
    process. It does not take into account the zombie child processes.
#### IMPLEMENTATION:
- The core logic for the getnumchild() system call is implemented in 
    kgetnumchild() in kernel/proc.c function which is called inside its system
    call handler, sys_getnumchild() in kernel/sysproc.c. kgetnumchild() function 
    iterates over the process table and counts alive processes whose parent 
    pointer matches with the current process pointer. wait_lock is acquired to
    safely access the parent pointer of the each process while iterating over
    the process table. Also, the lock of each child process is acquired before
    accessing its state. Here, the processes having its states as RUNNING, 
    RUNNABLE or SLEEPING are considered to be alive.

## PART-C

### C1. Per-Process System Call Counter
- A new field syscallcount is added to the struct proc in kernel/proc.h 
       to maintain the count of system calls invoked by a process since its 
       creation.
- The values are initialized to 0 by the procinit() function in kernel/proc.c.
       While freeing the struct proc of an exited process, its syscallcount value 
       is set to 0 by the freeproc() function in kernel/proc.c.
- The value is incremented in kernel/syscall.c, the dispatcher of system
       system calls for a process, with proper locking discipline when a process
       makes a system call.

### C2. getsyscount()
#### DESCRIPTION:
- Returns the system call count of the calling process.
#### IMPLEMENTATION:
- It is implemented in kernel/sysproc.c. It uses myproc() function to get the 
    struct proc pointer of the current running process (calling process) and 
    returns syscallcount from it. This systemcall itself is also counted its 
    return value.

### C3. getchildsyscount(int pid)
#### DESCRIPTION:
- Returns the system call count of a child process with the given PID.
#### RETURN VALUE:
- System call count if the pid is a child process pid of the calling
       process
- -1 otherwise
#### IMPLEMENTATION:
- The core logic for the getchildsyscount(pid) system call is implemented
    in the kgetchildsyscount(pid) in kernel/proc.c, which is called inside its
    system call handler, sys_getchildsyscount() in kernel/sysproc.c.
    sys_getchildsyscount() function accesses the argument to the system call 
    which is placed in a0 register in TRAPFRAME using argint() function and 
    calls kgetchildsyscount(pid) function passing as argument to it the accessed
    argument. kgetchildsyscount(pid) function looks for the process which is the
    child of the current process, and matches with the given PID. wait_lock is 
    acquired to safely access the parent pointer of the processes. If the child
    process is found, it returns syscallcount of the child process with proper
    locking discipline being followed. Else, it returns -1.