#ifndef __STAT_H
#define __STAT_H

#include "Type.h"

#define T_DIR 1     // Directory
#define T_FILE 2    // File
#define T_DEVICE 3  // Device

#define STAT_MAX_NAME 32

struct stat {
    char name[STAT_MAX_NAME + 1];
    int dev;      // File system's disk device
    short type;   // Type of file
    u64 size;  // Size of file in bytes
};

#endif