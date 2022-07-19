#include <Page.h>
#include <Driver.h>
#include <Error.h>
#include <Riscv.h>
#include <Spinlock.h>
#include <string.h>
#include <Process.h>
#include <Sysarg.h>
#include <MemoryConfig.h>


extern PageList freePages;
struct Spinlock pageListLock, cowBufferLock;

inline void pageLockInit(void) {
    initLock(&pageListLock, "pageListLock");
    initLock(&cowBufferLock, "cowBufferLock");
}

int pageRemove(u64 *pgdir, u64 va) {
    u64 *pte;
    u64 pa = pageLookup(pgdir, va, &pte);

    if (!pte) {
        return -1;
    }
    // tlb flush
    if (pa < PHYSICAL_ADDRESS_BASE || pa >= PHYSICAL_MEMORY_TOP) {
        return -1;
    }
    PhysicalPage *page = pa2page(pa);
    page->ref--;
    pageFree(page);
    *pte = 0;
    sfence_vma();
    return 0;
}

int countFreePages() {
    struct PhysicalPage* page;
    int count = 0;
    acquireLock(&pageListLock);
    LIST_FOREACH(page, &freePages, link)
        count++;
    releaseLock(&pageListLock);
    return count;
}

int pageAlloc(PhysicalPage **pp) {
    acquireLock(&pageListLock);
    PhysicalPage *page;
    if ((page = LIST_FIRST(&freePages)) != NULL) {
        *pp = page;
        page->hartId = r_hartid();
        LIST_REMOVE(page, link);
        releaseLock(&pageListLock);
        bzero((void*)page2pa(page), PAGE_SIZE);
        return 0;
    }
    releaseLock(&pageListLock);
    printf("there's no physical page left!\n");
    *pp = NULL;
    return -NO_FREE_MEMORY;
}

static int pageWalk(u64 *pgdir, u64 va, bool create, u64 **pte) {
    int level;
    u64 *addr = pgdir;
    for (level = 2; level > 0; level--) {
        addr += GET_PAGE_TABLE_INDEX(va, level);
        if (!(*addr) & PTE_VALID) {
            if (!create) {
                *pte = NULL;
                return 0;
            }
            PhysicalPage *pp;
            int ret = pageAlloc(&pp);
            if (ret < 0) {
                return ret;
            }
            (*addr) = page2pte(pp) | PTE_VALID;
            pp->ref++;
        }
        addr = (u64*)PTE2PA(*addr);
    }
    *pte = addr + GET_PAGE_TABLE_INDEX(va, 0);
    return 0;
}

u64 pageLookup(u64 *pgdir, u64 va, u64 **pte) {
    u64 *entry;
    pageWalk(pgdir, va, false, &entry);
    if (entry == NULL || !(*entry & PTE_VALID)) {
        return 0;
    }
    if (pte) {
        *pte = entry;
    }
    return PTE2PA(*entry);
}

void pageFree(PhysicalPage *page) {
    if (page->ref > 0) {
        return;
    }
    if (page->ref == 0) {
        acquireLock(&pageListLock);
        LIST_INSERT_HEAD(&freePages, page, link);
        releaseLock(&pageListLock);
    }
}

static void paDecreaseRef(u64 pa) {
    PhysicalPage *page = pa2page(pa);
    page->ref--;
    assert(page->ref==0);
    if (page->ref == 0) {
        acquireLock(&pageListLock);
        LIST_INSERT_HEAD(&freePages, page, link);
        releaseLock(&pageListLock);
    }
}

void pgdirFree(u64* pgdir) {
   // printf("jaoeifherigh   %lx\n", (u64)pgdir);
    u64 i, j, k;
    u64* pageTable;
    for (i = 0; i < PTE2PT; i++) {
        if (!(pgdir[i] & PTE_VALID))
            continue;
        pageTable = pgdir + i;
        u64* pa = (u64*) PTE2PA(*pageTable);
        for (j = 0; j < PTE2PT; j++) {
            if (!(pa[j] & PTE_VALID)) 
                continue;
            pageTable = (u64*) pa + j;
            u64* pa2 = (u64*) PTE2PA(*pageTable);
            for (k = 0; k < PTE2PT; k++) {
                if (!(pa2[k] & PTE_VALID)) 
                    continue;
                u64 addr = (i << 30) | (j << 21) | (k << 12);
                pageRemove(pgdir, addr);
            }
            pa2[j] = 0;
            paDecreaseRef((u64) pa2);
        }
        paDecreaseRef((u64) pa);
    }
    paDecreaseRef((u64) pgdir);
}

int pageInsert(u64 *pgdir, u64 va, u64 pa, u64 perm) {
    u64 *pte;
    va = DOWN_ALIGN(va, PAGE_SIZE);
    pa = DOWN_ALIGN(pa, PAGE_SIZE);
    perm |= PTE_ACCESSED | PTE_DIRTY;
    int ret = pageWalk(pgdir, va, false, &pte);
    if (ret < 0) {
        return ret;
    }
    if (pte != NULL && (*pte & PTE_VALID)) {
        pageRemove(pgdir, va);
    }
    ret = pageWalk(pgdir, va, true, &pte);
    if (ret < 0) {
        return ret;
    }
    *pte = PA2PTE(pa) | perm | PTE_VALID;
    if (pa >= PHYSICAL_ADDRESS_BASE && pa < PHYSICAL_MEMORY_TOP)
        pa2page(pa)->ref++;
    sfence_vma();
    return 0;
}

int allocPgdir(PhysicalPage **page) {
    int r;
    if ((r = pageAlloc(page)) < 0) {
        return r;
    }
    (*page)->ref++;
    return 0;
}

void pageout(u64 *pgdir, u64 badAddr) {
    if (badAddr <= PAGE_SIZE) {
        panic("^^^^^^^^^^TOO LOW^^^^^^^^^^^\n");
    }
    PhysicalPage *page = NULL;
    printf("[Page out]pageout at %lx\n", badAddr);
    if (pageAlloc(&page) < 0) {
        panic("");
    }
    if (pageInsert(pgdir, badAddr, page2pa(page), 
        PTE_USER | PTE_READ | PTE_WRITE | PTE_EXECUTE) < 0) {
        panic("");
    }
    Process *p = myproc();
    u64 pageStart = DOWN_ALIGN(badAddr, PAGE_SIZE);
    u64 pageFinish = pageStart + PAGE_SIZE;
    for (ProcessSegmentMap *psm = p->segmentMapHead; psm; psm = psm->next) {
        u64 start = MAX(psm->va, pageStart);
        u64 finish = MIN(psm->va + psm->len, pageFinish);
        if (start < finish) {
            assert(page != NULL);
            if (psm->flag == 1) {
                eread(psm->execFile, 0, page2pa(page) + PAGE_OFFSET(start), psm->fileOffset + start - psm->va, finish - start);
            } else if (psm->flag == 2) {
                bzero((void*)page2pa(page) + PAGE_OFFSET(start), finish - start);
            } else {
                panic("");
            }
        }
    }
}

u8 cowBuffer[PAGE_SIZE];
void cowHandler(u64 *pgdir, u64 badAddr) {
    u64 pa;
    u64 *pte;
    pa = pageLookup(pgdir, badAddr, &pte);
    // printf("[COW] %x to cow %lx %lx\n", myproc()->id, badAddr, pa);
    if (!(*pte & PTE_COW)) {
        printf("access denied");
        return;
    }
    PhysicalPage *page;
    int r = pageAlloc(&page);
    if (r < 0) {
        panic("cow handler error");
        return;
    }
    acquireLock(&cowBufferLock);
    pa = pageLookup(pgdir, badAddr, &pte);
    bcopy((void *)pa, (void*)cowBuffer, PAGE_SIZE);
    pageInsert(pgdir, badAddr, page2pa(page), (PTE2PERM(*pte) | PTE_WRITE) & ~PTE_COW);
    bcopy((void*) cowBuffer, (void*) page2pa(page), PAGE_SIZE);
    releaseLock(&cowBufferLock);
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
u64 vir2phy(u64* pagetable, u64 va, int* cow) {
    u64* pte;
    u64 pa;

    if (va >= MAXVA)
        return NULL;

    int ret = pageWalk(pagetable, va, 0, &pte);
    if (ret < 0) {
        panic("pageWalk error in vir2phy function!");
    }
    if (pte == 0)
        return NULL;
    if ((*pte & PTE_VALID) == 0)
        return NULL;
    if ((*pte & PTE_USER) == 0)
        return NULL;
    if (cow)
        *cow = (*pte & PTE_COW) > 0;
    pa = PTE2PA(*pte) + (va&0xfff);
    return pa;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(u64* pagetable, char* dst, u64 srcva, u64 len) {
    u64 n, va0, pa0;
    int cow;

    while (len > 0) {
        va0 = DOWN_ALIGN(srcva, PGSIZE);
        pa0 = vir2phy(pagetable, va0, &cow);
        if (pa0 == NULL)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > len)
            n = len;
        memmove(dst, (void*)(pa0 + (srcva - va0)), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(u64* pagetable, u64 dstva, char* src, u64 len) {
    u64 n, va0, pa0;
    int cow;

    while (len > 0) {
        va0 = DOWN_ALIGN(dstva, PGSIZE);
        pa0 = vir2phy(pagetable, va0, &cow);
        if (pa0 == NULL) {
            pageout(pagetable, va0);
        }
        if (cow) {
            // printf("COW?\n");
            cowHandler(pagetable, va0);
        }
        pa0 = vir2phy(pagetable, va0, &cow);
        n = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;
        memmove((void*)(pa0 + (dstva - va0)), src, n);
        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

int growproc(int n) {
    if (myproc()->heapBottom + n >= USER_HEAP_TOP)
        return -1;
    myproc()->heapBottom += n;
    return 0;
}

u64 sys_sbrk(u32 len) {
    u64 addr = myproc()->heapBottom;
    if (growproc(len) < 0)
        return -1;
    return addr;
}