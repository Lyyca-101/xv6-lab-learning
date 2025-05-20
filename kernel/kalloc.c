// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

#define BUFSZ 100
struct {
  struct spinlock lock[NCPU];
  char bufs[NCPU][BUFSZ];
  struct run *freelist[NCPU];
} kmem;

void
kinit()
{
  // lab-lock concern
  for(int i = 0;i < NCPU;i++){
    snprintf(kmem.bufs[i],BUFSZ,"kmem%d",i);
    initlock(&kmem.lock[i],kmem.bufs[i]);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  push_off();
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
  pop_off();
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  push_off();

  int cpu_no = cpuid();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock[cpu_no]);
  r->next = kmem.freelist[cpu_no];
  kmem.freelist[cpu_no] = r;
  release(&kmem.lock[cpu_no]);
  pop_off();

}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();

  int cpu_no = cpuid();

  acquire(&kmem.lock[cpu_no]);
  r = kmem.freelist[cpu_no];
  if(r)
    kmem.freelist[cpu_no] = r->next;
  release(&kmem.lock[cpu_no]);

  if(!r){
    // steal another CPU's page
    for(int i = 0;i < NCPU;i++){
      acquire(&kmem.lock[i]);
      if(kmem.freelist[i]){
        r = kmem.freelist[i];
        kmem.freelist[i] = r->next;
        release(&kmem.lock[i]);
        break;
      }
      release(&kmem.lock[i]);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  
  pop_off();
  
  return (void*)r;
}
