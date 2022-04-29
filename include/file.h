#ifndef __FILE_H
#define __FILE_H

#include "Type.h"


#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_APPEND  0x004
#define O_CREATE  0x200
#define O_TRUNC   0x400


#define NDEV 4
#define NFILE 64 //Number of fd that all process can open
struct file {
    enum { FD_NONE, FD_PIPE, FD_ENTRY, FD_DEVICE } type;
    int ref;  // reference count
    char readable;
    char writable;
    struct pipe* pipe;  // FD_PIPE
    struct dirent* ep;
    uint off;     // FD_ENTRY
    short major;  // FD_DEVICE
};

#define major(dev) ((dev) >> 16 & 0xFFFF)
#define minor(dev) ((dev)&0xFFFF)
#define mkdev(m, n) ((uint)((m) << 16 | (n)))

// map major device number to device functions.
struct devsw {
    int (*read)(int, u64, int);
    int (*write)(int, u64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1

struct file* filealloc(void);
void fileclose(struct file*);
struct file* filedup(struct file*);
void fileinit(void);
int fileread(struct file*, u64, int n);
int filestat(struct file*, u64 addr);
int filewrite(struct file*, u64, int n);
int dirnext(struct file* f, u64 addr);

#endif