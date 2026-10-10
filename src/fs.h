#ifndef PHOENOS_FS_H
#define PHOENOS_FS_H

#include "types.h"
#include "error.h"

/*
 * Système de fichiers en mémoire (RAM) : arbre de dossiers et de fichiers, avec
 * chemins absolus (/etc/motd) ou relatifs au dossier courant (docs/a.txt, ../b, .).
 * Tout est perdu à l'extinction : le contenu de départ vient de l'initrd (initrd.c).
 *
 * Limites : noms de 31 caractères, chemins de 255, fichiers de 1 Mio,
 * 4096 éléments, 32 niveaux de dossiers.
 */
#define FS_NAME_MAX  31
#define FS_PATH_MAX  256
#define FS_FILE_MAX  (1u << 20)
#define FS_NODES_MAX 4096
#define FS_DEPTH_MAX 32

enum fs_type { FS_DIR = 1, FS_FILE = 2 };

/* Un élément (dossier ou fichier). Les champs se LISENT librement ; ne les modifiez pas : utilisez fs_*(). */
struct fs_node {
    char name[FS_NAME_MAX + 1];     /* "" pour la racine */
    u8   type;                      /* FS_DIR ou FS_FILE */
    u8   depth;                     /* racine = 0        */
    struct fs_node *parent;         /* NULL pour la racine */
    struct fs_node *child;          /* premier enfant (dossiers), triés par nom */
    struct fs_node *next;           /* frère suivant, dans l'ordre alphabétique */
    u8  *data;                      /* contenu (fichiers)  */
    u32  size;                      /* taille en octets    */
    u32  cap;                       /* capacité allouée    */
};

struct fs_stats {
    u32 files;
    u32 dirs;                       /* racine comprise */
    u32 bytes;                      /* total du contenu des fichiers */
};

/* Flags de fs_remove */
#define FS_RM_DIR_OK    1           /* accepte de supprimer un dossier (vide) */
#define FS_RM_RECURSIVE 2           /* supprime aussi le contenu des dossiers */

/* (Ré)initialise : arbre vide avec seulement la racine "/" ; dossier courant = racine. */
void fs_init(void);

struct fs_node *fs_root(void);
struct fs_node *fs_cwd(void);

/* Cherche un élément. E_NOENT s'il n'existe pas, E_NOTDIR si un fichier barre le chemin. */
err_t fs_lookup(const char *path, struct fs_node **out);

err_t fs_chdir(const char *path);

/* Crée un dossier (E_EXIST s'il existe). Le dossier parent doit exister. */
err_t fs_mkdir(const char *path);

/* Crée le dossier et tous ses parents manquants ; pas d'erreur s'il existe déjà. */
err_t fs_mkdir_p(const char *path);

/* Crée un fichier vide, ou renvoie le fichier existant. E_ISDIR si c'est un dossier. */
err_t fs_create(const char *path, struct fs_node **out);

/* Remplace tout le contenu / ajoute à la fin. E_FBIG au-delà de FS_FILE_MAX, E_NOMEM si le tas est plein. */
err_t fs_write(struct fs_node *file, const void *data, u32 len);
err_t fs_append(struct fs_node *file, const void *data, u32 len);

/*
 * Supprime un élément. E_BUSY pour la racine ou un dossier qui contient le dossier courant.
 * Un dossier exige FS_RM_DIR_OK (sinon E_ISDIR), et FS_RM_RECURSIVE s'il n'est pas vide (sinon E_NOTEMPTY).
 */
err_t fs_remove(const char *path, int flags);

/* Déplace/renomme. Si dst est un dossier existant, src y est déplacé. E_EXIST si la cible existe déjà. */
err_t fs_rename(const char *src, const char *dst);

/* Copie un fichier (écrase la cible si c'est un fichier ; si c'est un dossier, copie dedans). */
err_t fs_copy(const char *src, const char *dst);

/* Écrit le chemin absolu de `n` dans buf. E_NAMETOOLONG si buf est trop petit. */
err_t fs_path_of(const struct fs_node *n, char *buf, usize size);

void fs_get_stats(struct fs_stats *out);

#endif
