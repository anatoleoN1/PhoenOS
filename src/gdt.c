/*
 * PhoenOS - gdt.c
 * Table des descripteurs globaux : modèle "flat" (segments 0 - 4 Gio).
 * Index : 0 nul, 1 code noyau, 2 données noyau, 3 code user, 4 données user.
 */
#include "gdt.h"
#include "types.h"

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

#define GDT_ENTRIES 5

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

    gdt_flush((u32)&gdt_pointer);
}
