#include <Process.h>
#include <bio.h>
#include <Page.h>
#include <Error.h>
#include <Elf.h>
#include <Riscv.h>
#include <Trap.h>
#include <string.h>
#include <Spinlock.h>
#include <Interrupt.h>
#include <Debug.h>
#include <FileSystem.h>
#include <Sysfile.h>
#include <Signal.h>

Process processes[PROCESS_TOTAL_NUMBER];
static struct ProcessList freeProcesses;
struct ProcessList scheduleList[2];
Process *currentProcess[HART_TOTAL_NUMBER] = {0, 0, 0, 0, 0};

struct Spinlock freeProcessesLock, scheduleListLock, processIdLock, waitLock, currentProcessLock;
Process* myproc() {
    interruptPush();
    int hartId = r_hartid();
    if (currentProcess[hartId] == NULL)
        panic("get current process error");
    Process *ret = currentProcess[hartId];
    interruptPop();
    return ret;
}

extern Trapframe trapframe[];
struct Spinlock freeProcessesLock, scheduleListLock;

u64 getProcessTopSp(Process* p) {
    return KERNEL_PROCESS_SP_TOP - (u64)(p - processes) * 10 * PAGE_SIZE;
}

SignalAction *getSignalHandler(Process *p) {
    return (SignalAction*)(PROCESS_SIGNAL_BASE + (u64)(p - processes) * PAGE_SIZE);
}

void processInit() {
    printf("Process init start...\n");
    
    initLock(&freeProcessesLock, "freeProcess");
    initLock(&scheduleListLock, "scheduleList");
    initLock(&processIdLock, "processId");
    initLock(&waitLock, "waitProcess");
    initLock(&currentProcessLock, "currentProcess");
    
    LIST_INIT(&freeProcesses);
    LIST_INIT(&scheduleList[0]);
    LIST_INIT(&scheduleList[1]);
    int i;
    extern u64 kernelPageDirectory[];
    for (i = PROCESS_TOTAL_NUMBER - 1; i >= 0; i--) {
        processes[i].state = UNUSED;
        processes[i].trapframe.kernelSatp = MAKE_SATP(kernelPageDirectory);
        LIST_INSERT_HEAD(&freeProcesses, &processes[i], link);
    }
    w_sscratch((u64)getHartTrapFrame());
    printf("Process init finish!\n");
}

u32 generateProcessId(Process *p) {
    acquireLock(&processIdLock);
    static u32 nextId = 0;
    u32 processId = (++nextId << (1 + LOG_PROCESS_NUM)) | (u32)(p - processes);
    releaseLock(&processIdLock);
    return processId;
}

void processDestory(Process *p) {
    processFree(p);
    int hartId = r_hartid();
    if (currentProcess[hartId] == p) {
        // acquireLock(&currentProcessLock);
        currentProcess[hartId] = NULL;
        extern char kernelStack[];
        u64 sp = (u64)kernelStack + (hartId + 1) * KERNEL_STACK_SIZE;
        asm volatile("ld sp, 0(%0)" : :"r"(&sp): "memory");
        // releaseLock(&currentProcessLock);
        yield();
    }
}

void processFree(Process *p) {
    // printf("[%lx] free env %lx\n", currentProcess[r_hartid()] ? currentProcess[r_hartid()]->id : 0, p->id);
    pgdirFree(p->pgdir);
    p->state = ZOMBIE; // new
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            struct File* f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }
    kernelProcessCpuTimeEnd();
    if (p->parentId > 0) {
        Process* parentProcess;
        int r = pid2Process(p->parentId, &parentProcess, 0);
        // if (r < 0) {
        //     panic("Can't get parent process, current process is %x, parent is %x\n", p->id, p->parentId);
        // }
        // printf("[Free] process %x wake up %x\n", p->id, parentProcess);
        // The parent process may die before the child process
        if (r == 0) {
            wakeup(parentProcess);
        }
    }
}

int pid2Process(u32 processId, struct Process **process, int checkPerm) {
    struct Process* p;
    int hartId = r_hartid();

    if (processId == 0) {
        *process = currentProcess[hartId];
        return 0;
    }

    p = processes + PROCESS_OFFSET(processId);

    if (p->state == UNUSED || p->id != processId) {
        *process = NULL;
        return -INVALID_PROCESS_STATUS;
    }

    if (checkPerm) {
        if (p != currentProcess[hartId] && p->parentId != currentProcess[hartId]->id) {
            *process = NULL;
            return -INVALID_PERM;
        }
    }

    *process = p;
    return 0;
}

extern FileSystem rootFileSystem;
extern void userVector();
int setup(Process *p) {
    int r;
    PhysicalPage *page;
    r = allocPgdir(&page);
    if (r < 0) {
        panic("setup page alloc error\n");
        return r;
    }
    
    p->pgdir = (u64*) page2pa(page);
    p->chan = 0;
    p->retValue = 0;
    p->state = UNUSED;
    p->parentId = 0;
    p->heapBottom = USER_HEAP_BOTTOM;
    p->awakeTime = 0;
    p->segmentMapHead = NULL;
    p->cwd = &rootFileSystem.root;

    r = pageAlloc(&page);
    extern u64 kernelPageDirectory[];
    pageInsert(kernelPageDirectory, getProcessTopSp(p) - PGSIZE, page2pa(page), PTE_READ | PTE_WRITE | PTE_EXECUTE);
    r = pageAlloc(&page);
    pageInsert(kernelPageDirectory, (u64)getSignalHandler(p), page2pa(page), PTE_READ | PTE_WRITE);
    
    r = pageAlloc(&page);
    pageInsert(p->pgdir, USER_STACK_TOP - PGSIZE, page2pa(page), PTE_USER | PTE_READ | PTE_WRITE | PTE_EXECUTE); // We must alloc the stack

    extern char trampoline[];
    pageInsert(p->pgdir, TRAMPOLINE_BASE, (u64)trampoline, 
        PTE_READ | PTE_WRITE | PTE_EXECUTE);
    pageInsert(p->pgdir, TRAMPOLINE_BASE + PAGE_SIZE, ((u64)trampoline) + PAGE_SIZE, 
        PTE_READ | PTE_WRITE | PTE_EXECUTE);
    return 0;
}

int processAlloc(Process **new, u64 parentId) {
    int r;
    Process *p;
    acquireLock(&freeProcessesLock);
    if (LIST_EMPTY(&freeProcesses)) {
        releaseLock(&freeProcessesLock);
        *new = NULL;
        return -NO_FREE_PROCESS;
    }
    p = LIST_FIRST(&freeProcesses);
    LIST_REMOVE(p, link);
    // printf("[Process Alloc] alloc an process %d, next : %x\n", (u32)(p - processes), (u32)(LIST_FIRST(&freeProcesses) - processes));
    releaseLock(&freeProcessesLock);
    if ((r = setup(p)) < 0) {
        return r;
    }

    p->id = generateProcessId(p);
    p->state = RUNNABLE;
    p->parentId = parentId;
    p->trapframe.kernelSp = getProcessTopSp(p);
    p->trapframe.sp = USER_STACK_TOP - 24; //argc = 0, argv = 0, envp = 0

    *new = p;
    return 0;
}

int codeMapper(u64 va, u32 segmentSize, u8 *binary, u32 binSize, void *userData) {
    Process *process = (Process*)userData;
    PhysicalPage *p = NULL;
    u64 i;
    int r = 0;
    u64 offset = va - DOWN_ALIGN(va, PAGE_SIZE);
    u64* j;

    if (offset > 0) {
        p = pa2page(pageLookup(process->pgdir, va, &j));
        if (p == NULL) {
            if (pageAlloc(&p) < 0) {
                return -1;
            }
            pageInsert(process->pgdir, va, page2pa(p), 
                PTE_EXECUTE | PTE_READ | PTE_WRITE | PTE_USER);
        }
        r = MIN(binSize, PAGE_SIZE - offset);
        bcopy(binary, (void*) page2pa(p) + offset, r);
    }
    for (i = r; i < binSize; i += r) {
        if (pageAlloc(&p) != 0) {
            return -1;
        }
        pageInsert(process->pgdir, va + i, page2pa(p), 
            PTE_EXECUTE | PTE_READ | PTE_WRITE | PTE_USER);
        r = MIN(PAGE_SIZE, binSize - i);
        bcopy(binary + i, (void*) page2pa(p), r);
    }

    offset = va + i - DOWN_ALIGN(va + i, PAGE_SIZE);
    if (offset > 0) {
        p = pa2page(pageLookup(process->pgdir, va + i, &j));
        if (p == NULL) {
            if (pageAlloc(&p) != 0) {
                return -1;
            }
            pageInsert(process->pgdir, va + i, page2pa(p), 
                PTE_EXECUTE | PTE_READ | PTE_WRITE | PTE_USER);
        }
        r = MIN(segmentSize - i, PAGE_SIZE - offset);
        bzero((void*) page2pa(p) + offset, r);
    }
    for (i += r; i < segmentSize; i += r) {
        if (pageAlloc(&p) != 0) {
            return -1;
        }
        pageInsert(process->pgdir, va + i, page2pa(p), 
            PTE_EXECUTE | PTE_READ | PTE_WRITE | PTE_USER);
        r = MIN(PAGE_SIZE, segmentSize - i);
        bzero((void*) page2pa(p), r);
    }
    return 0;
}

void processCreatePriority(u8 *binary, u32 size, u32 priority) {
    Process *p;
    int r = processAlloc(&p, 0);
    if (r < 0) {
        return;
    }
    p->priority = priority;
    u64 entryPoint;
    if (loadElf(binary, size, &entryPoint, p, codeMapper) < 0) {
        panic("process create error\n");
    }
    p->trapframe.epc = entryPoint;

    acquireLock(&scheduleListLock);
    LIST_INSERT_TAIL(&scheduleList[0], p, scheduleLink);
    releaseLock(&scheduleListLock);
}

void sleepRec();
void processRun(Process* p) {
    static volatile int first = 0;
    Trapframe* trapframe = getHartTrapFrame();
    if (currentProcess[r_hartid()]) {
        bcopy(trapframe, &(currentProcess[r_hartid()]->trapframe),
              sizeof(Trapframe));
    }

    p->state = RUNNING;
    if (p->reason == 1) {
        p->reason = 0;
        // acquireLock(&currentProcessLock);
        currentProcess[r_hartid()] = p;
        bcopy(&currentProcess[r_hartid()]->trapframe, trapframe, sizeof(Trapframe));
        asm volatile("ld sp, 0(%0)" : : "r"(&p->currentKernelSp));
        // releaseLock(&currentProcessLock);
        sleepRec();
    } else {
        // acquireLock(&currentProcessLock);
        currentProcess[r_hartid()] = p;
        if (first == 0) {
            // File system initialization must be run in the context of a
            // regular process (e.g., because it calls sleep), and thus cannot
            // be run from main().
            first = 1;
            rootFileSystem.name[0] = 'f';
            rootFileSystem.name[1] = 'a';
            rootFileSystem.name[2] = 't';
            rootFileSystem.name[3] = '3';
            rootFileSystem.name[4] = '2';
            
            rootFileSystem.read = blockRead;
            fatInit(&rootFileSystem);
            initDirentCache();
            printf("init dirent end\n");
            void testfat();
            testfat();

            struct dirent* ep = create(AT_FDCWD, "/dev", T_DIR, O_RDONLY);
            eunlock(ep);
            eput(ep);
            ep = create(AT_FDCWD, "/dev/vda2", T_DIR, O_RDONLY);
            ep->head = &rootFileSystem;            
            eunlock(ep);
        }
        bcopy(&(currentProcess[r_hartid()]->trapframe), trapframe, sizeof(Trapframe));
        u64 sp = getHartKernelTopSp(p);
        asm volatile("ld sp, 0(%0)" : :"r"(&sp): "memory");
        // releaseLock(&currentProcessLock);
        userTrapReturn();
    }
}

//因为与xv6的调度架构不同，所以目前不保证这个实现是正确且无死锁的
// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleepSave();
void sleep(void* chan, struct Spinlock* lk) {//wait()
    struct Process* p = myproc();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    kernelProcessCpuTimeEnd();
    acquireLock(&p->lock);  // DOC: sleeplock1
    releaseLock(lk);

    //这里不将state改成SLEEPING，所以进程会被不停地调度,进程状态始终是RUNNABLE
    // Go to sleep.
    p->chan = (u64)chan;
    p->state = SLEEPING;
    p->reason = 1;
    releaseLock(&p->lock);

	asm volatile("sd sp, 0(%0)" : :"r"(&p->currentKernelSp));

    sleepSave();

    // // Tidy up.
    acquireLock(&p->lock);  // DOC: sleeplock1
    p->chan = 0;
    releaseLock(&p->lock);

    kernelProcessCpuTimeBegin();

    // printf("%x\n", x);

    // Reacquire original lock.
    acquireLock(lk);
}

static inline void updateAncestorsCpuTime(Process *p) {
    Process *pp = p;
    while (pp->parentId > 0 && pid2Process(pp->parentId, &pp, false) >= 0) {
        pp->cpuTime.deadChildrenKernel += p->cpuTime.kernel;
        pp->cpuTime.deadChildrenUser += p->cpuTime.user;
    }
}

int wait(int targetProcessId, u64 addr) {
    Process* p = myproc();
    int haveChildProcess, pid;

    acquireLock(&waitLock);

    while (true) {
        haveChildProcess = 0;
        for (int i = 0; i < PROCESS_TOTAL_NUMBER; ++i) {
            Process* np = &processes[i];
            acquireLock(&np->lock);
            if (np->parentId == p->id) {
                haveChildProcess = 1;
                if ((targetProcessId == -1 || np->id == targetProcessId) && np->state == ZOMBIE) {
                    pid = np->id;
                    if (addr != 0 && copyout(p->pgdir, addr, (char *)&np->retValue, sizeof(np->retValue)) < 0) {
                        releaseLock(&np->lock);
                        releaseLock(&waitLock);
                        return -1;
                    }
                    acquireLock(&freeProcessesLock);
                    updateAncestorsCpuTime(np);
                    np->state = UNUSED;
                    LIST_INSERT_HEAD(&freeProcesses, np, link); //test pipe
                    // printf("[Process Free] Free an process %d\n", (u32)(np - processes));
                    releaseLock(&freeProcessesLock);
                    releaseLock(&np->lock);
                    releaseLock(&waitLock);
                    return pid;
                }
            }
            releaseLock(&np->lock);
        }

        if (!haveChildProcess) {
            releaseLock(&waitLock);
            return -1;
        }

        // printf("[WAIT]porcess id %x wait for %x\n", p->id, p);
        sleep(p, &waitLock);
    }
}

void wakeup(void* channel) {
    for (int i = 0; i < PROCESS_TOTAL_NUMBER; ++i) {
        if (&processes[i] != myproc()) {
            acquireLock(&processes[i].lock);
            // printf("%d %x %x\n", i, processes[i].state, processes[i].chan);
            if (processes[i].state == SLEEPING &&
                processes[i].chan == (u64)channel) {
                processes[i].state = RUNNABLE;
                // printf("[wake up]%x\n", processes[i].id);
            }
            releaseLock(&processes[i].lock);
        }
    }
}

static int processTimeCount[HART_TOTAL_NUMBER] = {0, 0, 0, 0, 0};
static int processBelongList[HART_TOTAL_NUMBER] = {0, 0, 0, 0, 0};
// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, u64 dst, void* src, u64 len) {
    if (user_dst) {
        struct Process* p = myproc();//because only this branch uses p->pgdir, so it need call myproc
        return copyout(p->pgdir, dst, src, len);
    } else {
        memmove((char*)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void* dst, int user_src, u64 src, u64 len) {
    if (user_src) {
        struct Process* p = myproc();//because only this branch uses p->pgdir, so it need call myproc
        return copyin(p->pgdir, dst, src, len);
    } else {
        memmove(dst, (char*)src, len);
        return 0;
    }
}

void yield() {
    int hartId = r_hartid();
    int count = processTimeCount[hartId];
    int point = processBelongList[hartId];
    Process* process = currentProcess[hartId];
    acquireLock(&scheduleListLock);
    if (process && process->state == RUNNING) {
        if(process->reason==1){
            bcopy(getHartTrapFrame(), &process->trapframe, sizeof(Trapframe));
        }
        process->state = RUNNABLE;
    }
    while ((count == 0) || !process || (process->state != RUNNABLE) || process->awakeTime > r_time()) {
        if (process)
            LIST_INSERT_TAIL(&scheduleList[point ^ 1], process, scheduleLink);
        if (LIST_EMPTY(&scheduleList[point]))
            point ^= 1;
        // printf("[POS1]now hart id %x: %x\n", hartId, process);
        if (!(LIST_EMPTY(&scheduleList[point]))) {
            process = LIST_FIRST(&scheduleList[point]);
            LIST_REMOVE(process, scheduleLink);
            count = 1;
        }
        releaseLock(&scheduleListLock);
        acquireLock(&scheduleListLock);
    }
    releaseLock(&scheduleListLock);
    count--;
    processTimeCount[hartId] = count;
    processBelongList[hartId] = point;
    // printf("hartID %d yield process %lx\n", hartId, process->id);
    if (process->awakeTime > 0) {
        getHartTrapFrame()->a0 = 0;
        process->awakeTime = 0;
    }
    processRun(process);
}

void processFork(u32 flags, u64 stackVa, u64 parentThreadId, u64 tls, u64 childThreadId) {
    if (flags != START_FORK) {
        currentProcess[r_hartid()]->trapframe.a0 = -1;
        return;
    }
    Process *process;
    int hartId = r_hartid();
    int r = processAlloc(&process, currentProcess[hartId]->id);
    process->cwd = myproc()->cwd; //when we fork, we should keep cwd
    for (ProcessSegmentMap *psm = myproc()->segmentMapHead; psm; psm = psm->next) {
        ProcessSegmentMap *newPsm;
        if (segmentMapAlloc(&newPsm) < 0) {
            panic("");
        }
        *newPsm = *psm;
        appendSegmentMap(process, newPsm);
    }
    if (r < 0) {
        currentProcess[hartId]->trapframe.a0 = r;
        return;
    }

    for (int i = 0; i < NOFILE; i++)
        if (myproc()->ofile[i])
            process->ofile[i] = filedup(myproc()->ofile[i]);

    process->priority = currentProcess[hartId]->priority;
    Trapframe* trapframe = getHartTrapFrame();
    bcopy(trapframe, &process->trapframe, sizeof(Trapframe));
    process->trapframe.a0 = 0;
    if (stackVa != 0) {
        process->trapframe.sp = stackVa;
    }
    if (parentThreadId != NULL) {
        copyout(currentProcess[hartId]->pgdir, parentThreadId, (char*) &currentProcess[hartId]->id, sizeof(u32));
    }
    if (childThreadId != NULL) {
        copyout(currentProcess[hartId]->pgdir, childThreadId, (char*) &process->id, sizeof(u32));
    }

    trapframe->a0 = process->id;
    u64 i, j, k;
    for (i = 0; i < 512; i++) {
        if (!(currentProcess[hartId]->pgdir[i] & PTE_VALID)) {
            continue;
        }
        u64 *pa = (u64*) PTE2PA(currentProcess[hartId]->pgdir[i]);
        for (j = 0; j < 512; j++) {
            if (!(pa[j] & PTE_VALID)) {
                continue;
            }
            u64 *pa2 = (u64*) PTE2PA(pa[j]);
            for (k = 0; k < 512; k++) {
                if (!(pa2[k] & PTE_VALID)) {
                    continue;
                }
                u64 va = (i << 30) + (j << 21) + (k << 12);
                if (va == TRAMPOLINE_BASE || va == TRAMPOLINE_BASE + PAGE_SIZE) {
                    continue;
                }
                if (pa2[k] & PTE_WRITE) {
                    pa2[k] |= PTE_COW;
                    pa2[k] &= ~PTE_WRITE;
                } 
                pageInsert(process->pgdir, va, PTE2PA(pa2[k]), PTE2PERM(pa2[k]));
            }
        }
    }

    acquireLock(&scheduleListLock);
    LIST_INSERT_TAIL(&scheduleList[0], process, scheduleLink);
    releaseLock(&scheduleListLock);

    return;
}

void kernelProcessCpuTimeBegin() {
    //printf("enter kernel %s %d\n", __FILE__, __LINE__);
    Process *p = myproc();
    long currentTime = r_time();
    p->cpuTime.kernel += currentTime - p->processTime.lastKernelTime;
}

void kernelProcessCpuTimeEnd() {
    Process *p = myproc();
    p->processTime.lastKernelTime = r_time();
}
