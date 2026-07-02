# Implementation of new features in tool OS xv6

## Introduction
In this project, new features are implemented into the existing xv6 https://github.com/abhijitcse/cs3523-xv6-26 with limited features. The extension of the operating system took place in four phases, making the understanding of the concepts of OS more deeper while also providing a practical view.

## Phase 1: Extending xv6 with Custom System Calls
In this phase, new system calls are implemented in the xv6 code base, along with user-level programs to test their functionality. This also served as an initial step in understanding the xv6 codebase and the flow from user mode to kernel mode and back to user mode when a system call is made.

## Phase 2: Implementation of System-Call-Aware Multi-level Feedback Queue Scheduler(SC-MLFQ) in xv6
In the phase, the default round-robin scheduler is replaced with a 4-Level SC-MLFQ scheduler. The scheduling statistics are maintained per process so that they can be used in deciding the process to schedule according to the scheduling policy.
### Scheduler Specification
1) Number of queues: Exactly 4 queues (Level 0 highest, Level 3 lowest).
2) Time Quantum per level:
  - Level 0: 2 ticks
  - Level 1: 4 ticks
  - Level 2: 8 ticks
  - Level 3: 16 ticks
3) Scheduling rule: A RUNNABLE process is choosen from the highest non-empty queue. Within each queue, round-robin is used.
4) Demotion rule:
If a process uses its entire time slice, it is demoted by one level (unless already at Level 3).
5) System-call-aware rule

        Let:

        ΔS = number of system calls made during current time slice

        ΔT = number of ticks consumed during current time slice
        
        If ΔS ≥ ΔT:
        The process is considered interactive.(No demotion)
        
        Otherwise:
        Normal demotion rule
    This is an additional rule implemented apart from the traditional MLFQ scheduler. This rule ensures that the interactive processes remain in the higher priority queues.

6) Global priority boost: 
After every 128 timer ticks, all RUNNABLE processes are moved to Level 0.

This phase provides a deeper understanding of the scheduling policy. The implemented SC-MLFQ scheduler differentiates between CPU-bound and interactive workloads, improving responsiveness for interactive tasks while maintaining fairness among all processes.

## Phase 3: Implementation of Scheduler-Aware Page Replacement in xv6
In this phase, xv6's virtual memory subsystem is extended by implementing page replacement when memory becomes full. The existing xv6 repository implements lazy allocation, that is, when a process accesses a previously unmapped virtual page, a page fault
occurs and the kernel allocates a physical page dynamically. However, if the system runs out of
physical memory, xv6 currently kills the process. In this phase, the kernel is modified to implement page replacement. That is, if the system runs out of
physical memory, instead of terminating the process, the kernel evicts an existing page using a page replacement algorithm, stores its contents in a swap area, and
reuses the freed frame. When a swapped-out page is accessed again, it is restored back from swap. Here, swap space is implemented as a **fixed size array in memory, instead of a disk-backed space**. In addition, the scheduling level of a process is also considered during page replacement to ensure that interactive processes retain their working sets longer. This phase deepens our understanding of the concept of paging and swap space, and provides a flow of what happens when a virtual address is accessed to how the data is retrieved from the physical memory.

## Phase 4: Disk Scheduling and RAID-backed Swap in xv6
In this phase, xv6 is extended to support the disk-backed swap space instead of the in-memory swap space implemented in phase-3. The disk scheduling policies (First-Come-First-Serve(FCFS) and Shortest-Seek-Time-First(SSTF)) are implemented. RAID levels(0,1 and 5) are implemented for the swap space by simulating the disks in software in single fs.img. This phase effectively demonstrates the practical implementation of the concepts of disk scheduling policies such as FCFS and SSTF, as well as RAID techniques such as striping, mirroring, and parity.


## ACKNOWLEDGMENTS

xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix
Version 6 (v6).  xv6 loosely follows the structure and style of v6,
but is implemented for a modern RISC-V multiprocessor using ANSI C.

xv6 is inspired by John Lions's Commentary on UNIX 6th Edition (Peer
to Peer Communications; ISBN: 1-57398-013-7; 1st edition (June 14,
2000)).  See also https://pdos.csail.mit.edu/6.1810/, which provides
pointers to on-line resources for v6.

The following people have made contributions: Russ Cox (context switching,
locking), Cliff Frey (MP), Xiao Yu (MP), Nickolai Zeldovich, and Austin
Clements.

We are also grateful for the bug reports and patches contributed by
Abhinavpatel00, Takahiro Aoyagi, Marcelo Arroyo, Hirbod Behnam, Silas
Boyd-Wickizer, Anton Burtsev, carlclone, Ian Chen, clivezeng, Dan
Cross, Cody Cutler, Mike CAT, Tej Chajed, Asami Doi,Wenyang Duan,
echtwerner, eyalz800, Nelson Elhage, Saar Ettinger, Alice Ferrazzi,
Nathaniel Filardo, flespark, Peter Froehlich, Yakir Goaron, Shivam
Handa, Matt Harvey, Bryan Henry, jaichenhengjie, Jim Huang, Matúš
Jókay, John Jolly, Alexander Kapshuk, Anders Kaseorg, kehao95,
Wolfgang Keller, Jungwoo Kim, Jonathan Kimmitt, Eddie Kohler, Vadim
Kolontsov, Austin Liew, l0stman, Pavan Maddamsetti, Imbar Marinescu,
Yandong Mao, Matan Shabtay, Hitoshi Mitake, Carmi Merimovich,
mes900903, Mark Morrissey, mtasm, Joel Nider, Hayato Ohhashi,
OptimisticSide, papparapa, phosphagos, Harry Porter, Greg Price, Zheng
qhuo, Quancheng, RayAndrew, Jude Rich, segfault, Ayan Shafqat, Eldar
Sehayek, Yongming Shen, Fumiya Shigemitsu, snoire, Taojie, Cam Tenny,
tyfkda, Warren Toomey, Stephen Tu, Alissa Tung, Rafael Ubal, unicornx,
Amane Uehara, Pablo Ventura, Luc Videau, Xi Wang, WaheedHafez, Keiichi
Watanabe, Lucas Wolf, Nicolas Wolovick, wxdao, Grant Wu, x653, Andy
Zhang, Jindong Zhang, Icenowy Zheng, ZhUyU1997, and Zou Chang Wei.

### BUILDING AND RUNNING XV6

You will need a RISC-V "newlib" tool chain from
https://github.com/riscv/riscv-gnu-toolchain, and qemu compiled for
riscv64-softmmu.  Once they are installed, and in your shell
search path, you can run "make qemu".
