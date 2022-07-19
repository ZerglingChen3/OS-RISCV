#ifndef _EXEC_H_
#define _EXEC_H_

#include <Type.h>
#include <fat.h>
#include <Process.h>

typedef struct Process Process;
typedef struct ProcessSegmentMap {
    struct dirent *execFile;
    u64 va;
    u64 fileOffset;
    u32 len;
    u32 flag;
    struct ProcessSegmentMap *next;
    bool used;
} ProcessSegmentMap;

extern ProcessSegmentMap segmentMaps[];

#define SEGMENT_MAP_COUNT 1024

u64 sys_exec(void);
int segmentMapAlloc(ProcessSegmentMap **psm);
void appendSegmentMap(Process *p, ProcessSegmentMap *psm);

#endif