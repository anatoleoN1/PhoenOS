/*
 * PhoenOS - paging.c
 * Construction et lecture des tables de pages (logique pure, testee sur la machine de
 * developpement). L'activation materielle est dans paging_enable.c.
 */
#include "paging.h"
#include "string.h"

u32 paging_tables_for(u32 map_top)
{
    return (map_top + (4u << 20) - 1) >> 22;
}

static int range_ok(u32 lo, u32 hi, u32 top)
{
    if (lo == 0 && hi == 0)
        return 1;
    return lo < hi && !(lo & (PAGE_SIZE - 1)) && !(hi & (PAGE_SIZE - 1)) && hi <= top;
}

static int in_range(u32 a, u32 lo, u32 hi)
{
    return a >= lo && a < hi;
}

err_t paging_build(u32 *dir, u32 *tables, u32 ntables, const struct paging_cfg *c)
{
    u32 top = c->map_top;

    if (!dir || !tables || top == 0 || (top & (PAGE_SIZE - 1)) || top > PAGING_MAX_TOP)
        return E_INVAL;
    if (((u32)dir | (u32)tables) & (PAGE_SIZE - 1))
        return E_INVAL;
    if (ntables != paging_tables_for(top))
        return E_INVAL;
    if (!range_ok(c->ro_lo, c->ro_hi, top) || !range_ok(c->user_lo, c->user_hi, top) ||
        !range_ok(c->unc_lo, c->unc_hi, top))
        return E_INVAL;
    if (c->ro_hi && c->user_hi && c->ro_lo < c->user_hi && c->user_lo < c->ro_hi)
        return E_INVAL;                                 /* code du noyau accessible au ring 3 : interdit */

    memset(dir, 0, PAGE_SIZE);
    memset(tables, 0, (usize)ntables * PAGE_SIZE);

    for (u32 t = 0; t < ntables; t++)
        dir[t] = ((u32)tables + t * PAGE_SIZE) | PAGE_P | PAGE_RW | PAGE_US;   /* les droits se decident par page */

    for (u32 a = PAGE_SIZE; a < top; a += PAGE_SIZE) {      /* a = 0 : page nulle, absente */
        u32 f = PAGE_P | PAGE_RW;

        if (in_range(a, c->ro_lo, c->ro_hi))
            f &= ~PAGE_RW;
        if (in_range(a, c->user_lo, c->user_hi))
            f |= PAGE_US;
        if (in_range(a, c->unc_lo, c->unc_hi))
            f |= PAGE_PCD | PAGE_PWT;
        tables[a >> 12] = a | f;
    }
    return E_OK;
}

err_t paging_lookup(const u32 *dir, u32 va, u32 *pa, u32 *flags)
{
    u32 pde = dir[va >> 22], pte;
    const u32 *pt;

    if (!(pde & PAGE_P))
        return E_FAULT;
    pt = (const u32 *)(pde & ~(PAGE_SIZE - 1));
    pte = pt[(va >> 12) & 1023];
    if (!(pte & PAGE_P))
        return E_FAULT;
    if (pa)
        *pa = (pte & ~(PAGE_SIZE - 1)) | (va & (PAGE_SIZE - 1));
    if (flags)
        *flags = pte & pde & (PAGE_P | PAGE_RW | PAGE_US);
    return E_OK;
}
