#ifndef _PAGE_H_
#define _PAGE_H_

#include <Type.h>
#include <Queue.h>
#include <MemoryConfig.h>
#include <Driver.h>

void pageLockInit(void);

#define DOWN_ALIGN(x, y) (((u64)(x)) & (~((u64)((y) - 1))))
#define UP_ALIGN(x, y) (DOWN_ALIGN((x) - 1, (y)) + (y))
#define PAGE_OFFSET(x) ((x)&(PAGE_SIZE-1))

#define PGSIZE (4096)

struct PhysicalPage;
LIST_HEAD(PageList, PhysicalPage);
typedef struct PageList PageList;
typedef LIST_ENTRY(PhysicalPage) PageListEntry;

// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

typedef struct PhysicalPage {
    PageListEntry link;
    u32 ref;
    int hartId;
} PhysicalPage;

inline u32 page2PPN(PhysicalPage *page) {
    extern PhysicalPage pages[];
    return page - pages;
}

inline PhysicalPage* ppn2page(u32 ppn) {
    extern PhysicalPage pages[];
    return pages + ppn;
}


inline u64 page2pa(PhysicalPage *page) {
    return PHYSICAL_ADDRESS_BASE + (page2PPN(page) << PAGE_SHIFT);
}

inline PhysicalPage* pa2page(u64 pa) {
    if (pa == 0)
        return NULL;
    return ppn2page((pa - PHYSICAL_ADDRESS_BASE) >> PAGE_SHIFT);
}

void bcopy(void *src, void *dst, u32 len);
void bzero(void *start, u32 len);
void memoryInit();
void startPage();

#define PA2PPN(va) ((((u64)(va)) & 0x0fffffff) >> PAGE_SHIFT)
#define PPN2PA(ppn) (((u64)(ppn)) << PAGE_SHIFT)
#define GET_PAGE_TABLE_INDEX(va, level) ((((u64)(va)) >> (PAGE_SHIFT + 9 * level)) & 0x1ff)
#define PTE2PT 512

#define PTE_VALID (1ll << 0)
#define PTE_READ (1ll << 1)
#define PTE_WRITE (1ll << 2)
#define PTE_EXECUTE (1ll << 3)
#define PTE_USER (1ll << 4)
#define PTE_ACCESSED (1ll << 6)
#define PTE_DIRTY (1 << 7)
#define PTE_COW (1ll << 8)
#define PERM_WIDTH 10
#define PTE2PERM(pte) (((u64)(pte)) & ~((1ull << 54) - (1ull << 10)))
#define PTE2PA(pte) (((((u64)(pte)) & ((1ull << 54) - (1ull << 10))) >> PERM_WIDTH) << PAGE_SHIFT)
#define PA2PTE(pa) ((((u64)(pa)) >> PAGE_SHIFT) << PERM_WIDTH)

inline u64 page2pte(PhysicalPage *page) {
    return (page2pa(page) >> PAGE_SHIFT) << PERM_WIDTH;
}

inline PhysicalPage* pte2page(u64 *pte) {
    return pa2page(PTE2PA(*pte));
}

int pageRemove(u64 *pgdir, u64 va);
int countFreePages();
int pageAlloc(PhysicalPage **page);
int pageInsert(u64 *pgdir, u64 va, u64 pa, u64 perm);
void pgdirFree(u64* pgdir);
u64 pageLookup(u64 *pgdir, u64 va, u64 **pte);
int allocPgdir(PhysicalPage **page);
void pageout(u64 *pgdir, u64 badAddr);
void cowHandler(u64 *pgdir, u64 badAddr);
void pageFree(PhysicalPage *page);

u64 vir2phy(u64* pagetable, u64 va, int* cow);
int copyin(u64* pagetable, char* dst, u64 srcva, u64 len);
int copyout(u64* pagetable, u64 dstva, char* src, u64 len);
u64 sys_sbrk(u32);

#define IS_RAM(pa) (pa >= PHYSICAL_ADDRESS_BASE && pa < PHYSICAL_MEMORY_TOP)

#endif