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

#define FLIST_CAPACITY (NBUF + 1)
#define HASH_SIZE 13

struct freelist {
  struct spinlock lock;
  struct buf *list[FLIST_CAPACITY];
  int tail;
  int head;  
};

struct hashbucket {
  struct spinlock lock;
  struct buf *head;
};



struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct hashbucket buckets[HASH_SIZE];

  // FREELIST TO TRACK FREES
  struct freelist blist;
} bcache;

static inline int hash_fx(uint dev, uint blockno);

void
binit(void)
{
  struct buf *b;
  struct hashbucket *hb;

  //  initlock(&bcache.lock, "bcache");
  initlock(&bcache.blist.lock, "buffer freelist lock");
  bcache.blist.tail = 0;
  bcache.blist.head = 0;

  for(b = bcache.buf; b < bcache.buf + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    initlock(&b->splock,"buffer cache spinlock");
    bcache.blist.list[bcache.blist.tail] = b;
    bcache.blist.tail = (bcache.blist.tail + 1) % FLIST_CAPACITY;  
    b->refcnt = 0;
  }

  for(hb = bcache.buckets; hb < bcache.buckets + HASH_SIZE; hb++) {
    initlock(&hb->lock, " hashbucket lock");
    hb->head = 0;
  }
}

static inline int 
hash_fx(uint dev, uint blockno) {
  return (dev + blockno) % HASH_SIZE;
}


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct hashbucket *hb;
  
  // Is the block already cached?
  int idx = hash_fx(dev, blockno);
  hb = &bcache.buckets[idx];
  acquire(&hb->lock);
  for (b = bcache.buckets[idx].head; b; b = b->next){
    acquire(&b->splock);      
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&b->splock);
      release(&hb->lock);
      acquiresleep(&b->lock);
      return b; 
    }  
    release(&b->splock);    
  }
  release(&hb->lock);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  acquire(&bcache.blist.lock);
  if (bcache.blist.head == bcache.blist.tail) {
    release(&bcache.blist.lock);
    panic("bget: no buffers");
  }  
 

  b = bcache.blist.list[bcache.blist.head];
  bcache.blist.head = (bcache.blist.head + 1) % FLIST_CAPACITY;
  acquire(&hb->lock);
  acquire(&b->splock);
  b->refcnt = 1;
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->next = 0; 
  if (hb->head == 0) {
     hb->head = b;
  } else {
    b->next = hb->head;
    hb->head = b;
  }

  release(&b->splock);
  release(&hb->lock);
  release(&bcache.blist.lock);

  acquiresleep(&b->lock);  
  return b;
}
  


// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
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

  acquire(&b->splock);
  b->refcnt--;
  int free = (b->refcnt == 0);
  int h = hash_fx(b->dev, b->blockno);
  release(&b->splock);

  struct hashbucket *hb;

  if (free) {
    hb = &bcache.buckets[h];
    
    acquire(&bcache.blist.lock);
    acquire(&hb->lock);
    acquire(&b->splock);
    
    struct buf **pp = &hb->head;
    while (*pp && *pp != b) { 
      pp = &(*pp)->next;
    }
    if (*pp == b)
        *pp = b->next;
    b->next = 0; 

    if (b->refcnt ==0) {
      if ((bcache.blist.tail + 1) % FLIST_CAPACITY == bcache.blist.head) 
        panic("freelist full");
      bcache.blist.list[bcache.blist.tail] = b;
      bcache.blist.tail = (bcache.blist.tail + 1) % FLIST_CAPACITY;
    }
    release(&b->splock);
    release(&hb->lock);
    release(&bcache.blist.lock);
  }  
}



#ifndef LAB_LOCK
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}
#endif

#ifdef LAB_LOCK
void
bpin(struct buf *b) {
  acquire(&b->splock);
  b->refcnt++;
  release(&b->splock);
}

void
bunpin(struct buf *b) {
  acquire(&b->splock);
  b->refcnt--;
  release(&b->splock);
}
#endif
