
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "stat.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "fcntl.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int formed = 0;
int formed2 = 0;

int fileNaming(struct proc *myprocess, char *str, int virtAddr);
int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

struct requestQueue
{
  struct spinlock lock;
  struct proc *req[NPROC];
  int head;
  int tail;
  void *chan;
};
struct requestQueue request;
struct requestQueue request2;
struct proc *requestDequeue()
{

  acquire(&request.lock);
  if (request.head == request.tail)
  {
    release(&request.lock);
    return 0;
  }
  else
  {
    struct proc *p = request.req[request.head];
    (request.head)++;
    (request.head) %= NPROC;
    release(&request.lock);
    return p;
  }
}

void requestEnqueue(struct proc *p)
{
  acquire(&request.lock);
  if (request.head != (request.tail + 1) % NPROC)
  {
    request.req[request.tail] = p;
    request.tail = ((request.tail) + 1) % NPROC;
  }
  release(&request.lock);
}

struct proc *requestDequeue2()
{

  acquire(&request2.lock);
  if (request2.head == request2.tail)
  {
    release(&request2.lock);
    return 0;
  }
  else
  {
    struct proc *p = request2.req[request2.head];
    (request2.head)++;
    (request2.head) %= NPROC;
    release(&request2.lock);
    return p;
  }
}

void requestEnqueue2(struct proc *p)
{
  acquire(&request2.lock);
  if (request2.head != (request2.tail + 1) % NPROC)
  {
    request2.req[request2.tail] = p;
    request2.tail = ((request2.tail) + 1) % NPROC;
  }
  release(&request2.lock);
}

void initRequestQueue(struct requestQueue *r)
{
  r->head = 0;
  r->tail = 0;
}

int readSwapFile(int fd, int n, char *p)
{
  // directly taken from inbuilt function in sysfile.c
  struct file *f;
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  return fileread(f, p, n);
}

int closeSwapFile(int fd)
{
  // directly taken from inbuilt function in sysfile.c
  struct file *f;

  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;

  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int writeSwapFile(int fd, char *p, int n)
{
  // directly taken from inbuilt function in sysfile.c
  struct file *f;
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  return filewrite(f, p, n);
}

static struct inode *
createSwapFile(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0)
  {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR)
  {              // Create . and .. entries.
    dp->nlink++; // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if (dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

static int
proc_fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd] == 0)
    {
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int proc_open(char *path, int omode)
{

  int fd;
  struct file *f;
  struct inode *ip;

  begin_op();

  if (omode & O_CREATE)
  {
    ip = createSwapFile(path, T_FILE, 0, 0);
    if (ip == 0)
    {
      end_op();
      return -1;
    }
  }
  else
  {
    if ((ip = namei(path)) == 0)
    {
      end_op();
      return -1;
    }
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY)
    {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if ((f = filealloc()) == 0 || (fd = proc_fdalloc(f)) < 0)
  {
    if (f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

pte_t *findVictim(pde_t *pgdir, int *va)
{
  while (1)
  {
    for (int i = 0; i < NPDENTRIES; i++)
    {
      pte_t *ipgdir = (pte_t *)P2V(PTE_ADDR(pgdir[i]));
      for (int j = 0; j < NPTENTRIES; j++)
      {
        if (!(ipgdir[j] & PTE_P))
        {
          continue;
        }
        if (ipgdir[j] & PTE_R)
        {
          ipgdir[j] ^= PTE_R;
        }
        else
        {
          *va = ((1 << 22) * i) + ((1 << 12) * j);
          pte_t *pte = (pte_t *)P2V(PTE_ADDR(ipgdir[j]));
          memset(&ipgdir[j], 0, sizeof(ipgdir[j]));
          ipgdir[j] = ((ipgdir[j]) ^ (0x080));
          return pte;
        }
      }
    }
  }
  return 0;
}
void convertToString(int a1, char *s)
{
  int a=a1;
  if (a > 0)
  {
    int i = 0;
    while (a > 0)
    {
      s[i] = (a % 10 + '0');
      a /= 10;
      i++;
    }
    for (int j = 0; j < i / 2; j++)
    {
      char c = s[j];
      s[j] = s[i - j];
      s[i - j] = c;
    }
    s[i] = '\0';
  }
  else
  {
    s[0] = '0';
    s[1] = '\0';
  }
}
void processReseter(struct proc* p){
  p->killed = 0;
  p->state = UNUSED;
  p->parent = 0;
  p->name[0] = '*';
}
void swapOutProcessMethod()
{
  acquire(&request.lock);
  while (request.head != request.tail){

    struct proc *p = requestDequeue();
    pde_t *outerPgDir = p->pgdir;
    
    int va;
    pte_t *pte = findVictim(outerPgDir, &va);
    
    
    char c[50];
    int converted = fileNaming(p, c, va);
    if(!converted){
      panic("error in file naming process in swap in");
    }


    int fd1 = proc_open(c, O_RDWR | O_CREATE);

    if(fd1 == -1){
      release(&request.lock);
      panic("proc_open error in swap out process");
    }  
    else{
      int fd2 = writeSwapFile(fd1, (char *)pte, PGSIZE);
      if (fd2 == -1){
        release(&request.lock);
        panic("proc write error in swap out process");
      }
      else{
        int fd3=closeSwapFile(fd1);
        if(fd3 == -1){
          release(&request.lock);
          panic("proc close error in swap out process");
        }
        else{
          kfree((char *)pte);
        }
      }
    }
  }
  release(&request.lock);

  struct proc *p;
  if ((p = myproc()) == 0)
  {
    panic("swap out process");
  }
  formed = 0;
  processReseter(p);
  sched();
}

int fileNaming(struct proc *myprocess, char *str, int virtAddr){
  
  convertToString(myprocess->pid,str);
  int ind=strlen(str);
  str[ind]='_';
  convertToString(virtAddr,str+ind+1);
  
  ind = strlen(str);

  safestrcpy(str+ind,".swp",5);
  
  return (ind > 0);
}

void swapInProcessMethod(){

	acquire(&request2.lock);
	while(request2.head != request2.tail){

		struct proc *p=requestDequeue2();

		int va=PTE_ADDR(p->addr);
		
    char c[50];
    int converted = fileNaming(p, c, va);
    if(!converted){
      panic("error in file naming process in swap in");
    }

    int fd1 = proc_open(c,O_RDONLY);

    if(fd1>=0){
      char *mem=kalloc();
      int fd2 = readSwapFile(fd1,PGSIZE,mem);
      if(fd2 >= 0){
        if(mappages(p->pgdir, (void *)va, PGSIZE, V2P(mem), PTE_W|PTE_U)<0){
          release(&request2.lock);
          panic("mappages");
        }
      }else{
        panic("file read incorrectly");
      }
      wakeup(p);
    }else{
      release(&request2.lock);
      panic("proc_open error in swap_in");
    }
	}

    release(&request2.lock);
    struct proc *p;
	if((p=myproc())==0)
	  panic("swap_in_process");

	formed2=0;
	processReseter(p);
	sched();

}

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&chanLock, "chan");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void)
{
  //formed=0;
  initlock(&request.lock, "request");
  initlock(&request2.lock, "request2");

  acquire(&request.lock);
  initRequestQueue(&request);
  release(&request.lock);

  acquire(&request2.lock);
  initRequestQueue(&request2);
  release(&request2.lock);

  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        //DOC: sleeplock0
    acquire(&ptable.lock); //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void create_kernel_process(const char *name, void (*entrypoint)())
{

  struct proc *p;
  p = allocproc();

  if (p == 0)
  {
    panic("Kernel Process not created");
  }

  if ((p->pgdir = setupkvm()) == 0)
  {
    kfree(p->kstack);
    panic("Page table not setup");
  }
  //Since it is a kernel process, trap frame need not be initialised (only for user soace processes.

  p->sz = PGSIZE;
  p->parent = initproc;
  p->cwd = idup(initproc->cwd);
  //names the process
  safestrcpy(p->name, name, sizeof(p->name));

  acquire(&ptable.lock);
  //entrypoint becomes next instruction to be executed
  p->context->eip = (uint)entrypoint;
  p->state = RUNNABLE;

  release(&ptable.lock);
}
