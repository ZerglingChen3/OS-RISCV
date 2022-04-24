#include <Page.h>
#include <Driver.h>
#include <Error.h>
#include <Riscv.h>
#include <Spinlock.h>

extern PageList freePages;
struct Spinlock pageListLock, memoryLock;

inline void pageLockInit(void) {
    initLock(&pageListLock, "pageListLock");
    initLock(&memoryLock, "memoryLock");
}

static void pageRemove(u64 *pgdir, u64 va) {
    u64 *pte;
    u64 pa = pageLookup(pgdir, va, &pte);

    // tlb flush
    if (pa < PHYSICAL_ADDRESS_BASE || pa >= PHYSICAL_MEMORY_TOP) {
        return;
    }
    PhysicalPage *page = pa2page(pa);
    page->ref--;
    pageFree(page);
    *pte = 0;
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
        releaseLock(&pageListLock);
        *pp = page;
        LIST_REMOVE(page, link);
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
    acquireLock(&memoryLock);

    u64 *entry;
    pageWalk(pgdir, va, false, &entry);
    if (entry == NULL || !(*entry & PTE_VALID)) {
        releaseLock(&memoryLock);
        return 0;
    }
    if (pte) {
        *pte = entry;
    }
    releaseLock(&memoryLock);
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
    if (page->ref == 0) {
        acquireLock(&pageListLock);
        LIST_INSERT_HEAD(&freePages, page, link);
        releaseLock(&pageListLock);
    }
}

void pgdirFree(u64* pgdir) {
    acquireLock(&memoryLock);
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
    releaseLock(&memoryLock);
}

int pageInsert(u64 *pgdir, u64 va, u64 pa, u64 perm) {
    acquireLock(&memoryLock);
    u64 *pte;
    va = DOWN_ALIGN(va, PAGE_SIZE);
    pa = DOWN_ALIGN(pa, PAGE_SIZE);
    perm |= PTE_ACCESSED | PTE_DIRTY;
    int ret = pageWalk(pgdir, va, true, &pte);
    if (ret < 0) {
        releaseLock(&memoryLock);
        return ret;
    }
    if (pte != NULL && (*pte & PTE_VALID)) {
        pageRemove(pgdir, va);
    }
    if (pa >= PHYSICAL_ADDRESS_BASE && pa < PHYSICAL_MEMORY_TOP) {
        pa2page(pa)->ref++;
    }
    *pte = PA2PTE(pa) | perm | PTE_VALID;
    sfence_vma();
    releaseLock(&memoryLock);
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
    printf("pageout at %lx\n", badAddr);
    PhysicalPage *page;
    if (pageAlloc(&page) < 0) {
        panic("");
    }
    if (pageInsert(pgdir, badAddr, page2pa(page), 
        PTE_USER | PTE_READ | PTE_WRITE) < 0) {
        panic("");
    }
}

void cowHandler(u64 *pgdir, u64 badAddr) {
    u64 *pte;
    u64 pa = pageLookup(pgdir, badAddr, &pte);
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
    pageInsert(pgdir, badAddr, page2pa(page), (PTE2PERM(*pte) | PTE_WRITE) & ~PTE_COW);
    bcopy((void*) pa, (void*) page2pa(page), PAGE_SIZE);
}
