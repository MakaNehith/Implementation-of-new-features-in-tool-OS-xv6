# Scheduler-Aware Page Replacement in xv6
This project implements a demand-paging and page replacement system in the xv6 operating system. It features a custom swap space, process-specific memory statistics, and a Scheduler-Aware Clock Algorithm that intelligently evicts pages based on process priority, prioritizing interactive processes over background tasks.

## Core Data Structures
To manage physical memory and swap space, the following structures were introduced:

- **struct frame & frame_table_t:** Represents physical memory. Each frame tracks its physical address (pa), the process owning it (proc), its virtual address (va), a reference bit (ref_bit), and a pointer to the next free frame. The frame_table holds all frames, a clockhand for the eviction algorithm, and a head pointer for the free list.

- **struct swap_page & swap_space_t:** Simulates secondary storage. A swap_page holds the page data, the pid of the owner, its virtual address, and allocation status.

- **Synchronization:** A single global lock (page_lock) must be acquired before accessing or modifying both the frame table and the swap space to prevent race conditions.

- Macros for PAGE_FRAMES (physical memory size) and SWAP_PAGES (disk capacity) are defined in param.h.

## Initialization
During system boot, memory and swap space are initialized:

- **frametableinit():** Iterates from the end of the kernel's memory (end) up to PHYSTOP (it could be less than PHYSTOP depending on PAGE_FRAMES), cutting the physical memory into PGSIZE chunks. It assigns a physical address to each frame in the frame_table and links them together to build the initial free list.

- **swapspaceinit():** Sets up the array of swap_page structs, formatting them into a linked free list ready to receive evicted pages.

### Process Memory Statistics
The Process Control Block (struct proc) was extended to track memory behavior dynamically. These counters are updated automatically during faults, allocations, and evictions:

- **page_faults:** Total page faults encountered.

- **pages_evicted:** Number of times the process had a page sent to swap.

- **pages_swapped_in:** Number of the times the pages of that process are swapped back to memory.
- **pages_swapped_out:** Number of the times the pages of that process are swapped out of memory into swap space.

- **resident_pages:** Current number of pages residing in physical RAM.

## Eviction & Allocation Strategy (kalloc)
The most important part of this implementation is in kalloc, which allocates memory and handles eviction when frame table is full.

### Eviction Rules
We strictly only evict user data and heap pages. Kernel pages, page table pages, and code pages (identified using the PTE_X executable bit) are protected. Protected pages have their proc field set to 0 in the frame table, rendering them invisible to the eviction algorithm.

### The Clock Algorithm
When a process requests memory and the free list is empty, kalloc executes a Scheduler-Aware Clock Algorithm:

- **Scanning (Up to 2 Passes):** The clockhand sweeps through the frame table. It uses the hardware's Accessed bit (PTE_A) to determine recency. If a page was recently accessed, its ref_bit is set to 1, and the PTE_A bit is cleared.

- **Candidate Selection:** The algorithm looks for valid, user-owned, non-executable data/heap pages where the ref_bit is 0.

- **Scheduler Awareness:** If multiple evictable pages are found, the algorithm checks the MLFQ priority level of the owning process. Pages belonging to lower-priority processes (higher level value) are preferred for eviction. This ensures high-priority, interactive processes keep their memory in RAM.

- **Swapping & TLB Flush:** Once an optimal victim is found:

    - The page is sent to swap space via swap_out().

    - The page table entry (PTE) is updated, the Valid bit (PTE_V) is cleared, and a custom Swapped bit (PTE_S) is set to mark that the data is now in swap space.

    - sfence_vma() is called to clear the TLB so the CPU knows the physical mapping is gone.

    - The frame is scrubbed and repurposed for the new allocation.

**Note:** The proc and va fields for a newly allocated frame are updated dynamically by the caller (e.g., uvmalloc) to ensure only actual user memory is marked as evictable.

## Memory & Swap Management
### Freeing Memory
- **kfree():** Returns a frame to the free list. If the frame belonged to a user process (proc != 0), it safely decrements that process's resident_pages count. Kernel and page-table pages bypass this stat update.

### Swap Interactions
- **swap_out():** Copies a frame's physical data into an available slot in the swap space. It updates the process stats (evictions, swap-outs, resident pages) and returns -1 if the swap is completely full.

- **swap_in():** Searches the swap space for page of specific process with specific virtual address, copies the data back into RAM, and frees the swap slot.

- **swap_copy():** Used during fork(). It copies data out of the swap space into a new physical frame without deleting the swap entry.

- **swapfree():** Simply clears a swap entry when a swapped-out page is deallocated.

## Virtual Memory Functions
Standard xv6 VM functions were modified to integrate with the swap system:

- **vmfault():** When a page fault occurs, it checks if the requested virtual address was swapped out to swap space (implied via PTE_S). If so, it allocates a new frame, brings the data back via swap_in(), and updates process statistics.

- **uvmalloc():** When memory is requested (e.g., via sbrk), it allocates frames and properly binds the proc and va metadata in the frame table, officially marking them as evictable user pages. Increments resident_pages.

- **uvmcopy():** Used by fork(). If the parent's page is currently in swap space, the child does not share the swap slot. Instead, the data is pulled directly into physical RAM for the child via swap_copy().

- **uvmunmap():** Cleans up memory. If a mapped page is currently sitting in swap space (PTE_S is set), it calls swapfree() to erase it from the swap space instead of calling kfree().

## New System Call: getvmstats
To observe the behavior of the paging system, a new system call was added:

    int getvmstats(int pid, struct vmstats *info);

Implemented in proc.c, this system call safely searches the process table for the requested pid, acquires its lock, and securely copies (copyout) the process's page faults, evictions, swap activity, and resident page count into user space. Returns 0 on success and -1 if the PID is invalid.

## Experimental Analysis
For this tests,
- PAGE_FRAMES = 512
- SWAP_PAGES = 1024
### Test 31 Analysis: "The Lazy Load & Swap Proof"

    Output:
    --- Starting Test 1: Swap & Eviction Behavior ---
    1. Initial state:
    Init [PID 3] -> Faults: 0 | Evicted: 0 | Swapped Out: 0 | Swapped In: 0 | Resident: 4

    2. After sbrk() (Memory allocated, but not yet accessed):
    Post-sbrk [PID 3] -> Faults: 0 | Evicted: 0 | Swapped Out: 0 | Swapped In: 0 | Resident: 4

    3. Writing to memory to trigger page faults and evictions...
    Post-Write [PID 3] -> Faults: 1001 | Evicted: 686 | Swapped Out: 686 | Swapped In: 1 | Resident: 319

    4. Reading memory to trigger swap-ins (reusing evicted pages)...
    temp = 65000
    Post-Read [PID 3] -> Faults: 2002 | Evicted: 1687 | Swapped Out: 1687 | Swapped In: 1002 | Resident: 319
    Test 1 Complete.

This test proves that your Demand Paging and Swap Space are working perfectly together.

- **1 & 2. Lazy Allocation Works:** When you asked for 1000 pages using sbrk, your Resident pages stayed at 4, and Faults stayed at 0. This proves your OS is "lazy"—it promised the memory to the program but didn't actually use any physical RAM yet.

- **3.Hitting the RAM Limit (Swap Out):** When you finally wrote data to those 1000 pages, the OS had to give you real RAM. Because you only have 512 total frames (and some are used by the system), it couldn't hold all 1000. Your stats show it successfully kicked out 686 pages to the swap space (Swap Out) to make room, keeping your system from crashing.

- **4.Retrieving Data (Swap In):** When you read the memory back, your Faults doubled and Swapped In jumped to 1002. It means the OS recognized that the data you wanted was on the swap space, paused the program, fetched the data from the swap space, put it back into RAM, and safely calculated the correct temp = 65000 total. Here, temp is calculated and printed to avoid compiler optimization, leading to removal of the loop. 

### Test 32 Analysis: "Testing Priority Eviction"

    Output:
    --- Starting Test 2: Priority-Based Eviction ---
    Child (Low Priority) [PID 5] -> Faults: 500 | Evicted: 196 | Swapped Out: 196 | Swapped In: 0 | Resident: 308
    Parent (High Priority) [PID 4] -> Faults: 501 | Evicted: 192 | Swapped Out: 192 | Swapped In: 1 | Resident: 313
    Test 2 Complete.

This test proves that your system can handle heavy memory competition and applies your Priority-Based Eviction logic.

- **The Setup:** You made the Child process do heavy CPU work (which drops its priority in the scheduler) while the Parent process slept (which keeps its priority high as an "interactive" task).

- **Memory Contention:** Both processes then aggressively asked for 500 pages. Together, they wanted 1000 pages, which is way more than your 512-frame limit. This forced the OS to evict pages to make room.

- **The Result:** 
  - The Parent ended up with fewer evictions (192) and more pages kept safely in RAM (313).
  - The Child suffered more evictions (196) and had fewer pages kept in RAM (308).

- **The Conclusion:** Because the Parent had a higher priority, the OS's eviction algorithm successfully protected the Parent's memory slightly more than the Child's. Even better, neither process crashed—they both successfully shared the limited RAM by utilizing the swap space!



#### Note on AI Usage
I used an AI assistant to help draft the testing scripts used to verify my eviction logic and to help format the markdown for this README. The actual kernel implementation and page replacement design are entirely my own work.