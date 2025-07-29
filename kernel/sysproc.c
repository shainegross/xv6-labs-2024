#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
#include "file.h"

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

#ifdef LAB_MMAP
uint64 
sys_mmap(void)
{
  uint64 addr, len, offset;
  int prot, flags, fd;

  argaddr(0, &addr);
  argaddr(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argaddr(5, &offset);
  
  struct proc *p = myproc();
  int idx; //index of vma array

  // Validate permissions
  if ((prot & ~(PROT_READ | PROT_WRITE)) != 0) {
    printf("sys_mmap: invalid prot flags: %d\n", prot); 
    return -1;
  }

  // Validate flags
  if (flags != MAP_SHARED && flags != MAP_PRIVATE){
    printf("sys_mmap: invalid mapping flags: %d\n", flags);
    return -1;
  }
  
  // Validate file descriptor
  if (fd < 0 || fd >= NOFILE || p->ofile[fd] == 0){
    printf("sys_mmap: invalid fd: %d or null file\n", fd);
    return -1;
  }
  
  // Round length
  len = PGROUNDUP(len);
  if (len == 0){
    printf("sys_mmap: rounded length is zero\n");
    return -1;
  }
    
  if (p->mmap_nextva + len > MMAPTOP) {
    printf("sys_mmap: mapping exceeds MMAPTOP: nextva=0x%lx, len=%lu, MMAPTOP=0x%lx\n", p->mmap_nextva, len, MMAPTOP);
    return -1;  
  }

  idx = p->proc_vma.allocated;
  if (idx >= LEN_VMA_ARRAY) {
    printf("sys_mmap: too many mappings (%d)\n", idx);
    return -1;
  }    

  addr = p->mmap_nextva;
  p->mmap_nextva += len;
  p->proc_vma.allocated++;
  int npages = len / PGSIZE;
  
  struct vma *curr_vma = &p->proc_vma.vma_array[idx];

  struct file *f = filedup(p->ofile[fd]);   
  if (!f->writable && (prot & PROT_WRITE) && (flags == MAP_SHARED)) {
    printf("sys_mmap: can't MAP_SHARED with PROT_WRITE on read-only file\n");
    return -1;
  }

  curr_vma->va_start = addr;
  curr_vma->length = len;
  curr_vma->prot = prot;
  curr_vma->flags = flags;  
  curr_vma->f = f;
  curr_vma->file_offset = offset;
  for (int i = 0; i < LEN_VMA_ARRAY; i++)
    curr_vma->mapped[i] = (i < npages ? 1 : 0);

  //printf("sys_mmap: VMA Allocated at 0x%lx; length: %lu; inum %d; prot %d;offset - %lu\n", curr_vma->va_start, curr_vma->length,f->ip->inum, flags, curr_vma->file_offset);    

  return addr;
}

uint64 
sys_munmap(void)
{
  uint64 va, len; 

  argaddr(0, &va);
  argaddr(1, &len);

  if (munmap(va, len) != 0)
    return -1;

  return 0;
}

// added to make force buffer write, which may be causing bad read. 
uint64
sys_fsync(void)
{
  int fd;
  argint(0, &fd);
  struct file *f = myproc()->ofile[fd];
  if (!f || !f->writable)
    return -1;
  begin_op();
  ilock(f->ip);
  iupdate(f->ip); // force update of inode metadata
  iunlock(f->ip);
  end_op();
  return 0;
}
#endif
