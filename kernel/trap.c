#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "file.h"
#include "fcntl.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

#define LOAD_PAGE_FAULT 13
#define STORE_PAGE_FAULT 15

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } // code to trigger VMA write on page fault
   else if(r_scause() == LOAD_PAGE_FAULT || r_scause() == STORE_PAGE_FAULT) {
    uint64 va = r_stval();
    struct proc *p = myproc();
    pte_t *pte;
    int perm;
    int handled = 0;

    //printf("usertrap: pid: %d, faulting va, 0x%lx\n", p->pid, va);
    for (int idx = 0; idx < p->proc_vma.allocated; idx++) {
      struct vma *vma = &p->proc_vma.vma_array[idx]; 
      if (va >= vma->va_start && va < vma->va_start + vma->length) {

        uint64 va_aligned = PGROUNDDOWN(va);
        
        //check that the page has not been previously unmapped
        uint64 page_index = (va_aligned - vma->va_start) / PGSIZE;
        if(vma->mapped[page_index] != 1){
          printf("usertrap: page already unmmaped\n"); 
          handled = 1;
          setkilled(p);
        }        
        
        // For store instruction, copy on write. If the page is not yet created, create a Read Only Page (even if page is Writable (else clause)
        // if RO page exists, copy on write (if clause).   
        if (r_scause() == STORE_PAGE_FAULT && (vma->prot & PROT_WRITE) && handled !=1){ 
          if(((pte = walk(p->pagetable, va_aligned, 0)) != 0) && (*pte & PTE_V)){ 
            if (vma->flags == MAP_SHARED) { 
              //printf("usertrap: COW: MAP SHARED: VMA 0x%lx; va: 0x%lx; va_aligned 0x%lx\n", vma->va_start, va, va_aligned);
              *pte |= PTE_W;
              handled = 1;
            }
            if (vma->flags == MAP_PRIVATE){
              char *mem = kalloc();
              if (!mem)
                panic("usertrap: mmmap cow kalloc\n");
              //printf("usertrap: COW: MAP PRIVATE -- VMA: 0x%lx; va: 0x%lx; va_aligned 0x%lx, pa %lx\n", vma->va_start, va, va_aligned, (uint64)mem);
              if (copyin(p->pagetable, mem, va_aligned, PGSIZE) < 0)
                panic("usertrap: copyin failed during COW");
              uvmunmap(p->pagetable, va_aligned, 1, 0);
              perm = PTE_W | PTE_U | PTE_V;
              if (vma->prot & PROT_READ) perm |= PTE_R;
              if(mappages(p->pagetable, va_aligned, PGSIZE, (uint64)mem, perm) != 0) 
                panic("mmap: usertrao cow mappages fail\n");
                
              sfence_vma();
              handled = 1;     
            }
          }
        }

        if (r_scause() == STORE_PAGE_FAULT && !(vma->prot & PROT_WRITE)) {
            printf("usertrap: invalid write to read-only VMA at 0x%lx\n", va);
            setkilled(p);
            handled = 1;
        }
        
        if (handled != 1) {
          pte = walk(p->pagetable, va_aligned, 0);
          if (pte && (*pte & PTE_V)) {
            printf("mmap: already mapped: va=0x%lx pte=0x%lx\n", va_aligned, *pte);
            panic("mmap: attempt to double map");
          }
          uint64 file_offset = vma->file_offset + (va_aligned - vma->va_start);
          if (file_offset >= vma->length)
            panic("mmap: offset exceeds mapping length");
          uint file_end = vma->f->ip->size;

          if (walkaddr(p->pagetable, va_aligned) != 0)
            panic("mmap: address already mapped");
            
          char *mem = kalloc();
          if (!mem) 
            panic("mmap: usertrap kalloc fail");

          uint sz = PGSIZE; 
          if (file_end <= file_offset)
            panic("mmap: usertrap 0 sz");
          else if (file_end - file_offset < PGSIZE)
            sz = file_end - file_offset;

          struct inode *ip = vma->f->ip;

          acquiresleep(&ip->lock);
          readi(ip, 0, (uint64)mem, file_offset, sz);
          releasesleep(&ip->lock); 
          if (sz < PGSIZE)
            memset(mem + sz, 0, PGSIZE - sz);

          perm = PTE_U | PTE_V;
          if (vma->prot & PROT_READ) perm |= PTE_R;

          //printf("usertrap: va: 0x%lx; va_aligned: 0x%lx; pa: 0x%lx; file_offset: %lu; inum %d; perm: %d; writable: %ld; size: %d\n", va, va_aligned, (uint64)mem, file_offset, ip->inum, perm, (perm & PTE_W), sz);
          if (mappages(p->pagetable, va_aligned, PGSIZE, (uint64)mem, perm) != 0) 
            panic("mmap: usertrao mappages fail");

          handled = 1;
        }
      }
    }   
    if (!handled) {
      printf("usertrap: added code\n");
      printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
      printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
      setkilled(p);
    }
    }  else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
    }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

