// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

#define HASH_TBL_SZ 29
#define BUFSZ 100

struct {
  struct spinlock locks[HASH_TBL_SZ];
  struct buf buckets[HASH_TBL_SZ];
  char buf[HASH_TBL_SZ][BUFSZ];
} hashtbl;

static
int hash(uint blockno){
  return blockno % HASH_TBL_SZ;
}

void
binit(void)
{
  struct buf *b;
  // lab-lock concern
  initlock(&bcache.lock, "bcache");

  b = bcache.buf;

  for(int i = 0;i < HASH_TBL_SZ;i++){
    snprintf(hashtbl.buf[i],BUFSZ,"bcache_%d",i);
    initlock(&hashtbl.locks[i],hashtbl.buf[i]);
    //hashtbl.buckets[i].next = &hashtbl.buckets[i];
    //hashtbl.buckets[i].prev = &hashtbl.buckets[i];
    for(int j = 0;j < NBUF/HASH_TBL_SZ ;j++){
      b->next = hashtbl.buckets[i].next;
      hashtbl.buckets[i].next = b;
      b++;
    }
  }

  while(b < bcache.buf + NBUF){
    b->next = hashtbl.buckets[0].next;
    hashtbl.buckets[0].next = b;
    b++;
  }

  // Create linked list of buffers
  //bcache.head.prev = &bcache.head;
  //bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    // head-insertion
    // next: -->,prev: <--
    // head <--> head
    // head <--> A <--> head
    // head <--> B <--> A <--> head
    // finally,a 31-buf circle-double-linked-list will be built
    // head is a pseudo buf(or,more commonly,pseudo node),this trick
    // make it easier to iterate the linked list,you can find it in leetcode
    //b->next = bcache.head.next;
    //b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    b->timestamp = 0;
    //bcache.head.next->prev = b;
    //bcache.head.next = b;
  }
}

// lab-lock: modify binit,bget and brelse to improve parallelism
// maintain the invariant that at most one copy of each block is cached
// scheme: partition the bcache based on hashtable

// out of concern lock conflicts
// 1. When two processes concurrently
// use the same block number. bcachetest test0 "doesn't ever do this"
// 2. When two processes concurrently miss in the cache, and need to
// find an unused block to replace. bcachetest test0 "doesn't ever do this"
// 3. When two processes concurrently use blocks that conflict in whatever
// scheme you use to partition the blocks and locks; for example, if two
// processes use blocks whose block numbers hash to the same slot in a hash
// table. bcachetest test0 MIGHT do this, depending on your design,
// but you should try to adjust your scheme's details to avoid conflicts
// (e.g., change the size of your hash table).


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf *free_b = 0;
  //printf("[bget] in\n");


  int index = hash(blockno);

  
  acquire(&hashtbl.locks[index]);

  //printf("[bget] index: %d blockno: %d\n",index,blockno);
  // Is the block already cached?
  // search in the hash bucket
  b = hashtbl.buckets[index].next;

  while(b){
    //printf("[bget] in 1 blockno: %d\n",blockno);
    if(b->blockno == blockno && b->dev == dev){
/*       if(blockno == 33)
        printf("[bget] blockno 33\n"); */
      b->refcnt++;
      //printf("[bget] out 1\n");
      release(&hashtbl.locks[index]);
      acquiresleep(&b->lock);
      return b;
    }
    if(b->refcnt == 0){
      free_b = b;
    }
    b = b->next;
  }
  /* for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  } */

  // first try in local hash bucket
  if(free_b){
    goto found;
  }

  // avoid circular waiting
  // CPU 0 hold A,acquire B
  // CPU 1 hold B,acquire A,A-->B-->A
  release(&hashtbl.locks[index]);

  acquire(&bcache.lock);
  // Your modified cache does not need to use LRU replacement,
  // but it must be able to use any of the NBUF struct bufs
  // with zero refcnt when it misses in the cache
  // Not cached.
  struct buf *pre_find = 0;//prev of the found free block buffer
  uint holding_bucket = -1;
  for(int i = 0;i < HASH_TBL_SZ;i++){
    struct buf *p;
    uint found_free = 0;
    acquire(&hashtbl.locks[i]);
    p = &hashtbl.buckets[i];

    while(p->next){
      if(p->next->refcnt == 0 && 
        (!pre_find || p->next->timestamp < pre_find->next->timestamp)){
        pre_find = p;
        found_free = 1;
      }
      p = p->next;
    }
    if(!found_free)
      release(&hashtbl.locks[i]);
    else {
      if(holding_bucket != -1)
        release(&hashtbl.locks[holding_bucket]);
      holding_bucket = i;
    }
  }
  if(!pre_find)
    panic("bget: no buffers");

  free_b = pre_find->next;

  if(index != holding_bucket){
    // move free_b from [holding_bucket] to [index]
    pre_find->next = free_b->next;
    release(&hashtbl.locks[holding_bucket]);
    acquire(&hashtbl.locks[index]);
    free_b->next = hashtbl.buckets[index].next;
    hashtbl.buckets[index].next = free_b;
  }
  release(&bcache.lock);

found:
  free_b->dev = dev;
  free_b->blockno = blockno;
  free_b->valid = 0;
  free_b->refcnt = 1;
  release(&hashtbl.locks[index]);
  acquiresleep(&free_b->lock);
  return free_b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
/*     if(blockno == 33)
      printf("[bread] blockno: %d read from disk\n",blockno); */
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
/*   if(b->blockno == 33){
    printf("[bwrite] blockno: %d write to disk\n",b->blockno);
  } */
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int index = hash(b->blockno);

/*   if(b->blockno == 33)
    printf("[brelse] uncached blockno 33\n"); */

  //acquire(&bcache.lock);

  acquire(&hashtbl.locks[index]);

  
  if(b->refcnt < 1)
    panic("brelse: release zero-refcnt buf");
  b->refcnt--;
  if(b->refcnt == 0){
    // update the last used time of this block buffer
    b->timestamp = ticks;
  }
  //if (b->refcnt == 0) {
    // no one is waiting for it.
    // step1 detach the buf from the linked list
/*     b->next->prev = b->prev;
    b->prev->next = b->next; */
    // step2 attach the buf to the 
    // head of the linked list
/*     b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  } */
  release(&hashtbl.locks[index]);
  //release(&bcache.lock);
}

void
bpin(struct buf *b) {
  int index = hash(b->blockno);
  acquire(&hashtbl.locks[index]);
  b->refcnt++;
  release(&hashtbl.locks[index]);
}

void
bunpin(struct buf *b) {
  int index = hash(b->blockno);
  acquire(&hashtbl.locks[index]);
  b->refcnt--;
  release(&hashtbl.locks[index]);
}


