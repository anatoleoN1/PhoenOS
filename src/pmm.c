/*
 * PhoenOS - pmm.c
 * Gestionnaire de mémoire physique : un bit par frame de 4 Kio (1 = utilisée).
 * Gère jusqu'à 4 Gio de RAM (bitmap de 128 Kio dans le .bss du noyau).
 */
#include "pmm.h"
#include "multiboot.h"
#include "panic.h"

#define MAX_FRAMES   (1u << 20)                 /* 4 Gio / 4 Kio */
#define BITMAP_WORDS (MAX_FRAMES / 32)
#define LOW_MEMORY   0x100000u                  /* 1 Mio : BIOS, VGA, etc. */

extern char kernel_start[];
extern char kernel_end[];

static u32 bitmap[BITMAP_WORDS];
static u32 frame_count;         /* nombre de frames couvertes par la carte */
static u32 usable_count;        /* frames annoncées disponibles */
static u32 free_count;          /* frames libres maintenant */
static u32 search_hint;         /* mot du bitmap où reprendre la recherche */

static inline int  bit_test(u32 f)  { return (bitmap[f >> 5] >> (f & 31)) & 1; }
static inline void bit_set(u32 f)   { bitmap[f >> 5] |= 1u << (f & 31); }
static inline void bit_clear(u32 f) { bitmap[f >> 5] &= ~(1u << (f & 31)); }

/* Déclare libre une région disponible (arrondie vers l'intérieur). */
static void add_region(u32 addr_lo, u32 addr_hi, u32 len_lo, u32 len_hi)
{
    u32 end, first, last;

    if (addr_hi || addr_lo > 0xFFFFE000u)
        return;                                 /* au-delà de 4 Gio : ignoré */

    end = addr_lo + len_lo;
    if (len_hi || end < addr_lo)
        end = 0xFFFFF000u;                      /* plafonné à 4 Gio */

    first = (addr_lo + PMM_FRAME_SIZE - 1) >> 12;
    last  = end >> 12;
    if (last > MAX_FRAMES)
        last = MAX_FRAMES;

    for (u32 f = first; f < last; f++) {
        if (bit_test(f)) {
            bit_clear(f);
            usable_count++;
            free_count++;
        }
    }
    if (last > frame_count)
        frame_count = last;
}

/* Réserve une plage (arrondie vers l'extérieur) : noyau, BIOS, infos GRUB... */
static void reserve_range(u32 start, u32 end)
{
    u32 first = start >> 12;
    u32 last  = (end + PMM_FRAME_SIZE - 1) >> 12;

    for (u32 f = first; f < last && f < frame_count; f++) {
        if (!bit_test(f)) {
            bit_set(f);
            free_count--;
        }
    }
}

void pmm_init(const struct multiboot_info *mbi)
{
    for (u32 i = 0; i < BITMAP_WORDS; i++)
        bitmap[i] = 0xFFFFFFFFu;                /* tout est "utilisé" par défaut */

    if (mbi->flags & MB_FLAG_MMAP) {
        u32 addr = mbi->mmap_addr;
        u32 end  = addr + mbi->mmap_length;

        while (addr + sizeof(struct multiboot_mmap_entry) <= end) {
            const struct multiboot_mmap_entry *e = (const struct multiboot_mmap_entry *)addr;

            if (e->size < 20)
                break;                          /* entrée invalide : on arrête */
            if (e->type == MB_MMAP_AVAILABLE)
                add_region(e->addr_lo, e->addr_hi, e->len_lo, e->len_hi);
            addr += e->size + 4;
        }
    } else if (mbi->flags & MB_FLAG_MEM) {
        add_region(LOW_MEMORY, 0, mbi->mem_upper * 1024u, 0);
    } else {
        PANIC("Le bootloader ne fournit aucune information sur la memoire");
    }

    reserve_range(0, LOW_MEMORY);
    reserve_range((u32)kernel_start, (u32)kernel_end);
    reserve_range((u32)mbi, (u32)mbi + 128);
    if (mbi->flags & MB_FLAG_MMAP)
        reserve_range(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);

    if (free_count == 0)
        PANIC("Aucune memoire libre apres l'initialisation du gestionnaire physique");
}

u32 pmm_alloc_frame(void)
{
    u32 words = (frame_count + 31) / 32;

    if (words == 0)
        return 0;

    for (u32 n = 0; n < words; n++) {
        u32 w = (search_hint + n) % words;

        if (bitmap[w] != 0xFFFFFFFFu) {
            u32 f = w * 32 + (u32)__builtin_ctz(~bitmap[w]);

            if (f >= frame_count)
                continue;
            bit_set(f);
            free_count--;
            search_hint = w;
            return f * PMM_FRAME_SIZE;
        }
    }
    return 0;
}

u32 pmm_alloc_contiguous(u32 count)
{
    u32 run = 0;

    if (count == 0)
        return 0;

    for (u32 f = 0; f < frame_count; f++) {
        if (bit_test(f)) {
            run = 0;
            continue;
        }
        if (++run == count) {
            u32 start = f + 1 - count;

            for (u32 i = start; i <= f; i++)
                bit_set(i);
            free_count -= count;
            return start * PMM_FRAME_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(u32 addr)
{
    u32 f = addr >> 12;

    if (addr & (PMM_FRAME_SIZE - 1))
        PANIC("pmm_free_frame(%x) : adresse non alignee sur une frame", addr);
    if (f >= frame_count)
        PANIC("pmm_free_frame(%x) : adresse hors de la memoire geree", addr);
    if (addr < LOW_MEMORY ||
        (addr + PMM_FRAME_SIZE > (u32)kernel_start && addr < (u32)kernel_end))
        PANIC("pmm_free_frame(%x) : tentative de liberer une frame reservee", addr);
    if (!bit_test(f))
        PANIC("Double liberation de la frame %x", addr);

    bit_clear(f);
    free_count++;
}

u32 pmm_usable_frames(void) { return usable_count; }
u32 pmm_free_frames(void)   { return free_count; }
