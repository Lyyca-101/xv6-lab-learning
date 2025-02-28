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

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct spinlock ref_lock;
  struct run *freelist;
} kmem;

uint8 refcounts[(PHYSTOP - KERNBASE)>>PGSHIFT];

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kmem.ref_lock,"reflock");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;

  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    refcounts[(uint64)p>>PGSHIFT] = 1;
    kfree(p);
  }
}

void
refcount_increment(void *pa){
  if((uint64)pa == 0x0000000087f60000){
    printf("in %p %d\n",pa,refcounts[(uint64)pa >> PGSHIFT]);
  }
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("refcount_increment: invalid pa");
  acquire(&kmem.ref_lock);
  refcounts[(uint64)pa >> PGSHIFT]++;
  release(&kmem.ref_lock);
}

void
refcount_decrement(void *pa){
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("refcount_decrement: invalid pa");
  acquire(&kmem.ref_lock);
  refcounts[(uint64)pa >> PGSHIFT]--;
  release(&kmem.ref_lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if(refcounts[(uint64)pa >> PGSHIFT] == 0){
    printf("%p\n",pa);
    panic("kfree: try to free a free page");
  }

  r = (struct run*)pa;
  refcount_decrement(pa);


  if(refcounts[(uint64)r>>PGSHIFT] > 0)
    return;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  
  
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    refcounts[(uint64)r>>PGSHIFT] = 1;
  }

  return (void*)r;
}
