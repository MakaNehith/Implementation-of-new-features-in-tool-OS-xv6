#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// A2
uint64
sys_getpid2(void)
{
  return myproc()->pid;
}

// A1
uint64
sys_hello(void)
{
  printf("Hello from the kernel!\n");
  return 0;
}

// B1
uint64
sys_getppid(void)
{
  return kgetppid();
}

// B2
uint64
sys_getnumchild(void)
{
  return kgetnumchild();
}

// C2
uint64
sys_getsyscount(void)
{
  return myproc()->syscallcount;
}

// C3
uint64
sys_getchildsyscount(void)
{
  int chpid;

  argint(0,&chpid);

  return kgetchildsyscount(chpid);
}

uint64
sys_getlevel(void)
{
  int lvl;
  struct proc* p = myproc();
  acquire(&p->lock);
  lvl = p->level;
  release(&p->lock);

  return lvl;
}

uint64
sys_getmlfqinfo(void)
{
  int pid;
  uint64 uaddr;
  argint(0,&pid);
  argaddr(1,&uaddr);

  struct mlfqinfo kinfo;

  int ret = kgetmlfqinfo(pid, &kinfo);
  if(ret < 0)
    return -1;

  if(copyout(myproc()->pagetable, uaddr, (char*)&kinfo, sizeof(kinfo)) < 0)
    return -1;

  return 0;
}

uint64
sys_getvmstats(void)
{
  int pid;
  uint64 uaddr;
  argint(0,&pid);
  argaddr(1,&uaddr);

  struct vmstats kvmstats;

  int ret = kgetvmstats(pid,&kvmstats);
  if(ret < 0)
    return -1;

  if(copyout(myproc()->pagetable, uaddr, (char*)&kvmstats, sizeof(kvmstats)) < 0)
    return -1;

  return 0;
}

uint64
sys_setdisksched(void)
{
  int policy;
  argint(0,&policy);

  if(policy != 0 && policy != 1){
    return -1;
  }

  disk_policy = policy;
  return 0;
}

uint64
sys_setraidlevel(void)
{
  int level;
  argint(0,&level);

  if(level != 0 && level != 1 && level != 5){
    return -1;
  }

  raid_level = level;
  return 0;
}

uint64
sys_setfaileddisk(void)
{
  int disk;
  argint(0,&disk);
  if(disk < -1 || disk >= NSSD){
    return -1;
  }
  failed_disk = disk;
  return 0;
}