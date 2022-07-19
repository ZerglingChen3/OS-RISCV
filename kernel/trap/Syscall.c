#include <Syscall.h>
#include <Driver.h>
#include <Process.h>
#include <Type.h>
#include <Page.h>
#include <Riscv.h>
#include <Trap.h>
#include <Spinlock.h>
#include <Sysfile.h>
#include <exec.h>
#include <Signal.h>
#include <Socket.h>
#include <Mmap.h>

void (*syscallVector[])(void) = {
    [SYSCALL_PUTCHAR]           syscallPutchar,
    [SYSCALL_GET_PROCESS_ID]    syscallGetProcessId,
    [SYSCALL_SCHED_YIELD]       syscallYield,
    [SYSCALL_PROCESS_DESTORY]   syscallProcessDestory,
    [SYSCALL_CLONE]             syscallClone,
    [SYSCALL_PUT_STRING]        syscallPutString,
    [SYSCALL_GET_PID]           syscallGetProcessId,
    [SYSCALL_GET_PARENT_PID]    syscallGetParentProcessId,
    [SYSCALL_WAIT]              syscallWait,
    [SYSCALL_DEV]               syscallDevice,
    [SYSCALL_DUP]               syscallDup,
    [SYSCALL_EXIT]              syscallExit,
    [SYSCALL_PIPE2]             syscallPipe,
    [SYSCALL_WRITE]             syscallWrite,
    [SYSCALL_READ]              syscallRead,
    [SYSCALL_CLOSE]             syscallClose,
    [SYSCALL_OPENAT]            syscallOpenAt,
    [SYSCALL_GET_CPU_TIMES]     syscallGetCpuTimes,
    [SYSCALL_GET_TIME_OF_DAY]   syscallGetTime,
    [SYSCALL_SLEEP_TIME]        syscallSleepTime,
    [SYSCALL_DUP3]              syscallDupAndSet,
    [SYSCALL_CHDIR]             syscallChangeDir,
    [SYSCALL_CWD]               syscallGetWorkDir,
    [SYSCALL_MKDIRAT]           syscallMakeDirAt,
    [SYSCALL_BRK]               syscallBrk,
    [SYSCALL_SBRK]              syscallSetBrk,
    [SYSCALL_FSTAT]             syscallGetFileState,
    [SYSCALL_MAP_MEMORY]        syscallMapMemory,
    [SYSCALL_UNMAP_MEMORY]      syscallUnMapMemory,
    [SYSCALL_READDIR]           syscallReadDir,
    [SYSCALL_EXEC]              syscallExec,
    [SYSCALL_GET_DIRENT]        syscallGetDirent,
    [SYSCALL_MOUNT]             syscallMount,
    [SYSCALL_UMOUNT]            syscallUmount,
    [SYSCALL_LINKAT]            syscallLinkAt,
    [SYSCALL_UNLINKAT]          syscallUnlinkAt,
    [SYSCALL_UNAME]             syscallUname,
    [SYSCALL_SET_TID_ADDRESS]   syscallSetTidAddress,
    [SYSCALL_EXIT_GROUP]        syscallExitGroup,
    [SYSCALL_SIGNAL_PROCESS_MASK] syscallSignProccessMask,
    [SYSCALL_SIGNAL_ACTION] syscallSignalAction,
    [SYSCALL_SIGNAL_TIMED_WAIT] syscallSignalTimedWait,
    [SYSCALL_GET_THREAD_ID] syscallGetTheardId,
    [SYSCALL_PROCESS_RESOURSE_LIMIT] syscallProcessResourceLimit,
    [SYSCALL_GET_TIME] syscallGetTime,
    [SYSCALL_LSEEK] syscallLSeek,
    [SYSCALL_IOCONTROL] syscallIOControl,
    [SYSCALL_SOCKET] syscallSocket,
    [SYSCALL_BIND] syscallBind,
    [SYSCALL_GET_SOCKET_NAME] syscallGetSocketName,
    [SYSCALL_SET_SOCKET_OPTION] syscallSetSocketOption,
    [SYSCALL_SEND_TO] syscallSendTo,
    [SYSCALL_RECEIVE_FROM] syscallReceiveFrom,
    [SYSCALL_FCNTL] syscallFcntl,
    [SYSCALL_LISTEN] syscallListen,
    [SYSCALL_CONNECT] syscallConnect,
    [SYSCALL_ACCEPT] syscallAccept,
    [SYSCALL_WRITE_VECTOR] syscallWriteVector
};

extern struct Spinlock printLock;
extern Process *currentProcess[HART_TOTAL_NUMBER];

void syscallPutchar() {
    Trapframe* trapframe = getHartTrapFrame();
    acquireLock(&printLock);
    putchar(trapframe->a0);
    releaseLock(&printLock);
}

void syscallGetProcessId() {
    Trapframe* trapframe = getHartTrapFrame();
    trapframe->a0 = currentProcess[r_hartid()]->id;
}

void syscallGetParentProcessId() {
    Trapframe* trapframe = getHartTrapFrame();
    trapframe->a0 = currentProcess[r_hartid()]->parentId;
}

void syscallProcessDestory() {
    Trapframe* trapframe = getHartTrapFrame();
    u32 processId = trapframe->a0;
    struct Process* process;
    int ret;

    if ((ret = pid2Process(processId, &process, 1)) < 0) {
        trapframe->a0 = ret;
        return;
    }    

    processDestory(process);
    trapframe->a0 = 0;
    return;
}

//todo: support mode
void syscallWait() {
    Trapframe* trapframe = getHartTrapFrame();
    int pid = trapframe->a0;
    u64 addr = trapframe->a1;    
    trapframe->a0 = wait(pid, addr);
}

void syscallYield() {
    kernelProcessCpuTimeEnd();
	yield();
}

void syscallClone() {
    Trapframe *tf = getHartTrapFrame();
    processFork(tf->a0, tf->a1, tf->a2, tf->a3, tf->a4);
}

void syscallExit() {
    Trapframe* trapframe = getHartTrapFrame();
    struct Process* process;
    int ret, ec = trapframe->a0;

    if ((ret = pid2Process(0, &process, 1)) < 0) {
        panic("Process exit error\n");
        return;
    }    

    process->retValue = (ec << 8); //todo
    processDestory(process);
    //will not reach here
    panic("sycall exit error");
}

void syscallPutString() {
    Trapframe* trapframe = getHartTrapFrame();
    int hartId = r_hartid();
    //printf("hart %d, env %lx printf string:\n", hartId, currentProcess[hartId]->id);
    u64 va = trapframe->a0;
    int len = trapframe->a1;
    extern Process *currentProcess[HART_TOTAL_NUMBER];
    u64* pte;
    u64 pa = pageLookup(currentProcess[hartId]->pgdir, va, &pte) + (va & 0xfff);
    if (pa == 0) {
        panic("Syscall put string address error!\nThe virtual address is %x, the length is %x\n", va, len);
    }
    char* start = (char*) pa;
    acquireLock(&printLock);
    while (len--) {
        putchar(*start);
        start++;
    }
    releaseLock(&printLock);
}

void syscallGetCpuTimes() {
    Trapframe *tf = getHartTrapFrame();
    // printf("addr : %lx\n", tf->a0);
    int cow;
    CpuTimes *ct = (CpuTimes*)vir2phy(myproc()->pgdir, tf->a0, &cow);
    if (cow) {
        cowHandler(myproc()->pgdir, tf->a0);
    }
    *ct = myproc()->cpuTime;
    tf->a0 = (r_cycle() & 0x3FFFFFFF);
}

void syscallGetTime() {
    Trapframe *tf = getHartTrapFrame();
    u64 time = r_time();
    TimeSpec ts;
    ts.second = time / 1000000;
    ts.microSecond = time % 1000000;
    copyout(myproc()->pgdir, tf->a0, (char*)&ts, sizeof(TimeSpec));
    tf->a0 = 0;
}

void syscallSleepTime() {
    Trapframe *tf = getHartTrapFrame();
    TimeSpec ts;
    copyin(myproc()->pgdir, (char*)&ts, tf->a0, sizeof(TimeSpec));
    myproc()->awakeTime = r_time() +  ts.second * 1000000 + ts.microSecond;
    kernelProcessCpuTimeEnd();
    yield();
}

void syscallBrk() {
    Trapframe *trapframe = getHartTrapFrame();
    u64 addr = trapframe->a0;
    if (addr == 0) {
        trapframe->a0 = myproc()->heapBottom;
    } else if (addr >= myproc()->heapBottom) {
        trapframe->a0 = (sys_sbrk(addr - myproc()->heapBottom) != -1);
    } else 
        trapframe->a0 = -1;
}

void syscallSetBrk() {
    Trapframe *trapframe = getHartTrapFrame();
    u32 len = trapframe->a0;
    trapframe->a0 = sys_sbrk(len);
}

void syscallMapMemory() {
    panic("");
    Trapframe *trapframe = getHartTrapFrame();
    u64 start = trapframe->a0, len = trapframe->a1, perm = trapframe->a2, flags = trapframe->a3;
    //printf("mmap: %lx %lx %lx %lx\n", start, len, perm, flags);
    bool alloc = (start == 0);
    if (alloc) {
        myproc()->heapBottom = UP_ALIGN(myproc()->heapBottom, 12);
        start = myproc()->heapBottom;
        myproc()->heapBottom = UP_ALIGN(myproc()->heapBottom + len, 12); 
    }
    u64 addr = start, end = start + len;
    start = DOWN_ALIGN(start, 12);
    while (start < end) {
        u64* pte;
        u64 pa = pageLookup(myproc()->pgdir, start, &pte);
        if (pa > 0 && (*pte & PTE_COW)) {
            cowHandler(myproc()->pgdir, start);
        }
        PhysicalPage* page;
        if (pageAlloc(&page) < 0) {        
            trapframe->a0 = -1;
            return ;
        }
        pageInsert(myproc()->pgdir, start, page2pa(page), perm | PTE_USER);
        start += PGSIZE;
    }

    struct File* fd;
    if (flags & MAP_ANONYMOUS) {
        trapframe->a0 = addr;
        return;
    }

    if (argfd(4, 0, &fd)) {
        // printf("fd: %x\n", trapframe->a4);
        trapframe->a0 = -1;
        return ;
    }
    fd->off = trapframe->a5;
    if (fileread(fd, addr, len)) {
        trapframe->a0 = addr;
    } else {
        trapframe->a0 = -1;
    }
}

void syscallUnMapMemory() {
    panic("");
    Trapframe *trapframe = getHartTrapFrame();
    u64 start = trapframe->a0, len = trapframe->a1, end = start + len;
    start = DOWN_ALIGN(start, 12);
    while (start < end) {
        if (pageRemove(myproc()->pgdir, start) < 0) {
            trapframe->a0 = -1;
            return ;
        }
        start += PGSIZE;
    }
    trapframe->a0 = 0;
}

void syscallExec() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = sys_exec();
}

void syscallUname() {
    struct utsname {
	    char sysname[65];
	    char nodename[65];
	    char release[65];
	    char version[65];
	    char machine[65];
	    char domainname[65];
    } uname;
    strncpy(uname.sysname, "my_linux", 65);
    strncpy(uname.nodename, "my_node", 65);
    strncpy(uname.release, "MIPS-OS", 65);
    strncpy(uname.version, "0.1.0", 65);
    strncpy(uname.machine, "Risc-V sifive_u", 65);
    strncpy(uname.domainname, "Beijing", 65);
    Trapframe *tf = getHartTrapFrame();
    copyout(myproc()->pgdir, tf->a0, (char*)&uname, sizeof(struct utsname));
}

void syscallSetTidAddress() {
    Trapframe *tf = getHartTrapFrame();
    copyout(myproc()->pgdir, tf->a0, (char*)(&myproc()->id), sizeof(u64));
    tf->a0 = myproc()->id;
}

void syscallExitGroup() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = 0;
}

void syscallSignProccessMask() {
    Trapframe *tf = getHartTrapFrame();
    u64 how = tf->a0;
    SignalSet set;
    copyin(myproc()->pgdir, (char*)&set, tf->a1, sizeof(SignalSet));
    if (tf->a2 != 0) {
        copyout(myproc()->pgdir, tf->a2, (char*)(&myproc()->blocked), sizeof(SignalSet));
    }
    tf->a0 = signProccessMask(how, &set);
}

void syscallSignalAction() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = doSignalAction(tf->a0, tf->a1, tf->a2);
}

void syscallSignalTimedWait() {
    TimeSpec ts;
    Trapframe *tf = getHartTrapFrame();
    if (tf->a2) {
        copyin(myproc()->pgdir, (char*) &ts, tf->a2, sizeof(TimeSpec));
    }
    SignalSet signalSet;
    copyin(myproc()->pgdir, (char*) &signalSet, tf->a0, sizeof(SignalSet));
    SignalInfo info;
    copyin(myproc()->pgdir, (char*) &info, tf->a0, sizeof(SignalInfo));
    tf->a0 = doSignalTimedWait(&signalSet, &info, tf->a2 ? &ts: 0);
}

void syscallGetTheardId() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = myproc()->id;
}

void syscallProcessResourceLimit() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = 0;
}

void syscallIOControl() {
    Trapframe *tf = getHartTrapFrame();
    printf("fd: %d, cmd: %d, argc: %d\n", tf->a0, tf->a1, tf->a2);
    tf->a0 = 0;
}

void syscallSocket() {
    Trapframe *tf = getHartTrapFrame();
    int domain = tf->a0, type = tf->a1, protocal = tf->a2;
    tf->a0 = createSocket(domain, type, protocal);
}

void syscallBind() {
    Trapframe *tf = getHartTrapFrame();
    assert(tf->a2 == sizeof(SocketAddr));
    SocketAddr sa;
    copyin(myproc()->pgdir, (char*)&sa, tf->a1, tf->a2);
    tf->a0 = bindSocket(tf->a0, &sa);
}

void syscallGetSocketName() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = getSocketName(tf->a0, tf->a1);
}

void syscallSetSocketOption() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = 0;
}

void syscallSendTo() {
    static char buf[PAGE_SIZE];
    Trapframe *tf = getHartTrapFrame();
    assert(tf->a5 == sizeof(SocketAddr));
    SocketAddr sa;
    copyin(myproc()->pgdir, (char *)&sa, tf->a4, sizeof(SocketAddr));
    u32 len = MIN(tf->a2, PAGE_SIZE);
    copyin(myproc()->pgdir, buf, tf->a1, len);
    tf->a0 = sendTo(tf->a0, buf, tf->a2, tf->a3, &sa);
}

void syscallReceiveFrom() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = receiveFrom(tf->a0, tf->a1, tf->a2, tf->a3, tf->a4);
}

void syscallFcntl() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = -1;
}

void syscallListen() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = 0;
}

void syscallConnect() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = 0;
}

void syscallAccept() {
    Trapframe *tf = getHartTrapFrame();
    tf->a0 = 0;
}
