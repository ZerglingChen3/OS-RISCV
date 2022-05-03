#ifndef _KERNEL_SYSCALL_H_
#define _KERNEL_SYSCALL_H_

#include <SyscallId.h>

void syscallPutchar();
void syscallGetProcessId();
void syscallYield();
void syscallProcessDestory();
void syscallFork();
void syscallPutString();
void syscallGetProcessId();
void syscallGetParentProcessId();
void syscallWait();

extern void (*syscallVector[])(void);

#endif