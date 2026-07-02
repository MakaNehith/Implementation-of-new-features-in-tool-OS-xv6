# Disk Scheduling and RAID-backed Swap in xv6

In this assignment, xv6 is extended to support the disk-backed swap space instead of the in-memory swap space implemented in PA-3. The disk scheduling policies (FCFS and SSTF) are implemented. RAID levels(0,1 and 5) are implemented for the swap space by simulating the disks in software in single fs.img
## Implementations

### 1. DISK SCHEDULING POLICIES
Disk scheduling is implemented in virtio_disk.c.
- A software queue (struct virtq_sched) is introduced to manage disk requests.
- Requests are scheduled based on a global variable disk_policy:
  - 0 → FCFS
  - 1 → SSTF

Workflow:
- When a disk request is issued, it enters virtio_disk_rw().
- The request is added to the scheduling queue using getrequest() (or sleeps if unavailable).
- postrequest() selects the next request based on the scheduling policy and submits it for I/O.
- The requesting process sleeps and is later awakened in virtio_disk_intr() upon I/O completion.

Latency Model:
Latency is computed as:

    latency = |current_block - requested_block| + C

where C = 10.

- The current disk head position is maintained globally and updated after each request.
- SSTF selects the closest request to the current head position, prioritizing higher-priority processes.

Statistics:

The following per-process statistics are maintained:

- disk_reads
- disk_writes
- avg_disk_latency

System Call:

    int setdisksched(int policy);

Used to dynamically switch between FCFS and SSTF.


### 2. RAID Implementation

Multiple disks are simulated within a single fs.img by increasing its size and partitioning it logically.

Two main functions are implemented:

- raid_write()
- raid_read()

These functions handle data placement and retrieval based on the selected RAID level.

#### RAID 0 (Striping)
- disk  = b % N
- block = b / N
- Provides parallelism but no fault tolerance.
#### RAID 1 (Mirroring)
- disk1 = (b % (NSSD / 2)) * 2
- disk2 = disk1 + 1
- block = b / (NSSD / 2)
- Data is duplicated across two disks for fault tolerance.
#### RAID 5 (Striping with Parity)
- block       = b / (NSSD - 1)
- parity_disk = block % NSSD
- disk        = b % (NSSD - 1)

- if (disk >= parity_disk)
    disk = disk + 1
- Parity is distributed across disks.
- Enables recovery from a single disk failure.
### 3. Swap System Integration
- The in-memory swap array from PA-3 is removed.
- raid_write() is used in swap_out() to write pages to disk.
- raid_read() is used in swap_in() to restore pages.
#### System Calls:

    int setraidlevel(int level);
    int setfaileddisk(int disk);
- setraidlevel() sets the RAID level.
- setfaileddisk() simulates disk failure (-1 means all disks are healthy).
#### Failure Handling:
- RAID 1: Reads from the mirrored disk if one disk fails.
- RAID 5:
  - If the parity disk fails → normal read.
  - If a data disk fails → data reconstructed using parity (XOR).
### 4. Statistics Integration

The following fields are added to struct vmstats:

    disk_reads
    disk_writes
    avg_disk_latency

These can be accessed using the getvmstats system call.

## Experimental Evaluation
Test 1: FCFS vs SSTF Performance (Test File 41)

This test runs the same workload (multiple child processes causing swap activity) under both scheduling policies.

Observation:
Performance varies across runs. In some cases, FCFS performs better, while in others SSTF achieves lower latency.

Sample Results:

Run 1:

- FCFS Avg Latency: 1988
- SSTF Avg Latency: 2093

Run 2:

- FCFS Avg Latency: 2069
- SSTF Avg Latency: 1961


Test 2: RAID & Swap Stress Test (Test File 42)

Spawns multiple processes to force heavy swapping.
Writes and verifies page data after swap-in.

Validates:

- RAID 0 → striping correctness
- RAID 1 → correctness under static failure
- RAID 5 → recovery under static and mid-flight failures

Disk failures are simulated using setfaileddisk().

Test 3: Swap Exhaustion Test (Test File 43)

- Requests more memory than available (RAM + swap).
- Ensures the system correctly handles exhaustion by terminating the process.
  

Test 4: Edge Case Testing (Test File 44)

- System Call Bounds: Invalid inputs are rejected correctly.
- Swap Deallocation: Ensures sbrk(-size) frees swap space properly.
- Fork & Swap Integrity: Verifies correct copying of swapped pages during fork().
## Design Choices and Assumptions
- Disk failures are handled only during raid_read() for data recovery.
- Swapped-out disk data is not cleared after swap-in.
- RAID 5 uses per-stripe locks to prevent corruption, since parity blocks are shared and updated on every write.


** Used AI help for testing
