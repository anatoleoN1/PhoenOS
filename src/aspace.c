/* PhoenOS - aspace.c : espaces d'adressage des programmes (voir aspace.h). */
#include "aspace.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"

#define DIR_SLOT (ASPACE_BASE >> 22)

static const pte_t *kernel_dir;

void aspace_set_kernel_dir(const pte_t *dir)
{
    kernel_dir = dir;
}

#ifdef __x86_64__
/*
 * perf64 : le programme a SON PML4, SON PDPT et SON premier repertoire (couvre 0-1 Gio) ; ceux du noyau sont copies
 * (les autres repertoires, au-dela de 1 Gio, restent ceux du noyau, partages). Les deux entrees de 2 Mio qui couvrent
 * la zone des programmes (4-8 Mio) pointent vers 2 tables de pages (1024 entrees) propres au programme.
 */
err_t aspace_create(struct aspace *a)
{
    u32 top, l3, l2, t;
    const pte_t *k4;
    const pte_t *k3;
    const pte_t *k2;

    memset(a, 0, sizeof(*a));
    if (!kernel_dir)
        return E_INVAL;
    k4 = kernel_dir;
    k3 = (const pte_t *)(usize)(k4[0] & ~(pte_t)(PAGE_SIZE - 1));
    k2 = (const pte_t *)(usize)(k3[0] & ~(pte_t)(PAGE_SIZE - 1));
    top = pmm_alloc_frame();
    l3 = top ? pmm_alloc_frame() : 0;
    l2 = l3 ? pmm_alloc_frame() : 0;
    t = l2 ? pmm_alloc_contiguous(2) : 0;
    if (!t) {
        if (l2) pmm_free_frame(l2);
        if (l3) pmm_free_frame(l3);
        if (top) pmm_free_frame(top);
        return E_NOMEM;
    }
    memcpy((void *)(usize)top, k4, PAGE_SIZE);
    memcpy((void *)(usize)l3, k3, PAGE_SIZE);
    memcpy((void *)(usize)l2, k2, PAGE_SIZE);
    memset((void *)(usize)t, 0, 2 * PAGE_SIZE);
    a->dir = (pte_t *)(usize)top;
    a->pt = (pte_t *)(usize)t;
    a->pdpt = l3;
    a->pd = l2;
    a->dir[0] = (pte_t)l3 | PAGE_P | PAGE_RW | PAGE_US;
    ((pte_t *)(usize)l3)[0] = (pte_t)l2 | PAGE_P | PAGE_RW | PAGE_US;
    ((pte_t *)(usize)l2)[ASPACE_BASE >> 21] = (pte_t)t | PAGE_P | PAGE_RW | PAGE_US;
    ((pte_t *)(usize)l2)[(ASPACE_BASE >> 21) + 1] = (pte_t)(t + PAGE_SIZE) | PAGE_P | PAGE_RW | PAGE_US;
    return E_OK;
}
#else
err_t aspace_create(struct aspace *a)
{
    u32 d, t;

    memset(a, 0, sizeof(*a));
    if (!kernel_dir)
        return E_INVAL;
    d = pmm_alloc_frame();
    if (!d)
        return E_NOMEM;
    t = pmm_alloc_frame();
    if (!t) {
        pmm_free_frame(d);
        return E_NOMEM;
    }
    a->dir = (u32 *)d;
    a->pt = (u32 *)t;
    memcpy(a->dir, kernel_dir, PAGE_SIZE);
    memset(a->pt, 0, PAGE_SIZE);
    a->dir[DIR_SLOT] = t | PAGE_P | PAGE_RW | PAGE_US;
    return E_OK;
}
#endif

static int zone_ok(u32 lo, u32 hi)
{
    return lo >= ASPACE_BASE && lo < hi && hi <= ASPACE_END;
}

err_t aspace_map(struct aspace *a, u32 lo, u32 hi, int writable)
{
    u32 first, last;

    if (!a->pt || !zone_ok(lo, hi))
        return E_INVAL;
    first = (lo - ASPACE_BASE) >> 12;
    last = (hi - ASPACE_BASE + PAGE_SIZE - 1) >> 12;           /* exclu */
    for (u32 i = first; i < last; i++) {
        u32 f;

        if (a->pt[i] & PAGE_P) {
            if (writable && !(a->pt[i] & ASPACE_SHARED))
                a->pt[i] |= PAGE_RW;
            continue;
        }
        f = pmm_alloc_frame();
        if (!f) {
            for (u32 k = first; k < i; k++)                    /* annule ce que CET appel a mappe */
                if ((a->pt[k] & PAGE_P) && (a->pt[k] & 0x200u)) {
                    pmm_free_frame(a->pt[k] & ~(PAGE_SIZE - 1));
                    a->pt[k] = 0;
                    a->pages--;
                }
            return E_NOMEM;
        }
        memset((void *)(usize)f, 0, PAGE_SIZE);
        a->pt[i] = (pte_t)f | PAGE_P | PAGE_US | (writable ? PAGE_RW : 0) | 0x200u;   /* 0x200 : "nouvelle" (bit libre) */
        a->pages++;
    }
    for (u32 i = first; i < last; i++)
        a->pt[i] &= ~0x200u;
    return E_OK;
}

err_t aspace_map_shared(struct aspace *a, u32 va, u32 frame)
{
    u32 i;

    if (!a->pt || (va & (PAGE_SIZE - 1)) || (frame & (PAGE_SIZE - 1)) || !frame || !zone_ok(va, va + PAGE_SIZE))
        return E_INVAL;
    i = (va - ASPACE_BASE) >> 12;
    if (a->pt[i] & PAGE_P)
        return E_BUSY;
    a->pt[i] = (pte_t)frame | PAGE_P | PAGE_US | ASPACE_SHARED;
    a->pages++;
    return E_OK;
}

/* Rend le cadre d'une entree, sauf s'il est partage (il appartient a la bibliotheque). */
static void release_entry(pte_t e)
{
    if (!(e & ASPACE_SHARED))
        pmm_free_frame((u32)(e & ~(PAGE_SIZE - 1)));
}

err_t aspace_unmap(struct aspace *a, u32 lo, u32 hi)
{
    if (!a->pt || !zone_ok(lo, hi))
        return E_INVAL;
    for (u32 i = (lo - ASPACE_BASE) >> 12; i < (hi - ASPACE_BASE + PAGE_SIZE - 1) >> 12; i++) {
        if (!(a->pt[i] & PAGE_P))
            continue;
        release_entry(a->pt[i]);
        a->pt[i] = 0;
        a->pages--;
    }
    return E_OK;
}

err_t aspace_protect(struct aspace *a, u32 lo, u32 hi, int writable)
{
    if (!a->pt || !zone_ok(lo, hi))
        return E_INVAL;
    for (u32 i = (lo - ASPACE_BASE) >> 12; i < (hi - ASPACE_BASE + PAGE_SIZE - 1) >> 12; i++) {
        if (!(a->pt[i] & PAGE_P))
            continue;
        if (writable && !(a->pt[i] & ASPACE_SHARED))
            a->pt[i] |= PAGE_RW;
        else
            a->pt[i] &= ~PAGE_RW;
    }
    return E_OK;
}

int aspace_check(const struct aspace *a, u32 addr, u32 len, int write)
{
    u32 end = addr + len;

    if (!a->pt || addr < ASPACE_BASE || addr > ASPACE_END || end < addr || end > ASPACE_END)
        return 0;
    if (len == 0)
        return 1;
    for (u32 p = (addr - ASPACE_BASE) >> 12; p <= (end - 1 - ASPACE_BASE) >> 12; p++) {
        pte_t e = a->pt[p];

        if (!(e & PAGE_P) || !(e & PAGE_US) || (write && !(e & PAGE_RW)))
            return 0;
    }
    return 1;
}

void aspace_destroy(struct aspace *a)
{
    if (a->pt) {
        for (u32 i = 0; i < ASPACE_PAGES; i++)
            if (a->pt[i] & PAGE_P)
                release_entry(a->pt[i]);
#ifdef __x86_64__
        pmm_free_frame((u32)(usize)a->pt);
        pmm_free_frame((u32)(usize)a->pt + PAGE_SIZE);
    }
    if (a->pd)
        pmm_free_frame(a->pd);
    if (a->pdpt)
        pmm_free_frame(a->pdpt);
    if (a->dir)
        pmm_free_frame((u32)(usize)a->dir);
#else
        pmm_free_frame((u32)a->pt);
    }
    if (a->dir)
        pmm_free_frame((u32)a->dir);
#endif
    memset(a, 0, sizeof(*a));
}

err_t aspace_put(struct aspace *a, u32 va, const u8 *src, u32 n)
{
    u32 end = va + n;

    if (!a->pt || va < ASPACE_BASE || end < va || end > ASPACE_END)
        return E_FAULT;
    while (va < end) {
        pte_t e = a->pt[(va - ASPACE_BASE) >> 12];
        u32 off = va & (PAGE_SIZE - 1);
        u32 take = PAGE_SIZE - off;

        if (!(e & PAGE_P) || (e & ASPACE_SHARED))
            return E_FAULT;                                    /* page partagee : jamais ecrite de l'exterieur */
        if (take > end - va)
            take = end - va;
        if (src) {
            memcpy((u8 *)(usize)(e & ~(PAGE_SIZE - 1)) + off, src, take);
            src += take;
        } else {
            memset((u8 *)(usize)(e & ~(PAGE_SIZE - 1)) + off, 0, take);
        }
        va += take;
    }
    return E_OK;
}

u32 aspace_cr3(const struct aspace *a)
{
    return (u32)(usize)a->dir;
}
