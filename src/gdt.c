/*
 * PhoenOS - gdt.c
 * Table des descripteurs globaux : modèle "flat" (segments 0 - 4 Gio).
 * Index : 0 nul, 1 code noyau, 2 données noyau, 3 code user, 4 données user.
 */
#include "gdt.h"
#include "types.h"
#include "panic.h"

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

#define GDT_ENTRIES 6

/* Segment d'etat de tache : le CPU y lit la pile du noyau a utiliser quand un programme
 * utilisateur (ring 3) est interrompu. Pas de commutation de taches materielle. */
struct tss_entry {
    u32 prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap_base;
} __attribute__((packed));

struct tss_entry tss;                           /* esp0 est mis a jour par user_enter */

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gdt_pointer;

extern void gdt_flush(u32 gdt_ptr_address);

static void gdt_set(int i, u32 base, u32 limit, u8 access, u8 gran)
{
    gdt[i].base_low    = (u16)(base & 0xFFFF);
    gdt[i].base_mid    = (u8)((base >> 16) & 0xFF);
    gdt[i].base_high   = (u8)((base >> 24) & 0xFF);
    gdt[i].limit_low   = (u16)(limit & 0xFFFF);
    gdt[i].granularity = (u8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access      = access;
}

void gdt_init(void)
{
    gdt_pointer.limit = (u16)(sizeof(gdt) - 1);
    gdt_pointer.base  = (u32)&gdt;

    gdt_set(0, 0, 0,          0x00, 0x00);  /* descripteur nul        */
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);  /* code noyau  (ring 0)   */
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF);  /* données noyau (ring 0) */
    gdt_set(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);  /* code user   (ring 3)   */
    gdt_set(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);  /* données user (ring 3)  */

    tss.ss0 = GDT_KERNEL_DATA;
    tss.esp0 = (u32)stack_top;
    tss.iomap_base = (u16)sizeof(tss);          /* pas de bitmap : tous les ports interdits en ring 3 */
    gdt_set(5, (u32)&tss, sizeof(tss) - 1, 0x89, 0x00);   /* TSS 32 bits disponible */

    gdt_flush((u32)&gdt_pointer);
    __asm__ volatile ("ltr %%ax" : : "a"((u16)GDT_TSS));
}
