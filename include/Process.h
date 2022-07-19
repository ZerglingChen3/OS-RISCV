#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <Type.h>
#include <Queue.h>
#include <Spinlock.h>
#include <fat.h>
#include <Timer.h>
#include <file.h>
#include <Signal.h>
#include <exec.h>

#define NOFILE 128  //Number of fds that a process can open
#define LOG_PROCESS_NUM 10
#define PROCESS_TOTAL_NUMBER (1 << LOG_PROCESS_NUM)
#define PROCESS_OFFSET(processId) ((processId) & (PROCESS_TOTAL_NUMBER - 1))

#define PROCESS_CREATE_PRIORITY(x, y) { \
    extern u8 binary##x##Start[]; \
    extern int binary##x##Size; \
    processCreatePriority(binary##x##Start, binary##x##Size, y); \
}
typedef struct Trapframe {
    u64 kernelSatp;
    u64 kernelSp;
    u64 trapHandler;
    u64 epc;
    u64 kernelHartId;
    u64 ra;
    u64 sp;
    u64 gp;
    u64 tp;
    u64 t0;
    u64 t1;
    u64 t2;
    u64 s0;
    u64 s1;
    u64 a0;
    u64 a1;
    u64 a2;
    u64 a3;
    u64 a4;
    u64 a5;
    u64 a6;
    u64 a7;
    u64 s2;
    u64 s3;
    u64 s4;
    u64 s5;
    u64 s6;
    u64 s7;
    u64 s8;
    u64 s9;
    u64 s10;
    u64 s11;
    u64 t3;
    u64 t4;
    u64 t5;
    u64 t6;
} Trapframe;

enum ProcessState {
    UNUSED,
    SLEEPING,
    RUNNABLE,
    RUNNING,
    ZOMBIE
};

struct ProcessTime {
    long lastUserTime;
    long lastKernelTime;
};

typedef struct Process {
    Trapframe trapframe;
    struct ProcessTime processTime;
    CpuTimes cpuTime;
    LIST_ENTRY(Process) link;
    u64 awakeTime;
    u64 *pgdir;
    u32 id;
    u32 parentId;
    LIST_ENTRY(Process) scheduleLink;
    u32 priority;
    enum ProcessState state;
    struct Spinlock lock;
    struct dirent *cwd;           // Current directory
    struct dirent *execFile;
    struct File *ofile[NOFILE];
    u64 chan;//wait Object
    u64 currentKernelSp;
    int reason;
    u64 retValue;
    u64 heapBottom;
    SignalSet blocked;
    SignalSet pending;
    ProcessSegmentMap *segmentMapHead;
} Process;

LIST_HEAD(ProcessList, Process);

#define START_FORK 17

u64 getProcessTopSp(Process* p);
SignalAction *getSignalHandler(Process *p);

Process* myproc();
void processInit();
void processCreatePriority(u8* binary, u32 size, u32 priority);
void sleep(void* chan, struct Spinlock* lk);
void wakeup(void* channel);
void yield();
void processFork(u32, u64, u64, u64, u64);
void processDestory(Process* p);
void processFree(Process* p);
int pid2Process(u32 processId, struct Process **process, int checkPerm);
int either_copyout(int user_dst, u64 dst, void* src, u64 len);
int either_copyin(void* dst, int user_src, u64 src, u64 len);
int wait(int, u64);
int setup(Process *p);
void kernelProcessCpuTimeBegin(void);
void kernelProcessCpuTimeEnd(void);
#endif