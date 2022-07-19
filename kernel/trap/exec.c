#include <Elf.h>
#include <Page.h>
#include <Process.h>
#include <Type.h>
#include <fat.h>
#include <file.h>
#include <string.h>
#include <Sysarg.h>
#include <Debug.h>
#include <Trap.h>
#include <Sysfile.h>
#include <exec.h>
#define MAXARG 32  // max exec arguments

ProcessSegmentMap segmentMaps[SEGMENT_MAP_COUNT];

int segmentMapAlloc(ProcessSegmentMap **psm) {
    for (int i = 0; i < SEGMENT_MAP_COUNT; i++) {
        if (!segmentMaps[i].used) {
            segmentMaps[i].used = true;
            *psm = &segmentMaps[i];
            return 0;
        }
    }
    panic("");
    return -1;
}

/*static int loadSegment(u64* pagetable, u64 va, u64 segmentSize, struct dirent* de, u32 fileOffset, u32 binSize) {
    u64 offset = va - DOWN_ALIGN(va, PAGE_SIZE);
    PhysicalPage* page = NULL;
    u64* entry;
    u64 i;
    int r = 0;    
    if (offset > 0) {
        page = pa2page(pageLookup(pagetable, va, &entry));
        if (page == NULL) {
            if (pageAlloc(&page) < 0) {
                printf("load segment error when we need to alloc a page!\n");
            }
            pageInsert(pagetable, va, page2pa(page), PTE_EXECUTE | PTE_READ | PTE_WRITE | PTE_USER);
        }
        r = MIN(binSize, PAGE_SIZE - offset);
        if (eread(de, 0, page2pa(page) + offset, fileOffset, r) != r) {
            panic("load segment error when eread on offset\n");
        }
    }

    for (i = r; i < binSize; i += r) {
        if (pageAlloc(&page) != 0) {
            panic("load segment error when we need to alloc a page 1!\n");
        }
        pageInsert(pagetable, va + i, page2pa(page), PTE_EXECUTE | PTE_READ | PTE_WRITE | PTE_USER);
        r = MIN(PAGE_SIZE, binSize - i);
        if (eread(de, 0, page2pa(page), fileOffset + i, r) != r) {
            panic("load segment error when eread on offset 1\n");
        }
    }

    offset = va + i - DOWN_ALIGN(va + i, PAGE_SIZE);
    if (offset > 0) {
        page = pa2page(pageLookup(pagetable, va + i, &entry));
        if (page == NULL) {
            if (pageAlloc(&page) != 0) {
                panic("load segment error when we need to alloc a page 2!\n");
            }
            pageInsert(pagetable, va + i, page2pa(page), PTE_EXECUTE | PTE_READ | PTE_WRITE | PTE_USER);
        }
        r = MIN(segmentSize - i, PAGE_SIZE - offset);
        bzero((void*)page2pa(page) + offset, r);
    }

    for (i += r; i < segmentSize; i += r) {
        if (pageAlloc(&page) != 0) {
            panic("load segment error when we need to alloc a page 3!\n");
        }
        pageInsert(pagetable, va + i, page2pa(page), PTE_EXECUTE | PTE_READ | PTE_WRITE | PTE_USER);
        r = MIN(PAGE_SIZE, segmentSize - i);
        bzero((void*)page2pa(page), r);
    }
    return 0;
}*/

void appendSegmentMap(Process *p, ProcessSegmentMap *psm) {
    psm->next = p->segmentMapHead;
    p->segmentMapHead = psm;
}

int exec(char* path, char** argv) {
    MSG_PRINT("in exec");    
    STR_PRINT(path);
    // char **s = argv;

    // while(*s)
    //     printf("%s ", *s++);
    // printf("\n");

    /*char *s, *last*/;
    int i, off;
    u64 argc,  sp, ustack[MAXARG], stackbase;
    Ehdr elf;
    struct dirent* de;
    Phdr ph;
    u64 *pagetable = 0;
    Process* p = myproc();
    p->segmentMapHead = NULL;
    u64* oldpagetable = p->pgdir;

    PhysicalPage *page;
    int r = allocPgdir(&page);
    if (r < 0) {
        panic("setup page alloc error\n");
        return r;
    }

    p->heapBottom = USER_HEAP_BOTTOM;// TODO,these code have writen twice
    pagetable = (u64*)page2pa(page);
    extern char trampoline[];
    pageInsert(pagetable, TRAMPOLINE_BASE, (u64)trampoline, 
        PTE_READ | PTE_WRITE | PTE_EXECUTE);
    pageInsert(pagetable, TRAMPOLINE_BASE + PAGE_SIZE, ((u64)trampoline) + PAGE_SIZE, 
        PTE_READ | PTE_WRITE | PTE_EXECUTE);
    
    MSG_PRINT("setup");

    if ((de = ename(AT_FDCWD, path)) == 0) {
        MSG_PRINT("find file error\n");
        return -1;
    }
    elock(de);
    p->execFile = de;

    MSG_PRINT("lock file success");
    // Check ELF header
    if (eread(de, 0, (u64)&elf, 0, sizeof(elf)) != sizeof(elf)){
        MSG_PRINT("read header error\n");
        goto bad;
    }
    if (!is_elf_format((u8*) &elf)){
        MSG_PRINT("not elf format\n");
        goto bad;
    }

    MSG_PRINT("begin map");
    
    // Load program into memory.
    for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
        if (eread(de, 0, (u64)&ph, off, sizeof(ph)) != sizeof(ph)) {
            goto bad;
        }
        if (ph.type != PT_LOAD)
            continue;
        if (ph.memsz < ph.filesz)
            goto bad;
        ProcessSegmentMap *psm;
        if (ph.filesz > 0) {
            if (segmentMapAlloc(&psm) < 0) {
                return -1;
            }
            psm->execFile = de;
            psm->va = ph.vaddr;
            psm->fileOffset = ph.offset;
            psm->len = ph.filesz;
            psm->flag = 1;
            appendSegmentMap(p, psm);
        }
        if (ph.memsz > ph.filesz) {
            if (segmentMapAlloc(&psm) < 0) {
                return -1;
            }
            psm->execFile = NULL;
            psm->va = ph.vaddr + ph.filesz;
            psm->fileOffset = 0;
            psm->len = ph.memsz - ph.filesz;
            psm->flag = 2;
            appendSegmentMap(p, psm);
        }
    }
    
    eunlock(de);
    de = 0;

    p = myproc();
    sp = USER_STACK_TOP;
    stackbase = sp - PGSIZE;
    if (pageAlloc(&page)){
        MSG_PRINT("allock stack error\n");
        goto bad;
    }
    pageInsert(pagetable, stackbase, page2pa(page),
               PTE_EXECUTE | PTE_READ | PTE_WRITE | PTE_USER);

    MSG_PRINT("begin push args\n");
    // Push argument strings, prepare rest of stack in ustack.
    for (argc = 0; argv[argc]; argc++) {
        if (argc >= MAXARG)
            goto bad;
        sp -= strlen(argv[argc]) + 1;
        sp -= sp % 16;  // riscv sp must be 16-byte aligned
        if (sp < stackbase)
            goto bad;
        if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
            goto bad;
        ustack[argc + 1] = sp;
    }
    ustack[0] = argc;
    ustack[argc + 1] = 0;

    int envCount = 2;
    char *envVariable[2] = {"va=a", "vb=b"};
    for (i = 0; i < envCount; i++) {
        sp -= strlen(envVariable[i]) + 1;
        sp -= sp % 16;  // riscv sp must be 16-byte aligned
        if (sp < stackbase)
            goto bad;
        if (copyout(pagetable, sp, envVariable[i], strlen(envVariable[i]) + 1) < 0)
            goto bad;
        ustack[argc + 2 + i] = sp;
    }
    ustack[argc + 2 + envCount] = 0;

    // push the array of argv[] pointers.
    sp -= (argc + 3 + envCount) * sizeof(u64);
    sp -= sp % 16;
    if (sp < stackbase)
        goto bad;
    if (copyout(pagetable, sp, (char*)ustack, (argc + 3 + envCount) * sizeof(u64)) < 0)
        goto bad;

    MSG_PRINT("end push args\n");
    // arguments to user main(argc, argv)
    // argc is returned via the system call return
    // value, which goes in a0.
    getHartTrapFrame()->a1 = sp;
    // printf("sp:  %lx\n", sp);

    // Save program name for debugging.
    /*
    for (last = s = path; *s; s++)
        if (*s == '/')
            last = s + 1;
    safestrcpy(p->name, last, sizeof(p->name));
    */

    HEX_PRINT(getHartTrapFrame()->epc);
    HEX_PRINT(getHartTrapFrame()->sp);
    MSG_PRINT("out exec");
    // Commit to the user image.
    p->pgdir = pagetable;
    getHartTrapFrame()->epc = elf.entry;  // initial program counter = main
    getHartTrapFrame()->sp = sp;          // initial stack pointer
    getHartTrapFrame()->a7 = 255;

    //free old pagetable
    pgdirFree(oldpagetable);
    asm volatile("fence.i");
    MSG_PRINT("out exec");
    return argc;  // this ends up in a0, the first argument to main(argc, argv)

bad:
    if (pagetable)
        pgdirFree((u64*)pagetable);
    if (de) {
        if(holdingsleep(&de->lock))
            eunlock(de);
        eput(de);
    }
    return -1;
}

#define MAXPATH      128   // maximum file path name
u64 sys_exec(void) {
    char path[MAXPATH], *argv[MAXARG];
    int i;
    u64 uargv, uarg;

    if (argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0) {
        return -1;
    }
    memset(argv, 0, sizeof(argv));
    for (i = 0;; i++) {
        if (i >= NELEM(argv)) {
            goto bad;
        }
        if (fetchaddr(uargv + sizeof(u64) * i, (u64*)&uarg) < 0) {
            goto bad;
        }
        if (uarg == 0) {
            argv[i] = 0;
            break;
        }
        PhysicalPage *page;
        if(pageAlloc(&page))
            goto bad;
        argv[i] = (char *)page2pa(page);
        if (argv[i] == 0)
            goto bad;
        if (fetchstr(uarg, argv[i], PGSIZE) < 0)
            goto bad;
    }

    int ret = exec(path, argv);

    for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
        pageFree(pa2page((u64)argv[i]));

    return ret;

bad:
    for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
        pageFree(pa2page((u64)argv[i]));
    return -1;
}
