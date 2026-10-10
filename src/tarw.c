/*
 * PhoenOS - tarw.c
 * Sérialise le système de fichiers en archive tar (USTAR).
 */
#include "tarw.h"
#include "fs.h"
#include "string.h"

struct writer {
    tarw_emit_fn emit;
    void        *ctx;
};

static u32 size_of_tree(const struct fs_node *dir)
{
    u32 total = 0;

    for (const struct fs_node *c = dir->child; c; c = c->next) {
        total += TARW_BLOCK;
        if (c->type == FS_FILE)
            total += (c->size + TARW_BLOCK - 1) & ~(u32)(TARW_BLOCK - 1);
        else
            total += size_of_tree(c);
    }
    return total;
}

u32 tarw_size(void)
{
    return size_of_tree(fs_root()) + 2 * TARW_BLOCK;      /* + deux blocs de zéros de fin */
}

/* Écrit `v` en octal sur n-1 chiffres suivis d'un NUL. */
static void put_octal(u8 *dst, int n, u32 v)
{
    for (int i = n - 2; i >= 0; i--) {
        dst[i] = (u8)('0' + (v & 7));
        v >>= 3;
    }
    dst[n - 1] = '\0';
}

static err_t emit_header(struct writer *w, const char *path, const struct fs_node *n)
{
    u8 h[TARW_BLOCK];
    char full[FS_PATH_MAX + 2];
    usize len = 0, split = 0;
    u32 sum = 0;
    int is_dir = n->type == FS_DIR;

    /* chemin sans le "/" de tête, avec "/" final pour un dossier */
    for (const char *p = path + 1; *p; p++)
        full[len++] = *p;
    if (is_dir)
        full[len++] = '/';
    full[len] = '\0';

    if (len > 100) {                                    /* découpe en préfixe (<=155) + nom (<=100) */
        usize i;

        for (i = 1; i < len; i++)
            if (full[i] == '/' && len - i - 1 <= 100 && len - i - 1 > 0)
                break;
        if (i >= len || i > 155)
            return E_NAMETOOLONG;
        split = i;
    }

    memset(h, 0, sizeof(h));
    if (split) {
        memcpy(h + 345, full, split);                   /* préfixe */
        memcpy(h, full + split + 1, len - split - 1);   /* nom */
    } else {
        memcpy(h, full, len);
    }
    put_octal(h + 100, 8, is_dir ? 0755u : 0644u);      /* mode */
    put_octal(h + 108, 8, 0);                           /* uid */
    put_octal(h + 116, 8, 0);                           /* gid */
    put_octal(h + 124, 12, is_dir ? 0 : n->size);       /* taille */
    put_octal(h + 136, 12, 0);                          /* date : inconnue pour l'instant */
    memset(h + 148, ' ', 8);                            /* somme de contrôle : espaces pendant le calcul */
    h[156] = is_dir ? '5' : '0';
    memcpy(h + 257, "ustar", 6);                        /* "ustar" + NUL */
    h[263] = '0'; h[264] = '0';                         /* version */
    put_octal(h + 329, 8, 0);                           /* devmajor */
    put_octal(h + 337, 8, 0);                           /* devminor */

    for (usize i = 0; i < TARW_BLOCK; i++)
        sum += h[i];
    {                                                   /* format classique : 6 chiffres octaux, NUL, espace */
        u8 six[7];

        put_octal(six, 7, sum);
        memcpy(h + 148, six, 6);
        h[154] = '\0';
        h[155] = ' ';
    }
    return w->emit(h, w->ctx);
}

static err_t emit_data(struct writer *w, const struct fs_node *n)
{
    u8 block[TARW_BLOCK];

    for (u32 off = 0; off < n->size; off += TARW_BLOCK) {
        u32 chunk = n->size - off;
        err_t e;

        if (chunk > TARW_BLOCK)
            chunk = TARW_BLOCK;
        memset(block, 0, sizeof(block));
        memcpy(block, n->data + off, chunk);
        e = w->emit(block, w->ctx);
        if (e != E_OK)
            return e;
    }
    return E_OK;
}

static err_t walk(struct writer *w, const struct fs_node *dir, char *path, usize len)
{
    for (const struct fs_node *c = dir->child; c; c = c->next) {
        usize nl = strlen(c->name);
        err_t e;

        if (len + 1 + nl >= FS_PATH_MAX)
            return E_NAMETOOLONG;
        path[len] = '/';
        memcpy(path + len + 1, c->name, nl + 1);

        e = emit_header(w, path, c);
        if (e == E_OK && c->type == FS_FILE)
            e = emit_data(w, c);
        if (e == E_OK && c->type == FS_DIR)
            e = walk(w, c, path, len + 1 + nl);
        if (e != E_OK)
            return e;
    }
    return E_OK;
}

err_t tarw_write(tarw_emit_fn emit, void *ctx)
{
    struct writer w = { emit, ctx };
    char path[FS_PATH_MAX + 2];
    u8 zero[TARW_BLOCK];
    err_t e;

    path[0] = '\0';
    e = walk(&w, fs_root(), path, 0);
    if (e != E_OK)
        return e;
    memset(zero, 0, sizeof(zero));
    e = emit(zero, ctx);
    return e != E_OK ? e : emit(zero, ctx);
}
