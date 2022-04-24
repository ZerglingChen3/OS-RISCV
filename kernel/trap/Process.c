#include <Process.h>
#include <Page.h>
#include <Error.h>
#include <Elf.h>
#include <Riscv.h>
#include <Trap.h>
#include <Spinlock.h>

Process processes[PROCESS_TOTAL_NUMBER];
static struct ProcessList freeProcesses;
struct ProcessList scheduleList[2];
Process *currentProcess[HART_TOTAL_NUMBER] = {0, 0, 0, 0, 0};

struct Spinlock freeProcessesLock, scheduleListLock, processIdLock;


void processInit() {
    printf("Process init start...\n");
    
    initLock(&freeProcessesLock, "freeProcess");
    initLock(&scheduleListLock, "scheduleList");
    initLock(&processIdLock, "processId");
    
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
        currentProcess[hartId] = NULL;
        yield();
    }
}

void processFree(Process *p) {
    printf("[%lx] free env %lx\n", currentProcess[r_hartid()] ? currentProcess[r_hartid()]->id : 0, p->id);
    pgdirFree(p->pgdir);
    acquireLock(&freeProcessesLock);
    LIST_INSERT_HEAD(&freeProcesses, p, link);
    releaseLock(&freeProcessesLock);
    return;
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
        return INVALID_PROCESS_STATUS;
    }

    if (checkPerm) {
        if (p != currentProcess[hartId] && p->parentId != currentProcess[hartId]->id) {
            *process = NULL;
            return INVALID_PERM;
        }
    }


    *process = p;
    return 0;
}

extern void userVector();
static int setup(Process *p) {
    int r;
    PhysicalPage *page;
    r = allocPgdir(&page);
    if (r < 0) {
        panic("setup page alloc error\n");
        return r;
    }

    p->pgdir = (u64*) page2pa(page);
    
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
    releaseLock(&freeProcessesLock);
    if ((r = setup(p)) < 0) {
        return r;
    }

    p->id = generateProcessId(p);
    p->state = RUNNABLE;
    p->parentId = parentId;
    p->trapframe.kernelSp = getHartKernelTopSp();
    p->trapframe.sp = USER_STACK_TOP;

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

void processRun(Process *p) {
    int hartId = r_hartid();
    Trapframe* trapframe = getHartTrapFrame();
    if (currentProcess[hartId]) {
        bcopy(trapframe, &(currentProcess[hartId]->trapframe), sizeof(Trapframe));
    }
    currentProcess[hartId] = p;
    p->state = RUNNING;
    bcopy(&(currentProcess[hartId]->trapframe), trapframe, sizeof(Trapframe));
    userTrapReturn();
}

void wakeup(void *channel) {
    // todo
}

static int processTimeCount[HART_TOTAL_NUMBER] = {0, 0, 0, 0, 0};
static int processBelongList[HART_TOTAL_NUMBER] = {0, 0, 0, 0, 0};
void yield() {
    int hartId = r_hartid();
    int count = processTimeCount[hartId];
    int point = processBelongList[hartId];
    acquireLock(&scheduleListLock);
    Process* process = currentProcess[hartId];
    if (process && process->state == RUNNING) {
        process->state = RUNNABLE;
    }
    while ((count == 0) || (process == NULL) || (process->state != RUNNABLE)) {
        if (process != NULL)
            LIST_INSERT_TAIL(&scheduleList[point ^ 1], process, scheduleLink);
        if (LIST_EMPTY(&scheduleList[point]))
            point ^= 1;
        if (LIST_EMPTY(&scheduleList[point])) {
            releaseLock(&scheduleListLock);
            acquireLock(&scheduleListLock);
        } else {
            process = LIST_FIRST(&scheduleList[point]);
            LIST_REMOVE(process, scheduleLink);
            count = process->priority;
        }
    }
    releaseLock(&scheduleListLock);
    count--;
    processTimeCount[hartId] = count;
    processBelongList[hartId] = point;
    printf("hartID %d yield process %lx\n", hartId, process->id);
    processRun(process);
}

void processFork() {
    Process *process;
    int hartId = r_hartid();
    int r = processAlloc(&process, currentProcess[hartId]->id);
    if (r < 0) {
        currentProcess[hartId]->trapframe.a0 = r;
        panic("");
        return;
    }
    process->priority = currentProcess[hartId]->priority;
    Trapframe* trapframe = getHartTrapFrame();
    bcopy(trapframe, &process->trapframe, sizeof(Trapframe));
    process->trapframe.a0 = 0;
    
    acquireLock(&scheduleListLock);
    LIST_INSERT_TAIL(&scheduleList[0], process, scheduleLink);
    releaseLock(&scheduleListLock);

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
                if (va == USER_BUFFER_BASE) {
                    PhysicalPage *p;
                    if (pageAlloc(&p) < 0) {
                        panic("Fork alloc page error!\n");
                    }
                    pageInsert(process->pgdir, va, page2pa(p), PTE_USER | PTE_READ | PTE_WRITE);
                } else {
                    if (pa2[k] & PTE_WRITE) {
                        pa2[k] |= PTE_COW;
                        pa2[k] &= ~PTE_WRITE;
                    }
                    pageInsert(process->pgdir, va, PTE2PA(pa2[k]), PTE2PERM(pa2[k]));
                }
            }
        }
    }
    sfence_vma();
    return;
}