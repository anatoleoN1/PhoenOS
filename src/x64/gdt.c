/*
 * PhoenOS perf64 - x64/gdt.c
 * GDT du mode long : 0 nul, 1 code noyau (0x08), 2 donnees noyau (0x10), 3 code utilisateur 32 bits (0x18|3 = 0x1B),
 * 4 donnees utilisateur (0x20|3 = 0x23), 5-6 TSS de 16 octets (0x28). Memes selecteurs que perf32 (gdt.h).
 * Le TSS donne la pile du noyau (rsp0) utilisee quand un programme ring 3 est interrompu, et une pile dediee (IST1)
 * pour la double faute : meme une pile noyau corrompue laisse afficher l'ecran d'erreur.
 */
#include "gdt.h"
#include "types.h"
#include "panic.h"

struct tss64 {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3, iomap_base;
} __attribute__((packed));

struct gdt_ptr64 {
    u16 limit;
    u64 base;
} __attribute__((packed));

#define DF_STACK_SIZE 8192

struct tss64 tss;
static u64 gdt[7] __attribute__((aligned(16)));
static struct gdt_ptr64 gdt_pointer;
static u8 df_stack[DF_STACK_SIZE] __attribute__((aligned(16)));

extern void gdt_flush(const struct gdt_ptr64 *p);

void tss_set_rsp0(usize rsp0)
{
    tss.rsp0 = rsp0;
}

void gdt_init(void)
{
    u64 base = (u64)(usize)&tss, limit = sizeof(tss) - 1;

    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFull;             /* code noyau 64 bits */
    gdt[2] = 0x00CF92000000FFFFull;             /* donnees noyau */
    gdt[3] = 0x00CFFA000000FFFFull;             /* code utilisateur 32 bits (ring 3, mode de compatibilite : programmes i386) */
    gdt[4] = 0x00CFF2000000FFFFull;             /* donnees utilisateur (ring 3) */
    gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ull << 40) | (((limit >> 16) & 0xF) << 48) |
             (((base >> 24) & 0xFF) << 56);     /* TSS 64 bits disponible */
    gdt[6] = base >> 32;

    tss.rsp0 = (u64)(usize)stack_top;
    tss.ist[0] = (u64)(usize)(df_stack + DF_STACK_SIZE);
    tss.iomap_base = (u16)sizeof(tss);          /* pas de bitmap : tous les ports interdits en ring 3 */

    gdt_pointer.limit = (u16)(sizeof(gdt) - 1);
    gdt_pointer.base = (u64)(usize)gdt;
    gdt_flush(&gdt_pointer);
    __asm__ volatile ("ltr %%ax" : : "a"((u16)GDT_TSS));
}
