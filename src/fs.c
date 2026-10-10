/*
 * PhoenOS - fs.c
 * Système de fichiers en RAM : arbre de nœuds alloués avec kmalloc.
 * Les enfants d'un dossier forment une liste chaînée triée par nom.
 */
#include "fs.h"
#include "kheap.h"
#include "string.h"

static struct fs_node *root;
static struct fs_node *cwd;
static u32 node_count;

/* ------------------------------------------------------------------ */
/* Noms et chemins                                                     */
/* ------------------------------------------------------------------ */

static int valid_name(const char *name)
{
    usize n = strlen(name);

    if (n == 0 || n > FS_NAME_MAX)
        return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    for (usize i = 0; i < n; i++)
        if (name[i] == '/' || (u8)name[i] < 32 || name[i] == 127)
            return 0;
    return 1;
}

/*
 * Extrait le prochain composant de *pp dans comp (NUL final), en sautant les '/'.
 * Renvoie 1 s'il y en a un, 0 à la fin du chemin, E_NAMETOOLONG s'il dépasse FS_NAME_MAX.
 */
static int next_component(const char **pp, char *comp)
{
    const char *p = *pp;
    usize n = 0;

    while (*p == '/')
        p++;
    if (*p == '\0') {
        *pp = p;
        return 0;
    }
    while (*p && *p != '/') {
        if (n >= FS_NAME_MAX)
            return E_NAMETOOLONG;
        comp[n++] = *p++;
    }
    comp[n] = '\0';
    *pp = p;
    return 1;
}

static struct fs_node *find_child(const struct fs_node *dir, const char *name)
{
    for (struct fs_node *c = dir->child; c; c = c->next) {
        int d = strcmp(c->name, name);

        if (d == 0)
            return c;
        if (d > 0)
            break;                              /* liste triée : on a dépassé */
    }
    return NULL;
}

static void link_child(struct fs_node *dir, struct fs_node *n)
{
    struct fs_node **pp = &dir->child;

    while (*pp && strcmp((*pp)->name, n->name) < 0)
        pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
    n->parent = dir;
}

static void unlink_child(struct fs_node *n)
{
    struct fs_node **pp = &n->parent->child;

    while (*pp && *pp != n)
        pp = &(*pp)->next;
    if (*pp)
        *pp = n->next;
    n->next = NULL;
}

/*
 * Parcourt `path`. Deux modes :
 *   leaf == NULL : renvoie l'élément désigné par tout le chemin.
 *   leaf != NULL : s'arrête avant le DERNIER composant : *out = dossier parent, leaf = dernier nom.
 *                  E_INVAL si le chemin n'a pas de dernier nom utilisable ("/", "a/..", "").
 */
static err_t walk(const char *path, struct fs_node **out, char *leaf)
{
    struct fs_node *cur;
    const char *p = path;
    char comp[FS_NAME_MAX + 1];

    if (!root)
        return E_NOENT;
    if (!path || !*path)
        return E_INVAL;
    if (strlen(path) >= FS_PATH_MAX)
        return E_NAMETOOLONG;

    cur = (*path == '/') ? root : cwd;

    for (;;) {
        int r = next_component(&p, comp);
        int last;
        const char *look = p;

        if (r < 0)
            return r;
        if (r == 0) {
            if (leaf)
                return E_INVAL;
            *out = cur;
            return E_OK;
        }

        while (*look == '/')
            look++;
        last = (*look == '\0');

        if (leaf && last) {
            if (strcmp(comp, ".") == 0 || strcmp(comp, "..") == 0)
                return E_INVAL;
            memcpy(leaf, comp, strlen(comp) + 1);
            *out = cur;
            return E_OK;
        }

        if (strcmp(comp, ".") == 0)
            continue;
        if (strcmp(comp, "..") == 0) {
            if (cur->parent)
                cur = cur->parent;
            continue;
        }
        if (cur->type != FS_DIR)
            return E_NOTDIR;
        cur = find_child(cur, comp);
        if (!cur)
            return E_NOENT;
    }
}

/* ------------------------------------------------------------------ */
/* Création et destruction de nœuds                                    */
/* ------------------------------------------------------------------ */

static err_t new_node(struct fs_node *parent, const char *name, enum fs_type type, struct fs_node **out)
{
    struct fs_node *n;

    if (!valid_name(name))
        return strlen(name) > FS_NAME_MAX ? E_NAMETOOLONG : E_INVAL;
    if (parent->type != FS_DIR)
        return E_NOTDIR;
    if (find_child(parent, name))
        return E_EXIST;
    if (node_count >= FS_NODES_MAX)
        return E_NOMEM;
    if (parent->depth + 1 > FS_DEPTH_MAX)
        return E_NAMETOOLONG;

    n = kzalloc(sizeof(*n));
    if (!n)
        return E_NOMEM;
    memcpy(n->name, name, strlen(name) + 1);
    n->type  = (u8)type;
    n->depth = (u8)(parent->depth + 1);
    link_child(parent, n);
    node_count++;
    *out = n;
    return E_OK;
}

static void free_subtree(struct fs_node *n)
{
    struct fs_node *c = n->child;

    while (c) {
        struct fs_node *next = c->next;

        free_subtree(c);
        c = next;
    }
    kfree(n->data);
    kfree(n);
    node_count--;
}

void fs_init(void)
{
    if (root) {
        while (root->child) {
            struct fs_node *c = root->child;

            unlink_child(c);
            free_subtree(c);
        }
        kfree(root);
        node_count--;
    }

    root = kzalloc(sizeof(*root));
    cwd = root;
    node_count = 0;
    if (root) {
        root->type = FS_DIR;
        node_count = 1;
    }
}

struct fs_node *fs_root(void) { return root; }
struct fs_node *fs_cwd(void)  { return cwd; }

/* ------------------------------------------------------------------ */
/* Opérations                                                          */
/* ------------------------------------------------------------------ */

err_t fs_lookup(const char *path, struct fs_node **out)
{
    return walk(path, out, NULL);
}

err_t fs_chdir(const char *path)
{
    struct fs_node *n;
    err_t e = walk(path, &n, NULL);

    if (e != E_OK)
        return e;
    if (n->type != FS_DIR)
        return E_NOTDIR;
    cwd = n;
    return E_OK;
}

err_t fs_mkdir(const char *path)
{
    struct fs_node *parent, *n;
    char leaf[FS_NAME_MAX + 1];
    err_t e = walk(path, &parent, leaf);

    if (e != E_OK)
        return e;
    return new_node(parent, leaf, FS_DIR, &n);
}

err_t fs_mkdir_p(const char *path)
{
    struct fs_node *cur;
    const char *p = path;
    char comp[FS_NAME_MAX + 1];

    if (!root)
        return E_NOENT;
    if (!path || !*path)
        return E_INVAL;
    if (strlen(path) >= FS_PATH_MAX)
        return E_NAMETOOLONG;

    cur = (*path == '/') ? root : cwd;

    for (;;) {
        int r = next_component(&p, comp);
        struct fs_node *child;

        if (r < 0)
            return r;
        if (r == 0)
            return E_OK;
        if (strcmp(comp, ".") == 0)
            continue;
        if (strcmp(comp, "..") == 0) {
            if (cur->parent)
                cur = cur->parent;
            continue;
        }
        if (cur->type != FS_DIR)
            return E_NOTDIR;

        child = find_child(cur, comp);
        if (!child) {
            err_t e = new_node(cur, comp, FS_DIR, &child);

            if (e != E_OK)
                return e;
        } else if (child->type != FS_DIR) {
            return E_NOTDIR;
        }
        cur = child;
    }
}

err_t fs_create(const char *path, struct fs_node **out)
{
    struct fs_node *parent, *n;
    char leaf[FS_NAME_MAX + 1];
    err_t e = walk(path, &parent, leaf);

    if (e != E_OK)
        return e;
    if (parent->type != FS_DIR)
        return E_NOTDIR;

    n = find_child(parent, leaf);
    if (n) {
        if (n->type == FS_DIR)
            return E_ISDIR;
        *out = n;
        return E_OK;
    }
    return new_node(parent, leaf, FS_FILE, out);
}

err_t fs_write(struct fs_node *file, const void *data, u32 len)
{
    u8 *buf = NULL;

    if (!file || file->type != FS_FILE)
        return E_ISDIR;
    if (len > FS_FILE_MAX)
        return E_FBIG;
    if (len) {
        buf = kmalloc(len);
        if (!buf)
            return E_NOMEM;
        memcpy(buf, data, len);                 /* avant kfree : `data` peut pointer dans l'ancien contenu */
    }
    kfree(file->data);
    file->data = buf;
    file->size = len;
    file->cap  = len;
    return E_OK;
}

err_t fs_append(struct fs_node *file, const void *data, u32 len)
{
    if (!file || file->type != FS_FILE)
        return E_ISDIR;
    if (len > FS_FILE_MAX - file->size)
        return E_FBIG;
    if (len == 0)
        return E_OK;

    if (file->size + len > file->cap) {
        u32 newcap = file->cap ? file->cap * 2 : 64;
        u8 *buf;

        while (newcap < file->size + len)
            newcap *= 2;
        if (newcap > FS_FILE_MAX)
            newcap = FS_FILE_MAX;

        buf = kmalloc(newcap);
        if (!buf)
            return E_NOMEM;
        if (file->size)
            memcpy(buf, file->data, file->size);
        memcpy(buf + file->size, data, len);    /* avant kfree : `data` peut pointer dans l'ancien contenu */
        kfree(file->data);
        file->data = buf;
        file->cap = newcap;
    } else {
        memmove(file->data + file->size, data, len);
    }
    file->size += len;
    return E_OK;
}

/* Vrai si `anc` est `n` ou l'un de ses ancêtres. */
static int is_ancestor_or_self(const struct fs_node *anc, const struct fs_node *n)
{
    for (; n; n = n->parent)
        if (n == anc)
            return 1;
    return 0;
}

err_t fs_remove(const char *path, int flags)
{
    struct fs_node *n;
    err_t e = walk(path, &n, NULL);

    if (e != E_OK)
        return e;
    if (n == root || is_ancestor_or_self(n, cwd))
        return E_BUSY;                          /* ne jamais supprimer le dossier où l'on se trouve */
    if (n->type == FS_DIR) {
        if (!(flags & FS_RM_DIR_OK))
            return E_ISDIR;
        if (n->child && !(flags & FS_RM_RECURSIVE))
            return E_NOTEMPTY;
    }
    unlink_child(n);
    free_subtree(n);
    return E_OK;
}

static u32 subtree_height(const struct fs_node *n)
{
    u32 h = 0;

    for (const struct fs_node *c = n->child; c; c = c->next) {
        u32 ch = 1 + subtree_height(c);

        if (ch > h)
            h = ch;
    }
    return h;
}

static void set_depth(struct fs_node *n, u32 depth)
{
    n->depth = (u8)depth;
    for (struct fs_node *c = n->child; c; c = c->next)
        set_depth(c, depth + 1);
}

/* Détermine le dossier cible et le nom final pour une destination (mv / cp). */
static err_t resolve_dest(const char *dst, const char *src_name, struct fs_node **dir,
                          char *name, struct fs_node **existing)
{
    struct fs_node *n;
    err_t e = walk(dst, &n, NULL);

    *existing = NULL;
    if (e == E_OK) {
        if (n->type == FS_DIR) {                /* destination = dossier : on y met l'élément */
            *dir = n;
            memcpy(name, src_name, strlen(src_name) + 1);
            *existing = find_child(n, name);
        } else {                                /* destination = fichier existant */
            *dir = n->parent;
            memcpy(name, n->name, strlen(n->name) + 1);
            *existing = n;
        }
        return E_OK;
    }
    if (e != E_NOENT)
        return e;
    return walk(dst, dir, name);                /* destination qui n'existe pas encore */
}

err_t fs_rename(const char *src, const char *dst)
{
    struct fs_node *n, *dir, *existing;
    char name[FS_NAME_MAX + 1];
    err_t e = walk(src, &n, NULL);

    if (e != E_OK)
        return e;
    if (n == root)
        return E_BUSY;

    e = resolve_dest(dst, n->name, &dir, name, &existing);
    if (e != E_OK)
        return e;
    if (!valid_name(name))
        return E_INVAL;
    if (existing == n)
        return E_OK;                            /* déplacer sur soi-même : rien à faire */
    if (existing)
        return E_EXIST;
    if (n->type == FS_DIR && is_ancestor_or_self(n, dir))
        return E_INVAL;                         /* un dossier ne peut pas entrer dans lui-même */
    if (dir->depth + 1 + subtree_height(n) > FS_DEPTH_MAX)
        return E_NAMETOOLONG;

    unlink_child(n);
    memcpy(n->name, name, strlen(name) + 1);
    link_child(dir, n);
    set_depth(n, dir->depth + 1u);
    return E_OK;
}

err_t fs_copy(const char *src, const char *dst)
{
    struct fs_node *s, *dir, *existing, *d;
    char name[FS_NAME_MAX + 1];
    int created = 0;
    err_t e = walk(src, &s, NULL);

    if (e != E_OK)
        return e;
    if (s->type != FS_FILE)
        return E_ISDIR;

    e = resolve_dest(dst, s->name, &dir, name, &existing);
    if (e != E_OK)
        return e;

    if (existing) {
        if (existing == s)
            return E_INVAL;                     /* copier un fichier sur lui-même */
        if (existing->type == FS_DIR)
            return E_ISDIR;
        d = existing;
    } else {
        e = new_node(dir, name, FS_FILE, &d);
        if (e != E_OK)
            return e;
        created = 1;
    }

    e = fs_write(d, s->data, s->size);
    if (e != E_OK && created) {                 /* on ne laisse pas un fichier vide en cas d'échec */
        unlink_child(d);
        free_subtree(d);
    }
    return e;
}

err_t fs_path_of(const struct fs_node *n, char *buf, usize size)
{
    const struct fs_node *chain[FS_DEPTH_MAX + 2];
    usize depth = 0, pos = 0;

    if (!n || size < 2)
        return E_NAMETOOLONG;
    for (const struct fs_node *c = n; c && c->parent; c = c->parent) {
        if (depth >= FS_DEPTH_MAX + 1)
            return E_NAMETOOLONG;
        chain[depth++] = c;
    }

    if (depth == 0) {                           /* racine */
        buf[0] = '/';
        buf[1] = '\0';
        return E_OK;
    }
    while (depth--) {
        usize len = strlen(chain[depth]->name);

        if (pos + 1 + len + 1 > size)
            return E_NAMETOOLONG;
        buf[pos++] = '/';
        memcpy(buf + pos, chain[depth]->name, len);
        pos += len;
    }
    buf[pos] = '\0';
    return E_OK;
}

static void count_nodes(const struct fs_node *n, struct fs_stats *s)
{
    if (n->type == FS_DIR) {
        s->dirs++;
        for (const struct fs_node *c = n->child; c; c = c->next)
            count_nodes(c, s);
    } else {
        s->files++;
        s->bytes += n->size;
    }
}

void fs_get_stats(struct fs_stats *out)
{
    memset(out, 0, sizeof(*out));
    if (root)
        count_nodes(root, out);
}
