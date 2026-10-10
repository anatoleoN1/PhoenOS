/* PhoenOS - aspace.c : espaces d'adressage des programmes (voir aspace.h). */
#include "aspace.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"

#define DIR_SLOT (ASPACE_BASE >> 22)

static const u32 *kernel_dir;

void aspace_set_kernel_dir(const u32 *dir)
{
    kernel_dir = dir;
}

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
            if (writable)
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
        memset((void *)f, 0, PAGE_SIZE);
        a->pt[i] = f | PAGE_P | PAGE_US | (writable ? PAGE_RW : 0) | 0x200u;   /* 0x200 : "nouvelle" (bit libre) */
        a->pages++;
    }
    for (u32 i = first; i < last; i++)
        a->pt[i] &= ~0x200u;
    return E_OK;
}

err_t aspace_unmap(struct aspace *a, u32 lo, u32 hi)
{
    if (!a->pt || !zone_ok(lo, hi))
        return E_INVAL;
    for (u32 i = (lo - ASPACE_BASE) >> 12; i < (hi - ASPACE_BASE + PAGE_SIZE - 1) >> 12; i++) {
        if (!(a->pt[i] & PAGE_P))
            continue;
        pmm_free_frame(a->pt[i] & ~(PAGE_SIZE - 1));
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
        if (writable)
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
        u32 e = a->pt[p];

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
                pmm_free_frame(a->pt[i] & ~(PAGE_SIZE - 1));
        pmm_free_frame((u32)a->pt);
    }
    if (a->dir)
        pmm_free_frame((u32)a->dir);
    memset(a, 0, sizeof(*a));
}

err_t aspace_put(struct aspace *a, u32 va, const u8 *src, u32 n)
{
    u32 end = va + n;

    if (!a->pt || va < ASPACE_BASE || end < va || end > ASPACE_END)
        return E_FAULT;
    while (va < end) {
        u32 e = a->pt[(va - ASPACE_BASE) >> 12];
        u32 off = va & (PAGE_SIZE - 1);
        u32 take = PAGE_SIZE - off;

        if (!(e & PAGE_P))
            return E_FAULT;
        if (take > end - va)
            take = end - va;
        if (src) {
            memcpy((u8 *)(e & ~(PAGE_SIZE - 1)) + off, src, take);
            src += take;
        } else {
            memset((u8 *)(e & ~(PAGE_SIZE - 1)) + off, 0, take);
        }
        va += take;
    }
    return E_OK;
}

u32 aspace_cr3(const struct aspace *a)
{
    return (u32)a->dir;
}
