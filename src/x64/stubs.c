/*
 * PhoenOS perf64 - x64/stubs.c
 * Versions "64 bits" de la pagination :
 *   - pagination : le code de demarrage (boot.S) a deja mappe 4 Gio en identite (pages de 2 Mio, >= 1 Gio sans cache) ;
 *     il n'y a donc rien a construire ici (la protection page par page viendra avec le mode utilisateur) ;
 */
#include "types.h"
#include "error.h"
#include "paging.h"
#include "aspace.h"

/* ---------- pagination ---------- */
extern u64 boot_pml4[];                    /* x64/boot.S */

static u32 kernel_cr3;

err_t paging_init(void)
{
    kernel_cr3 = (u32)(usize)boot_pml4;
    aspace_set_kernel_dir(boot_pml4);      /* modele des espaces des programmes (aspace.c) */
    return E_OK;
}
int   paging_enabled(void)                 { return 1; }
u32   paging_table_bytes(void)             { return 3u * 4096u; }
u32   paging_mapped_top(void)              { return PAGING_MAX_TOP; }
err_t paging_map_mmio(u32 phys, u32 size)  { (void)phys; (void)size; return E_OK; }   /* deja mappe (4 Gio) */


/* Charge le PML4 du programme / du noyau dans CR3 (vide aussi le cache d'adresses). */
void aspace_activate(const struct aspace *a)
{
    usize cr3 = aspace_cr3(a);

    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

void aspace_activate_kernel(void)
{
    usize cr3 = kernel_cr3;

    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}
