// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "sleeplock.h"
#include "vm.h"
#include "fs.h"
#include "buf.h"

struct frame_table_t frame_table;

struct swap_space_t swap_space;

struct spinlock page_lock;

struct sleeplock raid_locks[SSDSIZE];

int raid_level = 0;

void
raid_init(void)
{
  for(int i = 0; i<SSDSIZE; i++){
    initsleeplock(&raid_locks[i],"raid_lock");
  }
}

void
frametableinit(void)
{
  frame_table.clockhand = 0;
  
  struct frame *f;
  char *p;
  int index = 0;
  p = (char*)PGROUNDUP((uint64)end);
  
  while(p + PGSIZE <= ((char*)PHYSTOP)){
    f = &(frame_table.frames[index]);
    memset((void*)p, 1, PGSIZE);
    
    f->pa = (uint64)p;
    f->is_used = 0;
    f->proc = 0;
    f->ref_bit = 0;
    f->va = (uint64)0;
    f->next_free_index = index + 1;
    
    index++;
    p += PGSIZE;
    
    if(index == PAGE_FRAMES) break;
  }
  
  if(index > 0) {
    frame_table.freelist_head = (index > 0 ? 0 : -1);
    frame_table.frames[index - 1].next_free_index = -1;
  }
}

void
swapspaceinit(void)
{
  swap_space.freelist_head = 0;
  struct swap_page* p;
  int index = 0;
  for(p = swap_space.pages; p <= &(swap_space.pages[SWAP_PAGES-1]); p++){
    p->pid = 0;
    p->va = (uint64)0;
    p->is_used = 0;
    p->saved_raid_level = 0;
    if(p == &(swap_space.pages[SWAP_PAGES-1])){
      p->next_free_index = -1; 
    }
    else{
      p->next_free_index = index+1;
    }
    index++;
    // memset(p->data, 2, PGSIZE);
  }
}

void
kinit()
{
  initlock(&page_lock,"frametable_swapspace");
  raid_init();
  frametableinit();
  swapspaceinit();
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  memset(pa, 1, PGSIZE);

  int index = PA2FTIDX(pa);
  struct frame* f;
  
  acquire(&page_lock);
  f = &(frame_table.frames[index]);

  struct proc *p = f->proc;

  if(p != 0){
    p->resident_pages--;
  }

  f->is_used = 0;
  f->proc = 0;
  f->ref_bit = 0;
  f->va = (uint64)0;

  f->next_free_index = frame_table.freelist_head;
  frame_table.freelist_head = index;
  
  release(&page_lock);

}

uint64 get_pblock(int disk, int vblock){
  if(disk >= NSSD || vblock >= SSDSIZE){
    panic("Invalid disk or vblock");
  }
  return (SWAPSTART + (disk*SSDSIZE) + vblock);
}

void
raid_write(int frame_idx, int swap_idx)
{
  int base = (swap_idx*2);
  struct frame *f = &(frame_table.frames[frame_idx]);

  if(raid_level == 0){
    for(int i = 0; i<4; i++){
      int disk = i%NSSD;
      int vblock = base + (i/NSSD);
      uint64 pblock = get_pblock(disk,vblock);

      struct buf *bf = bread((uint64)0,pblock);
      memmove((char*)(bf->data),((char*)(f->pa) + (BSIZE*i)),BSIZE);
      bwrite(bf);
      brelse(bf);
    }
  }

  else if(raid_level == 1){
    for(int i = 0; i<4; i++){
      int disk1 = (i%(NSSD/2))*2;
      int disk2 = disk1+1;
      int vblock = base + (i/(NSSD/2));

      uint64 pblock1,pblock2;
      struct buf *bf1, *bf2;

      pblock1 = get_pblock(disk1,vblock);
      pblock2 = get_pblock(disk2,vblock);
      bf1 = bread((uint64)0,pblock1);
      bf2 = bread((uint64)0,pblock2);

      memmove((char*)(bf1->data),((char*)(f->pa) + (BSIZE*i)),BSIZE);
      bwrite(bf1);
      brelse(bf1);

      memmove((char*)(bf2->data),((char*)(f->pa) + (BSIZE*i)),BSIZE);
      bwrite(bf2);
      brelse(bf2);
    }
  }

  else if(raid_level == 5){
    for(int i = 0; i<4; i++){
      int vblock = base + (i/(NSSD-1));
      int parity_disk = vblock%NSSD;
      int disk = (i%(NSSD-1));
      if(disk >= parity_disk) disk++;

      acquiresleep(&raid_locks[vblock]);

      uint64 pblock,parity_pblock;
      struct buf *bf, *parity_bf;

      pblock = get_pblock(disk,vblock);
      parity_pblock = get_pblock(parity_disk,vblock);
      bf = bread((uint64)0,pblock);
      parity_bf = bread((uint64)0,parity_pblock);

      char *new_data = ((char*)(f->pa) + (BSIZE*i));

      for(int k = 0; k<BSIZE; k++){
        parity_bf->data[k] = new_data[k];
      }

      for(int j = 0; j<NSSD; j++){
        if(j != parity_disk && j != disk){
          uint64 other_pblock = get_pblock(j,vblock);
          struct buf *other_bf = bread((uint64)0,other_pblock);
          for(int k = 0; k<BSIZE; k++){
            parity_bf->data[k] = (uint64)parity_bf->data[k] ^ (uint64)other_bf->data[k];
          }
          brelse(other_bf);
        }
      }

      memmove((char*)bf->data,new_data,BSIZE);
      bwrite(bf);
      brelse(bf);

      bwrite(parity_bf);
      brelse(parity_bf);

      releasesleep(&raid_locks[vblock]);

    }
  }
}


uint64
swap_out(int index){

  struct frame *f = &(frame_table.frames[index]);
  struct proc *p = f->proc;

  int swap_idx = swap_space.freelist_head;

  if(swap_idx == -1){
    return -1;
  }

  struct swap_page *pg = &(swap_space.pages[swap_idx]);

  swap_space.freelist_head = pg->next_free_index;
  pg->is_used = 1;
  pg->pid = p->pid;
  pg->va = f->va;
  pg->saved_raid_level = raid_level;

  f->proc = 0;
  
  // memmove(pg->data,(char*)f->pa,PGSIZE);
  release(&page_lock);
  raid_write(index,swap_idx);
  acquire(&page_lock);

  p->pages_evicted++;
  p->pages_swapped_out++;
  p->resident_pages--;

  return 0;
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{

  int index;
  uint64 pa = 0;

  acquire(&page_lock);
  index = frame_table.freelist_head;

  if(index != -1){
    frame_table.freelist_head = frame_table.frames[index].next_free_index;
    frame_table.frames[index].is_used = 1;
    frame_table.frames[index].ref_bit = 1;
    release(&page_lock);
    pa = FTIDX2PA(index);
    memset((char*)pa, 5, PGSIZE);
    return (void*)pa;
  }

  int passes = 2;
  while(passes > 0){

    int mxlevel = -1; 
    int mxlevel_index = -1;
    struct proc *p;
    struct frame *f;

    for(int i = 0; i<PAGE_FRAMES; i++){
      index = (frame_table.clockhand + i)%PAGE_FRAMES;
      f = &(frame_table.frames[index]);
      p = frame_table.frames[index].proc;
      if(p != 0){
        pte_t *pte = walk(p->pagetable,f->va,0);
        if((pte!=0) && ((*pte)&(PTE_V)) && ((*pte)&(PTE_U))){

          if(*pte & PTE_A){
            f->ref_bit = 1;
            *pte &= ~PTE_A;
            sfence_vma();
          }
          
          if((f->ref_bit == 0) && (*pte & PTE_X) == 0){
            if(mxlevel < p->level){
              mxlevel = p->level;
              mxlevel_index = index;
            }
          }
        }
      }
    }

    if(mxlevel != -1){
      
      struct frame *f = &(frame_table.frames[mxlevel_index]);
      pte_t *pte = walk(f->proc->pagetable,f->va,0);
      if(pte != 0){
        *pte = (*pte & ~PTE_V) | PTE_S;
        sfence_vma();
      }


      // swap out
      if(swap_out(mxlevel_index) == -1){
        if(pte != 0){
          *pte = (*pte & ~PTE_S) | PTE_V;
          sfence_vma();
        }
        release(&page_lock);
        return 0;
      }

      if(pte != 0){
        *pte = PTE_FLAGS(*pte) | PTE_S;
        sfence_vma();
      }
      
      
      f->is_used = 1;
      f->ref_bit = 1;
      pa = f->pa;
      f->proc = 0;
      f->va = 0;

      frame_table.clockhand = (mxlevel_index + 1) % PAGE_FRAMES;

      release(&page_lock);

      memset((char*)f->pa,5,PGSIZE);
      break;
    }

    passes--;

    if(passes == 0 && mxlevel == -1){
      release(&page_lock);
      return 0;
    }

    for(int i = 0; i < PAGE_FRAMES; i++){
      if(frame_table.frames[i].proc != 0){
         frame_table.frames[i].ref_bit = 0;
      }
    }
  }
  
  return (void*)pa;

}
