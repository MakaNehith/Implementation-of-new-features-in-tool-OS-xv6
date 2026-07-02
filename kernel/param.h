#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGBLOCKS    (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       20000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define USERSTACK    1     // user stack pages
#define NMLFQ        4     // number of mlfq
#define PRIORITYBOOST 128  // prirority boost of MLFQ scheduler
#define PAGE_FRAMES 1024 // Number of page frames in the frame table
#define SWAP_PAGES  1024  // Number of pages in the swap space
#define FSLIMIT     5000
#define SWAPSTART   5000  // start block of the Swap space
#define SSDSIZE     2048  // size of each swap space disk
#define NSSD        4     // Number of swap space disks

