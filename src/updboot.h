#ifndef PHOENOS_UPDBOOT_H
#define PHOENOS_UPDBOOT_H

#include "types.h"
#include "error.h"
#include "updstore.h"
#include "updman.h"

/*
 * Demarrage avec mise a jour (0.10.4) : "etape 0".
 *
 * GRUB charge toujours le noyau de l'ISO (la version "d'usine"). Tout au debut de kernel_main, avant meme la GDT,
 * ce noyau regarde si une version plus recente et SIGNEE est installee dans un emplacement du disque ; si oui il la
 * lit, verifie signature et empreintes, la copie a la place de lui-meme et y saute avec les memes informations
 * Multiboot que celles de GRUB (un "kexec" tres simple). Si quoi que ce soit ne va pas, il continue de demarrer :
 * l'ISO reste toujours un point de retour.
 *
 * Une nouvelle version est "a l'essai" : si elle ne tient pas UPDATE_CONFIRM_S secondes, elle n'est pas confirmee ;
 * apres UPDATE_MAX_TRIES demarrages non confirmes, l'etape 0 revient a la version precedente (retour arriere).
 */
#define UPD_RELAUNCH_NAME "PhoenOS-relance"     /* nom de "bootloader" donne a un noyau relance */

struct upd_boot_env {
    const struct upd_blk *blk;
    const u8 *pubkey;               /* NULL : pas de cle embarquee, aucune mise a jour */
    u32 running_build;
    const char *running_edition;
    int relaunched;                 /* 1 : ce noyau a deja ete lance par une etape 0 */
};

enum { UPB_CONTINUE = 0, UPB_RELAUNCH = 1 };

struct upd_boot_plan {
    int action;
    int slot;                       /* emplacement a lancer (UPB_RELAUNCH) */
    const char *why;                /* raison, pour le journal */
    int rolled_back;                /* 1 : un retour arriere a eu lieu pendant ce choix */
    struct upd_slot s;
    struct upd_manifest m;
};

/* Verifie un emplacement : en-tete, signature, manifeste, edition, coherence. 0 si valide. */
int upd_slot_verify(const struct upd_blk *b, int slot, const u8 *pk, const char *edition,
                    struct upd_slot *s, struct upd_manifest *m);

/* Choisit entre "continuer" et "lancer l'emplacement" ; met a jour l'etat (essais, retour arriere). */
int upd_boot_select(const struct upd_boot_env *env, struct upd_boot_plan *plan);

/* Retour arriere : abandonne l'emplacement actif (annule son en-tete) et revient au precedent si valide, sinon a l'ISO. */
err_t upd_rollback(const struct upd_blk *b, struct upd_ctl *c, const u8 *pk, const char *edition, int *now_active);

/* Declare le demarrage actuel comme bon (essais remis a zero, build confirme). Ecrit seulement si necessaire. */
err_t upd_confirm(const struct upd_blk *b, u32 running_build);

/* ---- relance ---- */
enum { UPB_OK = 0, UPB_E_MEM = -1, UPB_E_DISK = -2, UPB_E_HASH = -3, UPB_E_IMAGE = -4 };

struct relaunch_args {              /* lu par relaunch.S : l'ordre des champs est impose */
    u32 src;                        /*  0 : octets du noyau a copier (apres l'en-tete PBI1) */
    u32 dst;                        /*  4 : adresse de chargement */
    u32 copy_len;                   /*  8 : octets a copier (multiple de 4) */
    u32 zero_dst;                   /* 12 : debut du .bss */
    u32 zero_len;                   /* 16 : taille du .bss */
    u32 entry;                      /* 20 : point d'entree */
    u32 mbi;                        /* 24 : infos Multiboot pour le noyau lance (ebx) */
    u32 magic;                      /* 28 : 0x2BADB002 (eax) */
    u32 sp;                         /* 32 : pile du code de copie */
};

/* stub : adresse (dans la memoire de preparation) du code de copie ; args : ses arguments. Ne revient pas, dans le noyau. */
typedef void (*upd_jump_fn)(void *stub, const struct relaunch_args *args);

struct multiboot_info;

/*
 * Prepare et lance l'emplacement `plan->slot`. Ne revient pas en cas de succes (dans le noyau) ; revient avec un code
 * UPB_E_* si quelque chose ne va pas AVANT de toucher a la memoire du noyau en cours (disque, memoire, empreinte, image).
 * stub, stub_len : le petit code de copie (relaunch.S), recopie dans la memoire de preparation ; jump : appelle ce code.
 */
int upd_relaunch(const struct upd_blk *b, const struct upd_boot_plan *plan, u32 mbi_phys, u32 old_kernel_end,
                 const void *stub, u32 stub_len, upd_jump_fn jump);

extern const char *upd_last_failure;    /* texte de la derniere raison d'echec (journal) */

#endif
