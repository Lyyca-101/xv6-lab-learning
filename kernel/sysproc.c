#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
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
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
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


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  uint64 pages;
  uint64 user_bitmask;
  unsigned int kern_bitmask = 0;
  int n;
  struct proc *p = myproc();

  argaddr(0,&pages);
  argint(1,&n);
  argaddr(2,&user_bitmask);
  // lab pgtbl: your code here.

  //vmprint(p->pagetable);

  for(int i = 0;i < n;i++){
    pte_t *pte = walk(p->pagetable, pages + i * PGSIZE, 0);
    if((*pte) & PTE_A){
      //printf("%d:%p\n",i,*pte);
      kern_bitmask |= 1 << i;
      (*pte) &= ~PTE_A;
    }
  }

  //printf("%p\n",kern_bitmask);

  if(copyout(p->pagetable,user_bitmask,(char *)(&kern_bitmask),sizeof(kern_bitmask)) < 0){
    return -1;
  }

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
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
