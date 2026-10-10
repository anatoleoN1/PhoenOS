/*
 * PhoenOS - kheap.c
 * Tas du noyau : liste chaînée de blocs triés par adresse, "first fit",
 * découpage des blocs trop grands et fusion des voisins libres.
 * Chaque bloc commence par un en-tête de 16 octets contenant un nombre
 * magique : il permet de détecter les doubles libérations et les débordements.
 */
#include "kheap.h"
#include "pmm.h"
#include "panic.h"
#include "string.h"
#include "io.h"

#define HEAP_ALIGN   16u
#define HEAP_MAX_FRAMES 2048u           /* 8 Mio */
#define HEAP_MIN_FRAMES 16u             /* 64 Kio */

#define MAGIC_USED 0xA110CA7Eu
#define MAGIC_FREE 0xF4EEF4EEu

struct block {
    u32 magic;
    u32 size;                           /* taille utile, sans l'en-tête */
    struct block *prev;
    struct block *next;
};

_Static_assert(sizeof(struct block) == HEAP_ALIGN, "en-tete de bloc : 16 octets attendus");

static struct block *heap_first;
static u32 heap_start, heap_end;

static void verify_magic(const struct block *b, const char *where)
{
    if (b->magic != MAGIC_USED && b->magic != MAGIC_FREE)
        PANIC("Corruption du tas (%s) : en-tete invalide a %x (magic=%x)",
              where, (u32)b, b->magic);
}

void kheap_init(void)
{
    u32 frames = pmm_free_frames() / 4;         /* au plus le quart de la mémoire libre */
    u32 base;

    if (frames > HEAP_MAX_FRAMES)
        frames = HEAP_MAX_FRAMES;
    if (frames < HEAP_MIN_FRAMES)
        PANIC("Memoire insuffisante pour le tas du noyau (%u frames libres)",
              pmm_free_frames());

    base = pmm_alloc_contiguous(frames);
    if (!base)
        PANIC("Impossible de reserver %u frames contigues pour le tas", frames);

    heap_start = base;
    heap_end   = base + frames * PMM_FRAME_SIZE;

    heap_first        = (struct block *)base;
    heap_first->magic = MAGIC_FREE;
    heap_first->size  = (heap_end - heap_start) - (u32)sizeof(struct block);
    heap_first->prev  = NULL;
    heap_first->next  = NULL;
}

u32 kheap_size(void)
{
    return heap_end - heap_start;
}

void *kmalloc(usize size)
{
    u32 irq;
    struct block *b;

    if (size == 0 || size > heap_end - heap_start)
        return NULL;
    size = (size + HEAP_ALIGN - 1) & ~(usize)(HEAP_ALIGN - 1);

    irq = cpu_irq_save();
    for (b = heap_first; b; b = b->next) {
        verify_magic(b, "kmalloc");
        if (b->magic != MAGIC_FREE || b->size < size)
            continue;

        /* Découpe si le reste peut contenir un en-tête + un minimum de données */
        if (b->size >= size + sizeof(struct block) + HEAP_ALIGN) {
            struct block *n = (struct block *)((u8 *)(b + 1) + size);

            n->magic = MAGIC_FREE;
            n->size  = b->size - (u32)size - (u32)sizeof(struct block);
            n->prev  = b;
            n->next  = b->next;
            if (b->next)
                b->next->prev = n;
            b->next = n;
            b->size = (u32)size;
        }
        b->magic = MAGIC_USED;
        cpu_irq_restore(irq);
        return b + 1;
    }
    cpu_irq_restore(irq);
    return NULL;
}

void *kzalloc(usize size)
{
    void *p = kmalloc(size);

    if (p)
        memset(p, 0, size);
    return p;
}

void kfree(void *ptr)
{
    u32 irq, p = (u32)ptr;
    struct block *b, *n;

    if (!ptr)
        return;
    if (p < heap_start + sizeof(struct block) || p >= heap_end || (p & (HEAP_ALIGN - 1)))
        PANIC("kfree(%x) : pointeur hors du tas ou mal aligne", p);

    irq = cpu_irq_save();
    b = (struct block *)ptr - 1;

    if (b->magic == MAGIC_FREE)
        PANIC("Double liberation detectee : kfree(%x)", p);
    if (b->magic != MAGIC_USED)
        PANIC("Corruption du tas : en-tete invalide pour kfree(%x) (magic=%x)", p, b->magic);

    b->magic = MAGIC_FREE;

    /* Fusion avec le bloc suivant s'il est libre */
    n = b->next;
    if (n) {
        verify_magic(n, "kfree/suivant");
        if (n->magic == MAGIC_FREE) {
            b->size += (u32)sizeof(struct block) + n->size;
            b->next = n->next;
            if (n->next)
                n->next->prev = b;
            n->magic = 0;
        }
    }

    /* Fusion avec le bloc précédent s'il est libre */
    n = b->prev;
    if (n) {
        verify_magic(n, "kfree/precedent");
        if (n->magic == MAGIC_FREE) {
            n->size += (u32)sizeof(struct block) + b->size;
            n->next = b->next;
            if (b->next)
                b->next->prev = n;
            b->magic = 0;
        }
    }
    cpu_irq_restore(irq);
}

void kheap_check(void)
{
    u32 irq = cpu_irq_save();
    const struct block *prev = NULL;

    for (const struct block *b = heap_first; b; b = b->next) {
        verify_magic(b, "kheap_check");
        if (b->prev != prev)
            PANIC("Corruption du tas : chainage casse au bloc %x", (u32)b);
        if ((u32)b < heap_start || (u32)(b + 1) + b->size > heap_end)
            PANIC("Corruption du tas : bloc %x hors des bornes (taille %u)", (u32)b, b->size);
        prev = b;
    }
    cpu_irq_restore(irq);
}

void kheap_get_stats(struct kheap_stats *out)
{
    u32 irq = cpu_irq_save();

    memset(out, 0, sizeof(*out));
    for (const struct block *b = heap_first; b; b = b->next) {
        verify_magic(b, "kheap_get_stats");
        if (b->magic == MAGIC_USED) {
            out->used_bytes += b->size;
            out->used_blocks++;
        } else {
            out->free_bytes += b->size;
            out->free_blocks++;
        }
    }
    cpu_irq_restore(irq);
}
