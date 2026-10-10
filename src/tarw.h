#ifndef PHOENOS_TARW_H
#define PHOENOS_TARW_H

#include "types.h"
#include "error.h"

/*
 * Écrit tout le système de fichiers (fs.c) au format tar USTAR : le même format que lit
 * initrd.c, et que sait lire l'outil `tar` d'un PC (tar tvf image.tar). Les éléments sont
 * produits dans l'ordre : un dossier avant son contenu.
 */
#define TARW_BLOCK 512

/* Reçoit les blocs de 512 octets un par un. Renvoyer autre chose que E_OK interrompt l'écriture. */
typedef err_t (*tarw_emit_fn)(const u8 *block, void *ctx);

/* Taille exacte, en octets, de l'archive que tarw_write produirait. */
u32 tarw_size(void);

err_t tarw_write(tarw_emit_fn emit, void *ctx);

#endif
