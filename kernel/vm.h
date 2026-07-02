#include "types.h"
#include "param.h"

extern char end[]; // first address after kernel.
// defined by kernel.ld.

#define PA2FTIDX(pa) ((((uint64)pa) - (PGROUNDUP((uint64)end)))/PGSIZE)

#define FTIDX2PA(index) ((PGROUNDUP((uint64)end)) + (((uint64)index)*PGSIZE))

struct proc;
struct spinlock;


#define SBRK_EAGER 1
#define SBRK_LAZY  2

struct frame{
    int is_used;
    struct proc* proc;
    uint64 va;
    int ref_bit;
    uint64 pa;
    int next_free_index;
};
  
struct frame_table_t{
  struct frame frames[PAGE_FRAMES];
  int clockhand;
  int freelist_head;
};
  
struct swap_page{
  int is_used;
  int pid;
  uint64 va;
  // char data[PGSIZE];
  int next_free_index;
  int saved_raid_level;
};
  
struct swap_space_t{
  struct swap_page pages[SWAP_PAGES];
  int freelist_head;
};

extern struct frame_table_t frame_table;
extern struct swap_space_t swap_space;
extern struct spinlock page_lock;