#ifndef _SYSCALL_ID_H_
#define _SYSCALL_ID_H_

#define SYSCALL_PUTCHAR 4
#define SYSCALL_GET_PROCESS_ID 1
#define SYSCALL_PROCESS_DESTORY 3
#define SYSCALL_PUT_STRING 5
#define SYSCALL_READDIR 10

#define SYSCALL_SBRK 13 // TODO , this id is wrong

#define SYSCALL_CWD 17
#define SYSCALL_DEV 20
#define SYSCALL_DUP 23
#define SYSCALL_DUP3 24

#define SYSCALL_MKDIRAT 34
#define SYSCALL_UNLINKAT 35
#define SYSCALL_LINKAT 37
#define SYSCALL_UMOUNT 39
#define SYSCALL_MOUNT 40
#define SYSCALL_CHDIR 49

#define SYSCALL_OPEN 55
#define SYSCALL_OPENAT 56
#define SYSCALL_CLOSE 57
#define SYSCALL_PIPE2 59
#define SYSCALL_GET_DIRENT 61
#define SYSCALL_READ 63
#define SYSCALL_WRITE 64

#define SYSCALL_FSTAT 80
#define SYSCALL_EXIT 93
#define SYSCALL_SLEEP_TIME 101

#define SYSCALL_SCHED_YIELD 124
#define SYSCALL_GET_CPU_TIMES 153

#define SYSCALL_GET_TIME 169
#define SYSCALL_GET_PID 172
#define SYSCALL_GET_PARENT_PID 173

#define SYSCALL_BRK 214

#define SYSCALL_UNMAP_MEMORY 215
#define SYSCALL_CLONE 220
#define SYSCALL_EXEC 221
#define SYSCALL_MAP_MEMORY 222
#define SYSCALL_WAIT 260
#endif