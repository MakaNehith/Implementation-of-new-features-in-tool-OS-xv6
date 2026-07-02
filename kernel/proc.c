#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct mlfq mlfq[NMLFQ];

struct spinlock mlfq_lock;

struct proc *initproc;

int time_quantum[NMLFQ];

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      // initialisation of syscallcount to 0 for the process slots
      p->syscallcount = 0;

      // MLFQ
      p->level = 0;
      p->ticks_consumed = 0;
      for(int i = 0; i<NMLFQ; i++){
        p->total_ticks[i] = 0;
      }
      p->times_scheduled = 0;
      p->prev_syscallcount = 0;
      p->isboosted = 0;

      // VM STATS
      p->page_faults = 0;
      p->pages_evicted = 0;
      p->pages_swapped_in = 0;
      p->pages_swapped_out = 0;
      p->resident_pages = 0;

      // DISK STATS
      p->disk_reads = 0;
      p->disk_writes = 0;
      p->total_disk_latency = 0;

      p->kstack = KSTACK((int) (p - proc));
  }
}

void
mlfqinit(void)
{
  struct mlfq* q;

  initlock(&mlfq_lock, "mlfq_lock");
  for(q = mlfq; q < &mlfq[NMLFQ]; q++){
    q->head = 0;
    q->tail = 0;
    q->count_of_processes = 0;
  }

  for(int i = 0; i<NMLFQ; i++){
    time_quantum[i] = (int)(2<<i);
  }
}

int
isEmpty(int level)
{
  if(mlfq[level].count_of_processes == 0){
    return 1;
  }
  else{
    return 0;
  }
}

int
isFull(int level)
{
  if(mlfq[level].count_of_processes == NPROC){
    return 1;
  }
  else{
    return 0;
  } 
}

void
enqueue(struct proc* p, int level)
{
  if(isFull(level)){
    panic("MLF queue is full");
  }
  else{
    mlfq[level].queue[mlfq[level].tail] = p;
    mlfq[level].tail = (mlfq[level].tail + 1)%NPROC;
    mlfq[level].count_of_processes++;
  }

}

struct proc* 
dequeue(int level)
{
  struct proc* p = 0;
  if(isEmpty(level)){
    panic("MLF queue is empty");
  }
  else{
    p = mlfq[level].queue[mlfq[level].head];
    mlfq[level].head = (mlfq[level].head + 1)%NPROC;
    mlfq[level].count_of_processes--;
  }

  return p;
}

void
mlfq_priority_boost(void)
{
  acquire(&mlfq_lock);
  struct proc* p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == RUNNING){
      p->isboosted = 1;
    }
    release(&p->lock);
  }
  for(int level = 1; level < NMLFQ; level++){
    while(!isEmpty(level)){
      p = dequeue(level);
      acquire(&p->lock);
      p->total_ticks[p->level] += p->ticks_consumed;
      p->ticks_consumed = 0;
      p->level = 0;
      p->prev_syscallcount = p->syscallcount;
      release(&p->lock);
      enqueue(p,0);
    }
  }
  release(&mlfq_lock);
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  release(&p->lock);

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    acquire(&p->lock);
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    acquire(&p->lock);
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  acquire(&p->lock);

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz, p->pid);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->syscallcount = 0;

  // MLFQ
  p->level = 0;
  p->ticks_consumed = 0;
  for(int i = 0; i<NMLFQ; i++){
    p->total_ticks[i] = 0;
  }
  p->times_scheduled = 0;
  p->prev_syscallcount = 0;
  p->isboosted = 0;

  // VM STATS
  p->page_faults = 0;
  p->pages_evicted = 0;
  p->pages_swapped_in = 0;
  p->pages_swapped_out = 0;
  p->resident_pages = 0;

  // DISK STATS
  p->disk_reads = 0;
  p->disk_writes = 0;
  p->total_disk_latency = 0;

  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0, p->pid);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0, p->pid);
    uvmfree(pagetable, 0, p->pid);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz, int pid)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0, pid);
  uvmunmap(pagetable, TRAPFRAME, 1, 0, pid);
  uvmfree(pagetable, sz, pid);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);

  acquire(&mlfq_lock);
  enqueue(p,0);
  release(&mlfq_lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  release(&np->lock);

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz, np) < 0){
    acquire(&np->lock);
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  acquire(&np->lock);
  
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  np->prev_syscallcount = np->syscallcount;
  release(&np->lock);

  acquire(&mlfq_lock);
  enqueue(np,0);
  release(&mlfq_lock);

  // acquire(&p->lock);
  // if(p->level > 0){
  //   release(&p->lock);
  //   yield();
  // }
  // else{
  //   release(&p->lock);
  // }

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        // if(pp->state == ZOMBIE){
        //   // Found one.
        //   pid = pp->pid;
        //   if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
        //                           sizeof(pp->xstate)) < 0) {
        //     release(&pp->lock);
        //     release(&wait_lock);
        //     return -1;
        //   }
        //   freeproc(pp);
        //   release(&pp->lock);
        //   release(&wait_lock);
        //   return pid;
        // }
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          int child_xstate = pp->xstate; // 1. Save the status to a local variable
          
          freeproc(pp);                  // 2. Clean up the child while holding the locks
          
          release(&pp->lock);            // 3. Drop the child lock
          release(&wait_lock);           // 4. Drop the global wait lock
          
          // 5. Safely copy the data to user space!
          // If this triggers a page fault and SSD swap I/O, it is completely fine 
          // because we are no longer holding any spinlocks.
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&child_xstate, sizeof(child_xstate)) < 0) {
            return -1;
          }
          
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;
    acquire(&mlfq_lock);
    for(int level = 0; level < NMLFQ; level++){
      if(!isEmpty(level)){
        found = 1;
        p = dequeue(level);
        acquire(&p->lock);
        p->state = RUNNING;
        p->times_scheduled++;
        c->proc = p;
        break;
      }
    }
    release(&mlfq_lock);

    if(found == 1){
      swtch(&c->context, &p->context);

      c->proc = 0;
      release(&p->lock);
    }
    else{
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&mlfq_lock);
  acquire(&p->lock);

  p->state = RUNNABLE;

  p->total_ticks[p->level] += p->ticks_consumed;

  // printf("PID %d: level=%d ticks=%d syscalls=%d\n",
  //        p->pid,
  //        p->level,
  //        p->ticks_consumed,
  //        p->syscallcount - p->prev_syscallcount);
  
  if(!(p->isboosted)){
    if((p->syscallcount - p->prev_syscallcount) >= (p->ticks_consumed)){
      // No demotion
    }
    else{
      p->level++;
      if(p->level == NMLFQ){
        p->level--;
      }
    }
  }
  else{
    p->level = 0;
    p->isboosted = 0;
  }
  p->ticks_consumed = 0;
  p->prev_syscallcount = p->syscallcount;

  enqueue(p,p->level);
  release(&mlfq_lock);

  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  acquire(&mlfq_lock);

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        enqueue(p,p->level);
      }
      release(&p->lock);
    }
  }

  release(&mlfq_lock);
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  acquire(&mlfq_lock);
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
        enqueue(p,p->level);
        
      }
      release(&p->lock);
      release(&mlfq_lock);
      return 0;
    }
    release(&p->lock);
  }
  release(&mlfq_lock);
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// B1
int
kgetppid()
{
  int pid;
  struct proc *p = myproc();

  if(p->pid == 1){
    return -1;
  }

  acquire(&wait_lock);

  struct proc *pp = p->parent;  
  pid = pp->pid;

  release(&wait_lock);

  return pid;
}

// B2
int
kgetnumchild(void)
{
  struct proc *p = myproc();
  int count_of_children = 0;

  struct proc *pp;

  acquire(&wait_lock);

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      acquire(&pp->lock);
      if(pp->state == RUNNABLE || pp->state == RUNNING || pp->state == SLEEPING){
        count_of_children++;
      }
      release(&pp->lock);
    }
  }

  release(&wait_lock);

  return count_of_children;
}

// C3
int
kgetchildsyscount(int pid)
{
  struct proc *p = myproc();
  struct proc *pp;
  int childsyscallcount;

  acquire(&wait_lock);

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p && pp->pid == pid){
      acquire(&pp->lock);
      childsyscallcount = pp->syscallcount;
      release(&pp->lock);
      release(&wait_lock);
      goto found;
    }
  }

  release(&wait_lock);
  return -1;

found:
  return childsyscallcount;
}

int
kgetmlfqinfo(int pid, struct mlfqinfo* info)
{
  int found = -1;
  struct proc* p;

  for(p = proc; p<&proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      found = 0;

      info->level = p->level;
      info->times_scheduled = p->times_scheduled;
      info->total_syscalls = p->syscallcount;
      for(int i = 0; i<NMLFQ; i++){
        info->ticks[i] = p->total_ticks[i];
      }
      info->ticks[p->level] += p->ticks_consumed;
      release(&p->lock);
      break;
    }
    else{
      release(&p->lock);
    }
  }

  return found;
}

int
kgetvmstats(int pid, struct vmstats* stats)
{
  int found = -1;
  struct proc* p;

  for(p = proc; p<&proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      found = 0;
      stats->page_faults = p->page_faults;
      stats->pages_evicted = p->pages_evicted;
      stats->pages_swapped_in = p->pages_swapped_in;
      stats->pages_swapped_out = p->pages_swapped_out;
      stats->resident_pages = p->resident_pages;
      stats->disk_reads = p->disk_reads;
      stats->disk_writes = p->disk_writes;
      int disk_req = (p->disk_reads + p->disk_writes);
      if(disk_req == 0){
        stats->avg_disk_latency = 0;
      }
      else{
        stats->avg_disk_latency = p->total_disk_latency/disk_req;
      }
      release(&p->lock);
      break;
    }
    else{
      release(&p->lock);
    }
  }

  return found;
}
