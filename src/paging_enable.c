/*
 * PhoenOS - paging_enable.c
 * Alloue les tables, les remplit (paging.c) et active la pagination (CR3, CR0.PG, CR0.WP).
 */
#include "paging.h"
#include "pmm.h"
#include "user.h"
#include "aspace.h"
#include "kprintf.h"
#include "string.h"

extern char kernel_start[];
extern char kernel_ro_end[];                    /* fin de .text + .rodata (linker.ld) */

static int enabled;
static u32 table_bytes;
static u32 mapped_top;
static u32 kernel_cr3;

err_t paging_init(void)
{
    struct paging_cfg cfg;
    u32 top = pmm_top(), ntables, frames, base;
    err_t e;

    if (top > PAGING_MAX_TOP) {
        pmm_limit(PAGING_MAX_TOP);              /* au-dela : jamais alloue au noyau */
        top = PAGING_MAX_TOP;
    }
    top &= ~(PAGE_SIZE - 1);
    if (top == 0)
        return E_NOMEM;

    memset(&cfg, 0, sizeof cfg);
    cfg.map_top = top;
    cfg.ro_lo = (u32)kernel_start;
    cfg.ro_hi = (u32)kernel_ro_end;
    cfg.unc_lo = 0xA0000;                       /* memoire video : sans cache */
    cfg.unc_hi = 0xC0000;

    ntables = paging_tables_for(top);
    frames = 1 + ntables;
    base = pmm_alloc_contiguous(frames);
    if (!base)
        return E_NOMEM;

    e = paging_build((u32 *)base, (u32 *)(base + PAGE_SIZE), ntables, &cfg);
    if (e != E_OK) {
        for (u32 i = 0; i < frames; i++)
            pmm_free_frame(base + i * PAGE_SIZE);
        return e;
    }

    /* La zone des programmes (4-8 Mio) reste mappee dans le noyau (reservee au noyau, U/S = 0) :
     * chaque programme la remplace par SA table de pages (aspace.c). */

    __asm__ volatile (
        "movl %0, %%cr3\n\t"
        "movl %%cr0, %%eax\n\t"
        "orl  $0x80010000, %%eax\n\t"           /* PG (31) + WP (16) : le noyau respecte aussi la lecture seule */
        "movl %%eax, %%cr0\n\t"
        "jmp  1f\n1:\n\t"
        : : "r"(base) : "eax", "memory");

    enabled = 1;
    kernel_cr3 = base;
    aspace_set_kernel_dir((const u32 *)base);
    table_bytes = frames * PAGE_SIZE;
    mapped_top = top;
    return E_OK;
}

int paging_enabled(void)      { return enabled; }
u32 paging_table_bytes(void)  { return table_bytes; }
u32 paging_mapped_top(void)   { return mapped_top; }

void aspace_activate(const struct aspace *a)
{
    u32 cr3 = aspace_cr3(a);

    __asm__ volatile ("movl %0, %%cr3" : : "r"(cr3) : "memory");
}

void aspace_activate_kernel(void)
{
    u32 cr3 = kernel_cr3;

    __asm__ volatile ("movl %0, %%cr3" : : "r"(cr3) : "memory");
}
