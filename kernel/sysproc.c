#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

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
#ifdef LAB_TRAPS
  backtrace();
#endif
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
sys_pgaccess(void){
  uint64 base;
  int len;
  uint64 user_mask_addr;
  uint64 kernel_mask=0;
  argaddr(0, &base);
  argint(1, &len);
  argaddr(2, &user_mask_addr);
  for (int i=0;i<len;i++){
    uint64 va=base+i*PGSIZE;
    pte_t *pte=walk(myproc()->pagetable,va,0);
    if(pte&&(*pte&PTE_A)){
      kernel_mask|=(1<<i);
      *pte&=~PTE_A;
    }
  }
  if (copyout(myproc()->pagetable,user_mask_addr,(char *)&kernel_mask,sizeof(kernel_mask))<0){
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

uint64
sys_trace(void)
{
  int mask;
  argint(0, &mask);
  myproc()->trace_mask = mask;
  return 0;
}

uint64
sys_sysinfo(void)
{
  uint64 addr;
  struct sysinfo info;
  struct proc *p = myproc();

  argaddr(0, &addr);

  info.freemem = total_freemem();
  info.nproc = get_nproc();

  if(copyout(p->pagetable, addr, (char *)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}
uint64
sys_sigalarm(){
  int ticks;
  uint64 handler;
  struct proc *p = myproc();
  argint(0, &ticks);
  argaddr(1, &handler);
  p->alarm_interval=ticks;
  p->alarm_handler=handler;
  p->alarm_ticks=0;
  return 0;
}
uint64
sys_sigreturn(void){
  struct proc *p = myproc();
  memmove(p->trapframe,p->alarm_trapframe, sizeof(struct trapframe));
  p->alarm_running=0;
  return p->trapframe->a0;
}
