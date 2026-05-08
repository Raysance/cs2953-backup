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

#define NBUCKETS 13

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct bucket buckets[NBUCKETS];
} bcache;

static int
hash(uint blockno)
{
  return blockno % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(int i = 0; i < NBUCKETS; i++){
    initlock(&bcache.buckets[i].lock, "bcache_bucket");
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int h = hash(blockno);

  acquire(&bcache.buckets[h].lock);

  // Is the block already cached?
  for(b = bcache.buckets[h].head.next; b != &bcache.buckets[h].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[h].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // We need to find an unused buffer.
  // According to hints, it's OK to serialize this part.
  acquire(&bcache.lock);

  // Check again to avoid race after acquiring bcache.lock
  for(b = bcache.buckets[h].head.next; b != &bcache.buckets[h].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      release(&bcache.buckets[h].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Find an unused buffer from any bucket
  for(int i = 0; i < NBUCKETS; i++){
    int target_idx = (h + i) % NBUCKETS;
    if(target_idx != h) acquire(&bcache.buckets[target_idx].lock);
    for(b = bcache.buckets[target_idx].head.prev; b != &bcache.buckets[target_idx].head; b = b->prev){
      if(b->refcnt == 0){
        // Move buffer to current bucket if it's from another one
        if(target_idx != h){
          b->next->prev = b->prev;
          b->prev->next = b->next;
          
          b->next = bcache.buckets[h].head.next;
          b->prev = &bcache.buckets[h].head;
          bcache.buckets[h].head.next->prev = b;
          bcache.buckets[h].head.next = b;
        }

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        
        if(target_idx != h) release(&bcache.buckets[target_idx].lock);
        release(&bcache.buckets[h].lock);
        release(&bcache.lock);
        
        acquiresleep(&b->lock);
        return b;
      }
    }
    if(target_idx != h) release(&bcache.buckets[target_idx].lock);
  }

  panic("bget: no buffers");
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
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int h = hash(b->blockno);
  acquire(&bcache.buckets[h].lock);
  b->refcnt--;
  release(&bcache.buckets[h].lock);
}

void
bpin(struct buf *b) {
  int h = hash(b->blockno);
  acquire(&bcache.buckets[h].lock);
  b->refcnt++;
  release(&bcache.buckets[h].lock);
}

void
bunpin(struct buf *b) {
  int h = hash(b->blockno);
  acquire(&bcache.buckets[h].lock);
  b->refcnt--;
  release(&bcache.buckets[h].lock);
}


