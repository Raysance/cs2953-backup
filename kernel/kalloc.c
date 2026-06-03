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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

struct {
  struct spinlock lock;
  int count[PHYSTOP / PGSIZE];
} p_ref;

void
kinit()
{
  initlock(&p_ref.lock, "p_ref");
  for (uint64 i=0;i<NCPU;i++)
  {
    char name[8];
    // snprintf(name, sizeof(name), "kmem_%d", i);
    initlock(&kmem[i].lock, name);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  acquire(&p_ref.lock);
  if (p_ref.count[(uint64)pa / PGSIZE] > 1) {
    p_ref.count[(uint64)pa / PGSIZE]--;
    release(&p_ref.lock);
    return;
  }
  p_ref.count[(uint64)pa / PGSIZE] = 0;
  release(&p_ref.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
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
  int id = cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  else {
    for(int i = 0; i < NCPU; i++) {
      if(i == id) continue;
      acquire(&kmem[i].lock);
      if(kmem[i].freelist) {
        r = kmem[i].freelist;
        kmem[i].freelist = r->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }
  release(&kmem[id].lock);
  pop_off();

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&p_ref.lock);
    p_ref.count[(uint64)r / PGSIZE] = 1;
    release(&p_ref.lock);
  }
  return (void*)r;
}

void
kref_inc(uint64 pa)
{
  if((pa % PGSIZE) != 0 || (char*)pa < end || pa >= PHYSTOP)
    panic("kref_inc");
  acquire(&p_ref.lock);
  p_ref.count[pa / PGSIZE]++;
  release(&p_ref.lock);
}

int
kref_get(uint64 pa)
{
  if((pa % PGSIZE) != 0 || (char*)pa < end || pa >= PHYSTOP)
    panic("kref_get");
  int count;
  acquire(&p_ref.lock);
  count = p_ref.count[pa / PGSIZE];
  release(&p_ref.lock);
  return count;
}

uint64
get_freemem(uint64 cpu_id)
{
  struct run *r;
  uint64 count = 0;

  acquire(&kmem[cpu_id].lock);
  r = kmem[cpu_id].freelist;
  while(r){
    count++;
    r = r->next;
  }
  release(&kmem[cpu_id].lock);

  return count * PGSIZE;
}

uint64
total_freemem(void)
{
  uint64 total = 0;
  for(int i = 0; i < NCPU; i++){
    total += get_freemem(i);
  }
  return total;
}
