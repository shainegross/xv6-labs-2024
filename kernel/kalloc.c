// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#ifdef LAB_PGTBL
void superinit(void);
#endif // LAB_PGTBL


void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  #ifdef LAB_PGTBL
  superinit();
  #endif

  initlock(&kmem.lock, "kmem");
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

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

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

#ifdef LAB_PGTBL
  void *superpage_base;
  char superpage_in_use[NSUPERPAGES];  // 0 = free, 1 = used
  void superinit(void) {
    superpage_base = (void*) SUPER_PA;  
    if ((uint64)superpage_base % SUPERPGSIZE != 0) {
      panic("superpage_base not aligned");
    }
    memset(superpage_in_use, 0, NSUPERPAGES);
  }
  
  // Allocate one 2MB page of physical memory.
  // Returns a pointer that the kernel can use.
  // Returns 0 if the memory cannot be allocated.void *superalloc() {
  void *
  superalloc(void) {
    for (int i = 0; i < NSUPERPAGES; i++) {
      if (!superpage_in_use[i]) {
        superpage_in_use[i] = 1;
        return (void*)((uint64)superpage_base + i * SUPERPGSIZE);
      }
    }
    return 0;
  }

  void superfree(void *pa) {
    if (((uint64)pa % SUPERPGSIZE) != 0)
      panic("superfree: unaligned address");
    int i = ((uint64)pa - (uint64)superpage_base) / SUPERPGSIZE;
    if (i >= 0 && i < NSUPERPAGES)
      superpage_in_use[i] = 0;
  }
#endif  //LAB_PGTBL
