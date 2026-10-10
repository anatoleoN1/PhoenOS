#ifndef PHOENOS_INITRD_H
#define PHOENOS_INITRD_H

#include "types.h"
#include "error.h"

struct initrd_stats {
    u32 files;          /* fichiers créés                               */
    u32 dirs;           /* dossiers créés explicitement par l'archive   */
    u32 bytes;          /* octets de contenu copiés                     */
    u32 skipped;        /* entrées ignorées (liens, chemin dangereux, trop grand...) */
};

/*
 * Charge une archive tar (format USTAR, celui de `tar --format=ustar`) dans le
 * système de fichiers (fs.c). Les données sont COPIÉES : la zone d'origine peut être réutilisée.
 *
 * Renvoie E_OK, ou une erreur (E_IO : archive corrompue/tronquée, E_NOMEM : tas plein).
 * En cas d'erreur, ce qui a déjà été chargé reste en place et `st` est à jour.
 *
 * Sécurité : un chemin contenant ".." est ignoré ; un nom invalide est ignoré ;
 * les liens et fichiers spéciaux sont ignorés (comptés dans `skipped`).
 */
err_t initrd_load(const u8 *data, u32 size, struct initrd_stats *st);

#endif
