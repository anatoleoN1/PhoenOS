#ifndef PHOENOS_ELF_H
#define PHOENOS_ELF_H

#include "types.h"
#include "error.h"

/*
 * Charge un programme ELF 32 ou 64 bits (x86, executable statique) dans la zone memoire
 * [lo, hi) : `mem` pointe sur l'adresse `lo`. Chaque segment PT_LOAD est copie a son
 * adresse virtuelle, le reste (.bss) est mis a zero. Ne lit jamais hors de `img`, n'ecrit
 * jamais hors de [lo, hi). Renvoie E_NOEXEC si le fichier n'est pas un programme valide.
 */
/*
 * Classe d'un executable : 32 (ELF i386), 64 (ELF x86-64) ou 0 (autre / illisible). Ne regarde que l'en-tete.
 * Les deux classes se chargent dans la meme zone (adresses sous 4 Gio) ; c'est le noyau qui decide s'il sait
 * lancer un programme 64 bits (edition perf64 seulement).
 */
int elf_class(const u8 *img, u32 size);

/* Un segment a charger (PT_LOAD non vide). */
struct elf_seg { u32 lo, hi; int writable; };
#define ELF_MAX_SEGS 8

/*
 * Valide le fichier et liste ses segments sans rien ecrire (pour reserver la memoire avant de
 * charger). E_NOEXEC si invalide ou s'il y a plus de ELF_MAX_SEGS segments ou s'ils se chevauchent.
 */
err_t elf_segments(const u8 *img, u32 size, u32 lo, u32 hi, struct elf_seg *segs, int *nsegs, u32 *entry);

/*
 * Variante qui n'ecrit pas directement : chaque morceau passe par put(ctx, adresse_virtuelle, source, n)
 * (source NULL = n octets a zero). Sert a remplir l'espace d'un AUTRE programme sans changer de CR3.
 */
typedef err_t (*elf_put_fn)(void *ctx, u32 va, const u8 *src, u32 n);
err_t elf_load_with(const u8 *img, u32 size, elf_put_fn put, void *pctx, u32 lo, u32 hi, u32 *entry);

err_t elf_load(const u8 *img, u32 size, u8 *mem, u32 lo, u32 hi, u32 *entry);

/*
 * Ou chercher selon la classe (arborescence "system32" de PhoenOS) : sur perf64, les programmes et bibliotheques 64 bits
 * (natifs) sont dans /bin, /apps, /lib et les 32 bits (mode de compatibilite) dans /bin32, /apps32, /lib32.
 * Sur perf32, tout est 32 bits : /bin, /apps, /lib comme avant.
 */
#ifdef __x86_64__
#define ELF_NATIVE 64
#else
#define ELF_NATIVE 32
#endif
enum { DIR_BIN = 0, DIR_APPS = 1, DIR_LIB = 2 };
static inline const char *exec_dir(int kind, int cls)
{
    static const char *const nat[3] = { "/bin/", "/apps/", "/lib/" };
    static const char *const old[3] = { "/bin32/", "/apps32/", "/lib32/" };

    return (cls == ELF_NATIVE) ? nat[kind] : old[kind];
}

#endif
