/* PhoenOS - libmgr.c : gestionnaire de bibliotheques partagees (voir libmgr.h). */
#include "libmgr.h"
#include "aspace.h"
#include "paging.h"
#include "pmm.h"
#include "kheap.h"
#include "fs.h"
#include "dlog.h"
#include "string.h"

struct cached {
    int  used;
    u32  refs;
    struct psl_info info;
    u8  *image;                         /* copie de l'ELF (ne depend plus du fichier) */
    u32  nro;
    u32  ro_va[PSL_MAX_PAGES];          /* pages de code partagees : adresse et cadre */
    u32  ro_frame[PSL_MAX_PAGES];
};

static struct cached *cache[LIBMGR_MAX];

static int is_name_ok(const char *name)
{
    usize n = strlen(name);

    if (n == 0 || n >= 16)
        return 0;
    for (usize i = 0; i < n; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static err_t read_lib(const char *name, struct psl_info *info, const u8 **file, enum psl_status *st)
{
    char path[32];
    struct fs_node *n;
    err_t e;

    *st = PSL_BAD_HEADER;
    if (!is_name_ok(name))
        return E_NOLIB;
    strcpy(path, "/lib/");
    strcpy(path + 5, name);
    strcpy(path + 5 + strlen(name), ".psl");
    e = fs_lookup(path, &n);
    if (e != E_OK || n->type != FS_FILE)
        return E_NOENT;
    *st = psl_parse(n->data, n->size, info);
    if (*st == PSL_OK && strcmp(info->name, name) != 0)
        *st = PSL_BAD_HEADER;                           /* le nom du fichier doit etre celui de l'en-tete */
    *file = n->data;
    return *st == PSL_OK ? E_OK : E_NOLIB;
}

static int ro_index(const struct cached *c, u32 va)
{
    for (u32 i = 0; i < c->nro; i++)
        if (c->ro_va[i] == (va & ~0xFFFu))
            return (int)i;
    return -1;
}

/* Remplit les pages de code partagees : on n'ecrit QUE dans les pages de code (les donnees sont traitees par programme). */
static err_t fill_ro(void *ctx, u32 va, const u8 *src, u32 n)
{
    struct cached *c = ctx;

    while (n) {
        int k = ro_index(c, va);
        u32 off = va & 0xFFFu, take = 0x1000u - off;

        if (take > n)
            take = n;
        if (k >= 0) {
            if (src)
                memcpy((u8 *)c->ro_frame[k] + off, src, take);
            else
                memset((u8 *)c->ro_frame[k] + off, 0, take);
        }
        if (src)
            src += take;
        va += take;
        n -= take;
    }
    return E_OK;
}

static void cache_free(int i)
{
    struct cached *c = cache[i];

    for (u32 k = 0; k < c->nro; k++)
        pmm_free_frame(c->ro_frame[k]);
    kfree(c->image);
    kfree(c);
    cache[i] = NULL;
}

/* Trouve (ou charge) la bibliotheque dans le gestionnaire. */
static err_t cache_get(const char *name, int *idx)
{
    static struct psl_info info;
    const u8 *file;
    enum psl_status st;
    struct cached *c;
    int free_slot = -1;
    u32 entry;
    err_t e;

    for (int i = 0; i < LIBMGR_MAX; i++) {
        if (cache[i] && strcmp(cache[i]->info.name, name) == 0) {
            *idx = i;
            return E_OK;
        }
        if (!cache[i] && free_slot < 0)
            free_slot = i;
    }
    e = read_lib(name, &info, &file, &st);
    if (e != E_OK) {
        if (e == E_NOLIB)
            DWARN("psl", "bibliotheque %s refusee : %s", name, psl_status_text(st));
        return e == E_NOENT ? E_NOLIB : e;
    }
    if (free_slot < 0)
        return E_NOMEM;                                 /* trop de bibliotheques differentes en memoire */
    c = kmalloc(sizeof *c);
    if (!c)
        return E_NOMEM;
    memset(c, 0, sizeof *c);
    c->image = kmalloc(info.payload_len);
    if (!c->image) {
        kfree(c);
        return E_NOMEM;
    }
    memcpy(c->image, info.payload, info.payload_len);
    c->info = info;
    c->info.payload = c->image;
    for (int s = 0; s < info.nseg; s++) {
        if (info.segs[s].writable)
            continue;
        for (u32 va = info.segs[s].lo & ~0xFFFu; va < info.segs[s].hi; va += 0x1000u) {
            u32 f = pmm_alloc_frame();

            if (!f) {
                cache[free_slot] = c;
                cache_free(free_slot);
                return E_NOMEM;
            }
            memset((void *)f, 0, 0x1000u);
            c->ro_va[c->nro] = va;
            c->ro_frame[c->nro++] = f;
        }
    }
    e = elf_load_with(c->image, info.payload_len, fill_ro, c, PSL_LIB_LO, PSL_LIB_HI, &entry);
    if (e != E_OK) {
        cache[free_slot] = c;
        cache_free(free_slot);
        return E_NOLIB;
    }
    cache[free_slot] = c;
    *idx = free_slot;
    DINFO("psl", "bibliotheque %s %s chargee : %#x-%#x, %u pages de code partagees", c->info.name, c->info.version,
          c->info.lo, c->info.hi, c->nro);
    return E_OK;
}

struct priv_ctx { struct aspace *as; const struct cached *c; };

/* Donnees : on ecrit dans l'espace du programme, sauf dans les pages de code (deja la, partagees). */
static err_t fill_private(void *ctx, u32 va, const u8 *src, u32 n)
{
    struct priv_ctx *x = ctx;

    while (n) {
        u32 off = va & 0xFFFu, take = 0x1000u - off;

        if (take > n)
            take = n;
        if (ro_index(x->c, va) < 0) {
            err_t e = aspace_put(x->as, va, src, take);

            if (e != E_OK)
                return e;
        }
        if (src)
            src += take;
        va += take;
        n -= take;
    }
    return E_OK;
}

static void release(int i)
{
    if (cache[i] && cache[i]->refs && --cache[i]->refs == 0) {
        DINFO("psl", "bibliotheque %s : plus aucun programme, memoire rendue", cache[i]->info.name);
        cache_free(i);
    }
}

err_t libmgr_attach(struct uproc *p, const char *name)
{
    struct cached *c;
    struct aspace *as = p->as;
    int idx;
    err_t e;
    u32 mapped_shared = 0;

    if (!as)
        return E_NOSYS;
    e = cache_get(name, &idx);
    if (e != E_OK)
        return e;
    c = cache[idx];
    for (int i = 0; i < p->nlibs; i++) {
        if (p->libs[i] == idx)
            return E_OK;                                /* deja la */
    }
    e = E_OK;
    if (p->nlibs >= PSL_PER_PROC)
        e = E_2BIG;
    for (int i = 0; e == E_OK && i < p->nlibs; i++) {
        const struct cached *o = cache[p->libs[i]];

        if (c->info.lo < o->info.hi && o->info.lo < c->info.hi)
            e = E_BUSY;                                 /* deux bibliotheques ne peuvent pas occuper les memes adresses */
    }
    for (u32 k = 0; e == E_OK && k < c->nro; k++) {
        e = aspace_map_shared(as, c->ro_va[k], c->ro_frame[k]);
        if (e == E_OK)
            mapped_shared++;
    }
    for (int s = 0; e == E_OK && s < c->info.nseg; s++)
        if (c->info.segs[s].writable)
            e = aspace_map(as, c->info.segs[s].lo, c->info.segs[s].hi, 1);
    if (e == E_OK) {
        struct priv_ctx x = { as, c };
        u32 entry;

        e = elf_load_with(c->image, c->info.payload_len, fill_private, &x, PSL_LIB_LO, PSL_LIB_HI, &entry);
    }
    if (e != E_OK) {
        if (e != E_BUSY && e != E_2BIG)
            aspace_unmap(as, c->info.lo, c->info.hi);   /* la plage etait libre (verifiee) : on la rend entierement */
        else if (mapped_shared)
            aspace_unmap(as, c->info.lo, c->info.hi);
        if (c->refs == 0)
            cache_free(idx);
        return e;
    }
    c->refs++;
    p->libs[p->nlibs++] = (u8)idx;
    DINFO("psl", "%s utilise %s (%u utilisateur%s)", p->name, c->info.name, c->refs, c->refs > 1 ? "s" : "");
    return E_OK;
}

err_t libmgr_resolve(struct uproc *p, const char *name, const char *sym, u32 *addr)
{
    err_t e = libmgr_attach(p, name);
    int idx = -1;

    if (e != E_OK)
        return e;
    for (int i = 0; i < p->nlibs; i++)
        if (strcmp(cache[p->libs[i]]->info.name, name) == 0)
            idx = p->libs[i];
    if (idx < 0)
        return E_NOLIB;
    if (!sym) {
        *addr = cache[idx]->info.lo;
        return E_OK;
    }
    *addr = psl_find(&cache[idx]->info, sym);
    return *addr ? E_OK : E_NOENT;
}

void libmgr_detach_all(struct uproc *p)
{
    for (int i = 0; i < p->nlibs; i++)
        release(p->libs[i]);
    p->nlibs = 0;
}

int libmgr_info(int i, const char **name, u32 *refs, u32 *lo, u32 *hi, u32 *shared_pages)
{
    int seen = 0;

    for (int k = 0; k < LIBMGR_MAX; k++) {
        if (!cache[k])
            continue;
        if (seen++ == i) {
            *name = cache[k]->info.name;
            *refs = cache[k]->refs;
            *lo = cache[k]->info.lo;
            *hi = cache[k]->info.hi;
            *shared_pages = cache[k]->nro;
            return 1;
        }
    }
    return 0;
}
