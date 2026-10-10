/*
 * PhoenOS - initrd.c
 * Lecteur d'archives tar (USTAR) : remplit le système de fichiers au démarrage.
 *
 * Une archive tar est une suite de blocs de 512 octets : un en-tête, puis le contenu
 * du fichier arrondi à 512 octets, et ainsi de suite ; deux blocs de zéros la terminent.
 */
#include "initrd.h"
#include "fs.h"
#include "string.h"

#define TAR_BLOCK 512

/* Positions dans l'en-tête */
#define H_NAME     0      /* 100 octets */
#define H_SIZE     124    /* 12 octets, octal */
#define H_CHKSUM   148    /* 8 octets, octal */
#define H_TYPE     156
#define H_MAGIC    257    /* "ustar" */
#define H_PREFIX   345    /* 155 octets */

/* Lit un nombre octal ASCII (espaces/NUL autour permis). */
static err_t parse_octal(const u8 *s, usize n, u32 *out)
{
    u32 v = 0;
    usize i = 0;
    int digits = 0;

    if (n && (s[0] & 0x80))
        return E_FBIG;                          /* codage base 256 (fichiers > 8 Gio) : non géré */
    while (i < n && s[i] == ' ')
        i++;
    for (; i < n && s[i] >= '0' && s[i] <= '7'; i++) {
        if (v > 0x1FFFFFFFu)
            return E_FBIG;
        v = (v << 3) | (u32)(s[i] - '0');
        digits++;
    }
    for (; i < n; i++)
        if (s[i] != ' ' && s[i] != '\0')
            return E_INVAL;                     /* caractère parasite */
    if (!digits)
        return E_INVAL;
    *out = v;
    return E_OK;
}

static int block_is_zero(const u8 *b)
{
    for (usize i = 0; i < TAR_BLOCK; i++)
        if (b[i])
            return 0;
    return 1;
}

static int checksum_ok(const u8 *h)
{
    u32 sum = 0, stored;

    for (usize i = 0; i < TAR_BLOCK; i++)
        sum += (i >= H_CHKSUM && i < H_CHKSUM + 8) ? ' ' : h[i];     /* le champ lui-même compte pour des espaces */
    if (parse_octal(h + H_CHKSUM, 8, &stored) != E_OK)
        return 0;
    return sum == stored;
}

/* Copie au plus `max` caractères de src (arrêt au NUL) dans dst, renvoie la longueur. */
static usize copy_field(char *dst, const u8 *src, usize max)
{
    usize n = 0;

    while (n < max && src[n]) {
        dst[n] = (char)src[n];
        n++;
    }
    return n;
}

/*
 * Nettoie le chemin de l'archive : retire "./" et "/" de tête. Renvoie 0 si le chemin est
 * dangereux (contient "..") ou vide après nettoyage ; sinon 1 et `clean` reçoit "/chemin/propre".
 */
static int sanitize(const char *path, char *clean)
{
    const char *p = path;
    usize n = 0;

    clean[n++] = '/';
    for (;;) {
        const char *start;
        usize len;

        while (*p == '/')
            p++;
        if (p[0] == '.' && (p[1] == '/' || p[1] == '\0')) {         /* composant "." : ignoré */
            p++;
            continue;
        }
        if (*p == '\0')
            break;

        start = p;
        while (*p && *p != '/')
            p++;
        len = (usize)(p - start);
        if (len == 2 && start[0] == '.' && start[1] == '.')
            return 0;                                               /* remontée interdite */
        if (n + len + 1 >= FS_PATH_MAX)
            return 0;
        if (n > 1)
            clean[n++] = '/';
        memcpy(clean + n, start, len);
        n += len;
    }
    clean[n] = '\0';
    return n > 1;
}

/* Erreurs qui arrêtent le chargement (le reste est ignoré entrée par entrée). */
static int fatal(err_t e)
{
    return e == E_NOMEM;
}

err_t initrd_load(const u8 *data, u32 size, struct initrd_stats *st)
{
    u32 off = 0;
    struct initrd_stats local;

    if (!st)
        st = &local;
    memset(st, 0, sizeof(*st));

    while (off < size) {
        const u8 *h = data + off;
        char raw[FS_PATH_MAX * 2], clean[FS_PATH_MAX];
        usize n;
        u32 fsize, padded;
        u8 type;
        err_t e;

        if (size - off < TAR_BLOCK)
            return E_IO;                        /* bloc incomplet : archive tronquée */
        if (block_is_zero(h))
            return E_OK;                        /* marqueur de fin */
        if (memcmp(h + H_MAGIC, "ustar", 5) != 0 || !checksum_ok(h))
            return E_IO;                        /* pas une archive tar valide, ou corrompue */

        if (parse_octal(h + H_SIZE, 12, &fsize) != E_OK)
            fsize = 0xFFFFFFFFu;
        type = h[H_TYPE];

        /* Le contenu doit tenir dans l'archive */
        if (fsize > size - off - TAR_BLOCK)
            return E_IO;
        padded = (fsize + TAR_BLOCK - 1) & ~(u32)(TAR_BLOCK - 1);
        if (padded < fsize)
            return E_IO;

        /* Nom complet : prefix + "/" + name */
        n = 0;
        if (h[H_PREFIX]) {
            n = copy_field(raw, h + H_PREFIX, 155);
            raw[n++] = '/';
        }
        n += copy_field(raw + n, h + H_NAME, 100);
        raw[n] = '\0';

        if (type == '0' || type == '\0' || type == '5') {
            if (!sanitize(raw, clean)) {
                if (n && strcmp(raw, "./") != 0 && strcmp(raw, ".") != 0)
                    st->skipped++;              /* chemin dangereux ou invalide ; "./" seul est normal */
            } else if (type == '5') {
                e = fs_mkdir_p(clean);
                if (e == E_OK)
                    st->dirs++;
                else if (fatal(e))
                    return e;
                else
                    st->skipped++;
            } else if (fsize > FS_FILE_MAX) {
                st->skipped++;
            } else {
                struct fs_node *node;
                char *slash = clean + strlen(clean);

                while (slash > clean && *slash != '/')
                    slash--;
                e = E_OK;
                if (slash > clean) {            /* crée les dossiers parents */
                    *slash = '\0';
                    e = fs_mkdir_p(clean);
                    *slash = '/';
                }
                if (e == E_OK)
                    e = fs_create(clean, &node);
                if (e == E_OK)
                    e = fs_write(node, h + TAR_BLOCK, fsize);
                if (e == E_OK) {
                    st->files++;
                    st->bytes += fsize;
                } else if (fatal(e)) {
                    return e;
                } else {
                    st->skipped++;
                }
            }
        } else {
            st->skipped++;                      /* liens, périphériques, en-têtes pax/GNU... */
        }

        off += TAR_BLOCK + padded;
    }
    return E_OK;                                /* fin sans marqueur : tolérée */
}
