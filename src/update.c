/* PhoenOS - update.c : mises a jour signees (etape 0, commande maj, tache de fond). Voir update.h. */
#include "update.h"
#include "updconf.h"
#include "updkey.h"
#include "updstore.h"
#include "updman.h"
#include "updboot.h"
#include "httpc.h"
#include "tlssock.h"
#include "crypto.h"
#include "multiboot.h"
#include "version.h"
#include "kprintf.h"
#include "dlog.h"
#include "ata.h"
#include "net.h"
#include "sched.h"
#include "string.h"
#include "persist.h"
#include "power.h"
#include "pit.h"

#define UPD_OWNER 0x00FD0001u           /* "proprietaire" du noyau pour les connexions TLS et TCP (aucun programme n'a ce numero) */

extern char kernel_end[];
extern char relaunch_stub_start[], relaunch_stub_end[];

static int g_relaunched;
static int g_busy;
static char g_notice[112];
static int g_notice_new;
static char g_why[128];

void update_reset(void)
{
    g_relaunched = 0; g_busy = 0; g_notice[0] = 0; g_notice_new = 0; g_why[0] = 0;
}

int update_relaunched(void) { return g_relaunched; }

const char *update_take_notice(void)
{
    if (!g_notice_new)
        return NULL;
    g_notice_new = 0;
    return g_notice;
}

/* ------------------------------------------------------------------ disque */
static err_t a_rd(void *c, u32 lba, u32 n, void *buf) { return ata_read((const struct ata_dev *)c, lba, n, buf); }
static err_t a_wr(void *c, u32 lba, u32 n, const void *buf) { return ata_write((const struct ata_dev *)c, lba, n, buf); }
static err_t a_fl(void *c) { return ata_flush((const struct ata_dev *)c); }

static struct upd_blk g_blk;

static const struct upd_blk *get_blk(void)
{
    const struct ata_dev *d = ata_first_disk();

    if (!d)
        return NULL;
    g_blk.sectors = d->sectors;
    g_blk.ctx = (void *)d;
    g_blk.rd = a_rd;
    g_blk.wr = a_wr;
    g_blk.flush = a_fl;
    return &g_blk;
}

static const u8 *pubkey(void)
{
#if UPDATE_KEY_PRESENT
    return UPDATE_PUBKEY;
#else
    return NULL;
#endif
}

static void fingerprint(char out[17])
{
    static const char hx[] = "0123456789abcdef";
    u8 d[32];

    if (!pubkey()) { memcpy(out, "(aucune)", 9); return; }
    sha256(d, pubkey(), 32);
    for (int i = 0; i < 8; i++) { out[i * 2] = hx[d[i] >> 4]; out[i * 2 + 1] = hx[d[i] & 15]; }
    out[16] = 0;
}

/* ------------------------------------------------------------------ etape 0 */
static void real_jump(void *stub, const struct relaunch_args *a)
{
    ((void (*)(const struct relaunch_args *))stub)(a);          /* le code de copie ne revient jamais */
}

static int name_is_relaunch(const struct multiboot_info *mbi)
{
    const u8 *raw = (const u8 *)mbi;
    u32 name;

    if (!(mbi->flags & (1u << 9)))
        return 0;
    name = (u32)raw[64] | ((u32)raw[65] << 8) | ((u32)raw[66] << 16) | ((u32)raw[67] << 24);   /* boot_loader_name */
    if (!name)
        return 0;
    return strcmp((const char *)(usize)name, UPD_RELAUNCH_NAME) == 0;
}

void update_stage0(const struct multiboot_info *mbi)
{
    struct upd_boot_env env;
    struct upd_boot_plan plan;
    const struct upd_blk *b;
    struct upd_ctl c;
    int rc;

    g_relaunched = name_is_relaunch(mbi);
#ifdef PHOENOS_X64
    return;                                     /* perf64 0.11.0 : pas encore de relance (arrive en 0.11.1) : on reste sur la version du CD */
#endif
    if (g_relaunched)
        kprintf("       Mises a jour : version installee, lancee par l'etape 0 (build %u)\n", PHOENOS_BUILD);
    if (!pubkey() || g_relaunched)
        return;
    ata_init();
    b = get_blk();
    if (!b)
        return;
    env.blk = b;
    env.pubkey = pubkey();
    env.running_build = PHOENOS_BUILD;
    env.running_edition = PHOENOS_EDITION;
    env.relaunched = 0;
    rc = upd_boot_select(&env, &plan);
    if (plan.rolled_back)
        kprintf("[WARN] Mises a jour : retour arriere effectue (%s)\n", plan.why);
    DINFO("maj", "etape 0 : %s", plan.why);
    if (rc != UPB_RELAUNCH)
        return;
    kprintf("       Mises a jour : lancement de la version %s (%s)...\n", plan.m.version, plan.why);
    rc = upd_relaunch(b, &plan, (u32)(usize)mbi, (u32)(usize)kernel_end, relaunch_stub_start,
                      (u32)(relaunch_stub_end - relaunch_stub_start), real_jump);
    /* on n'arrive ici que si la preparation a echoue avant de toucher au noyau actuel */
    kprintf("[FAIL] Mises a jour : %s : on garde la version du CD\n", upd_last_failure);
    DERROR("maj", "relance impossible (%d) : %s", rc, upd_last_failure);
    if (upd_ctl_load(b, &c) == E_OK) {
        if (rc == UPB_E_HASH || rc == UPB_E_IMAGE) {
            (void)upd_rollback(b, &c, pubkey(), PHOENOS_EDITION, NULL);
        } else if (c.tries) {
            c.tries--;                                  /* panne passagere (disque, memoire) : cet essai ne compte pas */
            (void)upd_ctl_store(b, &c);
        }
    }
}

/* ------------------------------------------------------------------ reseau */
static int n_open(void *ctx, const char *host, u16 port)
{
    u32 ip;
    err_t e;
    int h;

    e = net_resolve(host, &ip);
    if (e != E_OK)
        return e;
    e = tlss_connect(UPD_OWNER, host, ip, port, &h);
    if (e != E_OK)
        return e;
    *(int *)ctx = h;
    return 0;
}
static int n_send(void *ctx, const u8 *d, u32 n) { return tlss_send(UPD_OWNER, *(int *)ctx, d, n); }
static int n_recv(void *ctx, u8 *b, u32 cap) { return tlss_recv(UPD_OWNER, *(int *)ctx, b, cap); }
static void n_close(void *ctx) { (void)tlss_close(UPD_OWNER, *(int *)ctx); }

static int g_conn;
static struct http_net g_net = { &g_conn, n_open, n_send, n_recv, n_close };

/* Texte d'erreur d'un telechargement dans g_why. */
static void explain_http(int rc, const struct http_result *r)
{
    const char *detail = "";

    if (rc == HTTP_E_OPEN) {
        if (r->net_err == E_CERT || r->net_err == E_TLS)
            detail = tlss_last_error();
        else if (r->net_err == E_BUSY)
            detail = "la connexion securisee est utilisee par un programme";
        else
            detail = err_str(r->net_err);
    } else if (rc == HTTP_E_STATUS) {
        ksnprintf(g_why, sizeof g_why, "le serveur repond %d (version ou fichier absent de la release ?)", r->status);
        return;
    }
    ksnprintf(g_why, sizeof g_why, "%s%s%s", http_strerror(rc), *detail ? " : " : "", detail);
}

struct memsink { u8 *buf; u32 cap, len; };
static int sink_mem(void *c, const u8 *d, u32 n)
{
    struct memsink *m = c;

    if (n > m->cap - m->len)
        return 1;
    memcpy(m->buf + m->len, d, n);
    m->len += n;
    return 0;
}

/* ------------------------------------------------------------------ verification */
struct offer {
    struct upd_manifest m;
    u8 sig[64];
    char text[UPD_MAN_MAX + 1];
    u32 len;
};
static struct offer g_offer;

/* 0 : mise a jour plus recente disponible (g_offer) ; 1 : deja a jour ; < 0 : erreur (g_why) */
static int fetch_offer(const struct upd_ctl *ctl)
{
    static char path[160];
    struct memsink ms;
    struct http_result r;
    int rc;

    if (!pubkey()) {
        ksnprintf(g_why, sizeof g_why, "aucune cle de signature dans ce PhoenOS (tools/phoenos_sign.py keygen) : mises a jour desactivees");
        return -1;
    }
    ksnprintf(path, sizeof path, "/%s/releases/latest/download/manifest-%s.txt", UPDATE_REPO, PHOENOS_EDITION);
    ms.buf = (u8 *)g_offer.text; ms.cap = UPD_MAN_MAX; ms.len = 0;
    rc = http_get(&g_net, UPDATE_HOST, path, sink_mem, &ms, UPD_MAN_MAX, &r);
    if (rc != HTTP_OK) {
        if (rc == HTTP_E_STATUS && r.status == 404)
            ksnprintf(g_why, sizeof g_why, "pas de mise a jour publiee pour l'edition %s", PHOENOS_EDITION);
        else
            explain_http(rc, &r);
        return -2;
    }
    g_offer.len = ms.len;
    g_offer.text[ms.len] = 0;
    ksnprintf(path, sizeof path, "/%s/releases/latest/download/manifest-%s.txt.sig", UPDATE_REPO, PHOENOS_EDITION);
    ms.buf = g_offer.sig; ms.cap = 64; ms.len = 0;
    rc = http_get(&g_net, UPDATE_HOST, path, sink_mem, &ms, 64, &r);
    if (rc != HTTP_OK || ms.len != 64) {
        if (rc == HTTP_OK)
            ksnprintf(g_why, sizeof g_why, "signature de taille incorrecte (%u octets)", ms.len);
        else
            explain_http(rc, &r);
        return -3;
    }
    if (!upd_man_verify(pubkey(), g_offer.text, g_offer.len, g_offer.sig)) {
        ksnprintf(g_why, sizeof g_why, "SIGNATURE INVALIDE : le manifeste n'a pas ete signe avec votre cle. Mise a jour refusee");
        return -4;
    }
    rc = upd_man_parse(g_offer.text, g_offer.len, &g_offer.m);
    if (rc != UPD_M_OK) {
        ksnprintf(g_why, sizeof g_why, "%s", upd_man_strerror(rc));
        return -5;
    }
    if (strcmp(g_offer.m.edition, PHOENOS_EDITION) != 0) {
        ksnprintf(g_why, sizeof g_why, "le manifeste est pour l'edition %s, pas %s", g_offer.m.edition, PHOENOS_EDITION);
        return -6;
    }
    if (ctl && g_offer.m.build < ctl->min_build) {
        ksnprintf(g_why, sizeof g_why, "version %s plus ancienne que la derniere confirmee : refusee (protection contre le retour en arriere)",
                  g_offer.m.version);
        return -7;
    }
    return g_offer.m.build > PHOENOS_BUILD ? 0 : 1;
}

/* ------------------------------------------------------------------ telechargement vers un emplacement */
struct dl {
    const struct upd_blk *b;
    int slot;
    u32 region, region_max;     /* secteur de depart (relatif) et nombre maximal de secteurs */
    u32 expect;                 /* taille attendue */
    u32 got;
    u32 sectors;                /* deja ecrits */
    u32 fill;
    u32 edition_id;
    int is_kernel;
    u32 hgot;
    u8 head[32];
    err_t err;
    sha256_ctx h;
    u8 buf[4096];
};
static struct dl g_dl;

static err_t dl_flush(struct dl *d, int final)
{
    u32 n = (d->fill + UPD_SECTOR - 1) / UPD_SECTOR;
    err_t e;

    if (!n)
        return E_OK;
    memset(d->buf + d->fill, 0, n * UPD_SECTOR - d->fill);
    if (d->sectors + n > d->region_max)
        return E_FBIG;
    e = upd_slot_write(d->b, d->slot, d->region + d->sectors, n, d->buf);
    if (e != E_OK)
        return e;
    d->sectors += n;
    d->fill = 0;
    (void)final;
    return E_OK;
}

static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static int sink_dl(void *c, const u8 *p, u32 n)
{
    struct dl *d = c;

    if (n > d->expect - d->got) {
        d->err = E_FBIG;                                /* plus que la taille annoncee dans le manifeste signe */
        return 1;
    }
    sha256_update(&d->h, p, n);
    if (d->is_kernel && d->hgot < 32) {                 /* en-tete PBI1 : verifie des qu'il est complet, avant de continuer */
        u32 k = 32 - d->hgot < n ? 32 - d->hgot : n;

        memcpy(d->head + d->hgot, p, k);
        d->hgot += k;
        if (d->hgot == 32 &&
            (d->head[0] != 'P' || d->head[1] != 'B' || d->head[2] != 'I' || d->head[3] != '1' || le32(d->head + 4) != 1 ||
             le32(d->head + 24) != d->edition_id || le32(d->head + 16) + 32 != d->expect)) {
            d->err = E_NOEXEC;
            return 1;
        }
    }
    while (n) {
        u32 take = (u32)sizeof d->buf - d->fill;

        if (take > n)
            take = n;
        memcpy(d->buf + d->fill, p, take);
        d->fill += take; p += take; n -= take; d->got += take;
        if (d->fill == sizeof d->buf) {
            d->err = dl_flush(d, 0);
            if (d->err != E_OK)
                return 1;
            if ((d->sectors & 63u) == 0)
                kprintf(".");
        }
    }
    return 0;
}

/* Telecharge un fichier du manifeste dans une region de l'emplacement. 0 ou code d'erreur negatif (g_why). */
static int download(const struct upd_blk *b, int slot, const char *version, const char *name, u32 size,
                    const u8 want[32], int is_kernel)
{
    static char path[200];
    struct http_result r;
    u8 got[32];
    int rc;

    memset(&g_dl, 0, sizeof g_dl);
    g_dl.b = b; g_dl.slot = slot;
    g_dl.region = is_kernel ? UPD_KERNEL_OFF : UPD_INITRD_OFF;
    g_dl.region_max = is_kernel ? UPD_KERNEL_MAX : UPD_INITRD_MAX;
    g_dl.expect = size;
    g_dl.edition_id = PHOENOS_EDITION_ID;
    g_dl.is_kernel = is_kernel;
    sha256_init(&g_dl.h);
    ksnprintf(path, sizeof path, "/%s/releases/download/%s/%s", UPDATE_REPO, version, name);
    kprintf("       %s (%u Kio) ", name, (size + 1023) / 1024);
    rc = http_get(&g_net, UPDATE_HOST, path, sink_dl, &g_dl, size, &r);
    kprintf("\n");
    if (rc == HTTP_E_SINK || (rc == HTTP_E_BIG)) {
        ksnprintf(g_why, sizeof g_why, "%s : %s", name, g_dl.err == E_NOEXEC ? "image du noyau invalide" :
                  g_dl.err == E_FBIG ? "fichier plus gros que dans le manifeste" : err_str(g_dl.err ? g_dl.err : E_IO));
        return -1;
    }
    if (rc != HTTP_OK) {
        explain_http(rc, &r);
        return -2;
    }
    if (g_dl.got != size) {
        ksnprintf(g_why, sizeof g_why, "%s : %u octets recus au lieu de %u", name, g_dl.got, size);
        return -3;
    }
    if (dl_flush(&g_dl, 1) != E_OK) {
        ksnprintf(g_why, sizeof g_why, "ecriture sur le disque impossible");
        return -4;
    }
    sha256_final(&g_dl.h, got);
    if (!crypto_memeq(got, want, 32)) {
        ksnprintf(g_why, sizeof g_why, "%s : empreinte SHA-256 incorrecte (fichier altere ou autre version)", name);
        return -5;
    }
    return 0;
}

static err_t load_ctl(const struct upd_blk **bp, struct upd_ctl *c)
{
    const struct upd_blk *b = get_blk();
    err_t e;

    if (!b) { ksnprintf(g_why, sizeof g_why, "aucun disque : les mises a jour ont besoin d'un disque (voir disks)"); return E_NOENT; }
    e = upd_area_check(b);
    if (e == E_NOSYS) { ksnprintf(g_why, sizeof g_why, "disque trop petit (%u Mio) : 32 Mio au moins (make newdisk)", b->sectors / 2048); return e; }
    e = upd_ctl_load(b, c);
    if (e == E_NOENT) { ksnprintf(g_why, sizeof g_why, "zone de mise a jour non preparee : tapez  maj preparer"); return e; }
    if (e != E_OK) { ksnprintf(g_why, sizeof g_why, "etat des mises a jour illisible (maj preparer force)"); return e; }
    *bp = b;
    return E_OK;
}

/* Installe : verifie, telecharge dans l'emplacement inactif, puis bascule. */
static err_t do_install(void)
{
    const struct upd_blk *b;
    struct upd_ctl c;
    int rc, target;
    err_t e;

    e = load_ctl(&b, &c);
    if (e != E_OK)
        return e;
    if (c.trial && g_relaunched) {
        ksnprintf(g_why, sizeof g_why, "la version actuelle est encore a l'essai : attendez %u s apres le demarrage", UPDATE_CONFIRM_S);
        return E_BUSY;
    }
    rc = fetch_offer(&c);
    if (rc < 0)
        return E_IO;
    if (rc == 1) {
        ksnprintf(g_why, sizeof g_why, "deja a jour (version %s)", PHOENOS_VERSION);
        return E_EXIST;
    }
    if (c.active >= 0) {                                /* la version proposee est-elle deja dans l'emplacement actif ? */
        struct upd_slot as;
        struct upd_manifest am;

        if (upd_slot_verify(b, c.active, pubkey(), PHOENOS_EDITION, &as, &am) == 0 && as.build >= g_offer.m.build) {
            ksnprintf(g_why, sizeof g_why, "PhoenOS %s est deja installe : tapez reboot pour l'essayer", am.version);
            return E_EXIST;
        }
    }
    target = (c.active == 0) ? 1 : 0;
    kprintf("  Installation de PhoenOS %s dans l'emplacement %c...\n", g_offer.m.version, 'A' + target);
    e = upd_slot_invalidate(b, target);                 /* tant que ce n'est pas fini, cet emplacement ne compte pas */
    if (e != E_OK) { ksnprintf(g_why, sizeof g_why, "ecriture sur le disque impossible"); return e; }
    if (download(b, target, g_offer.m.version, g_offer.m.kname, g_offer.m.ksize, g_offer.m.khash, 1) != 0)
        return E_IO;
    if (download(b, target, g_offer.m.version, g_offer.m.iname, g_offer.m.isize, g_offer.m.ihash, 0) != 0)
        return E_IO;
    e = upd_slot_commit(b, target, g_offer.m.build, g_offer.m.ksize, g_offer.m.isize, g_offer.text, g_offer.len, g_offer.sig);
    if (e != E_OK) { ksnprintf(g_why, sizeof g_why, "ecriture sur le disque impossible"); return e; }
    c.active = target;
    c.trial = 1;
    c.tries = 0;
    e = upd_ctl_store(b, &c);                           /* DERNIERE ecriture : c'est elle qui bascule */
    if (e != E_OK) { ksnprintf(g_why, sizeof g_why, "ecriture sur le disque impossible"); return e; }
    DINFO("maj", "version %s installee dans l'emplacement %c", g_offer.m.version, 'A' + target);
    return E_OK;
}

/* ------------------------------------------------------------------ commande maj */
static void print_slots(const struct upd_blk *b, const struct upd_ctl *c)
{
    for (int s = 0; s < 2; s++) {
        struct upd_slot us;
        struct upd_manifest m;
        int v = upd_slot_verify(b, s, pubkey(), PHOENOS_EDITION, &us, &m);

        kprintf("     emplacement %c : ", 'A' + s);
        if (v == 0)
            kprintf("PhoenOS %s (build %u)%s%s\n", m.version, m.build, c->active == s ? "  <- actif" : "",
                    (c->active == s && c->trial) ? " (a l'essai)" : "");
        else if (upd_slot_load(b, s, &us) == E_OK)
            kprintf("contenu non valide (signature ou edition refusee)\n");
        else
            kprintf("vide\n");
    }
}

static err_t cmd_status(void)
{
    const struct upd_blk *b = get_blk();
    struct upd_ctl c;
    char fp[17];
    err_t e;

    fingerprint(fp);
    kprintf("  PhoenOS %s - edition %s - build %u%s\n", PHOENOS_VERSION, PHOENOS_EDITION, PHOENOS_BUILD,
            g_relaunched ? " (version installee sur le disque)" : " (version du CD)");
    kprintf("  Depot : %s - cle de signature : %s\n", UPDATE_REPO, fp);
    if (!pubkey())
        kprintf("  Pas de cle de signature : mises a jour desactivees (python3 tools/phoenos_sign.py keygen, puis make iso).\n");
    if (!b) { kprintf("  Pas de disque.\n"); return E_OK; }
    e = upd_area_check(b);
    if (e != E_OK) { kprintf("  Disque trop petit (%u Mio) : 32 Mio au moins.\n", b->sectors / 2048); return E_OK; }
    e = upd_ctl_load(b, &c);
    if (e == E_NOENT) { kprintf("  Zone de mise a jour non preparee : tapez  maj preparer\n"); return E_OK; }
    if (e != E_OK) { kprintf("  Etat illisible (maj preparer force pour repartir de zero).\n"); return E_OK; }
    kprintf("  Actif : %s - essais : %u - plus haut build confirme : %u - mode auto : %s\n",
            c.active < 0 ? "version du CD" : (c.active == 0 ? "emplacement A" : "emplacement B"), c.tries, c.min_build,
            c.autom == 0 ? "non" : (c.autom == 1 ? "verifier" : "installer"));
    print_slots(b, &c);
    return E_OK;
}

static err_t cmd_verify(void)
{
    const struct upd_blk *b = get_blk();
    struct upd_ctl c;
    int have = b && upd_ctl_load(b, &c) == E_OK;
    int rc = fetch_offer(have ? &c : NULL);

    if (rc < 0) {
        kprintf("maj: %s\n", g_why);
        return E_REPORTED;
    }
    if (rc == 1) {
        kprintf("  Deja a jour : PhoenOS %s (la derniere version publiee est la %s).\n", PHOENOS_VERSION, g_offer.m.version);
        return E_OK;
    }
    kprintf("  Mise a jour disponible : PhoenOS %s (vous avez %s). Signature valide.\n", g_offer.m.version, PHOENOS_VERSION);
    if (g_offer.m.notes[0])
        kprintf("  %s\n", g_offer.m.notes);
    kprintf("  Tapez  maj installer  pour l'installer.\n");
    return E_OK;
}

static err_t cmd_install(void)
{
    err_t e = do_install();

    if (e == E_EXIST) {
        kprintf("  %s\n", g_why);
        return E_OK;
    }
    if (e != E_OK) {
        kprintf("maj: %s\n", g_why);
        kprintf("maj: rien n'a ete modifie : la version actuelle reste active.\n");
        return E_REPORTED;
    }
    kprintf("  PhoenOS %s est installe. Il sera essaye au prochain demarrage : tapez  reboot.\n", g_offer.m.version);
    kprintf("  Si cette version ne tient pas %u s, PhoenOS revient tout seul a la precedente.\n", UPDATE_CONFIRM_S);
    return E_OK;
}

static err_t cmd_back(void)
{
    const struct upd_blk *b;
    struct upd_ctl c;
    int now;
    err_t e = load_ctl(&b, &c);

    if (e != E_OK) { kprintf("maj: %s\n", g_why); return E_REPORTED; }
    if (c.active < 0) { kprintf("  Rien a annuler : aucune version installee n'est active (c'est celle du CD).\n"); return E_OK; }
    e = upd_rollback(b, &c, pubkey(), PHOENOS_EDITION, &now);
    if (e != E_OK) { kprintf("maj: ecriture sur le disque impossible (%s)\n", err_str(e)); return E_REPORTED; }
    kprintf("  Retour arriere fait : %s. Tapez  reboot  pour l'appliquer.\n",
            now < 0 ? "version du CD" : (now == 0 ? "emplacement A" : "emplacement B"));
    return E_OK;
}

static err_t cmd_prepare(int force)
{
    const struct upd_blk *b = get_blk();
    err_t e;

    if (!b) { kprintf("maj: aucun disque.\n"); return E_REPORTED; }
    if (upd_area_check(b) == E_NOSYS) { kprintf("maj: disque trop petit (%u Mio) : 32 Mio au moins (make newdisk).\n", b->sectors / 2048); return E_REPORTED; }
    if (persist_state() != PERSIST_READY) {
        kprintf("maj: le disque n'est pas un disque PhoenOS pret (voir disks, format) : zone de mise a jour non creee.\n");
        return E_REPORTED;
    }
    e = upd_area_prepare(b, force);
    if (e == E_BUSY) {
        kprintf("maj: la zone (secteurs %u a %u) contient deja autre chose : non touchee (maj preparer force pour l'ecraser).\n",
                UPD_BASE_LBA, UPD_BASE_LBA + UPD_AREA_SECTORS - 1);
        return E_REPORTED;
    }
    if (e != E_OK) { kprintf("maj: preparation impossible (%s)\n", err_str(e)); return E_REPORTED; }
    kprintf("  Zone de mise a jour prete (16 Mio, deux emplacements A et B).\n");
    return E_OK;
}

static err_t cmd_auto(const char *arg)
{
    const struct upd_blk *b;
    struct upd_ctl c;
    err_t e = load_ctl(&b, &c);

    if (e != E_OK) { kprintf("maj: %s\n", g_why); return E_REPORTED; }
    if (!arg) {
        kprintf("  Mode automatique : %s\n", c.autom == 0 ? "non" : (c.autom == 1 ? "verifier" : "installer"));
        return E_OK;
    }
    if (!strcmp(arg, "non")) c.autom = 0;
    else if (!strcmp(arg, "verifier")) c.autom = 1;
    else if (!strcmp(arg, "installer")) c.autom = 2;
    else { kprintf("usage : maj auto [non|verifier|installer]\n"); return E_REPORTED; }
    e = upd_ctl_store(b, &c);
    if (e != E_OK) { kprintf("maj: ecriture sur le disque impossible\n"); return E_REPORTED; }
    kprintf("  Mode automatique : %s\n", arg);
    return E_OK;
}

err_t update_command(int argc, char **argv)
{
    err_t e;

    if (g_busy) {
        kprintf("maj: une verification ou une installation est deja en cours.\n");
        return E_REPORTED;
    }
    g_busy = 1;
    if (argc == 1 || !strcmp(argv[1], "etat"))
        e = cmd_status();
    else if (!strcmp(argv[1], "verifier"))
        e = cmd_verify();
    else if (!strcmp(argv[1], "installer"))
        e = cmd_install();
    else if (!strcmp(argv[1], "retour"))
        e = cmd_back();
    else if (!strcmp(argv[1], "preparer"))
        e = cmd_prepare(argc > 2 && !strcmp(argv[2], "force"));
    else if (!strcmp(argv[1], "auto"))
        e = cmd_auto(argc > 2 ? argv[2] : NULL);
    else {
        kprintf("usage : maj [etat|verifier|installer|retour|preparer [force]|auto [non|verifier|installer]]\n");
        e = E_REPORTED;
    }
    g_busy = 0;
    return e;
}

/* ------------------------------------------------------------------ tache de fond */
/* Un pas de la tache de fond, `up_s` = secondes depuis le demarrage (separe de la tache pour pouvoir le tester). */
void update_bg_step(struct upd_bg *st, u32 up_s)
{
    if (!st->confirmed && up_s >= UPDATE_CONFIRM_S) {
        st->confirmed = 1;
        if (g_relaunched) {
            const struct upd_blk *b = get_blk();

            if (b && upd_confirm(b, PHOENOS_BUILD) == E_OK)
                DINFO("maj", "version %s confirmee apres %u s de fonctionnement", PHOENOS_VERSION, up_s);
        }
    }
    if (st->next_check == 0)
        st->next_check = UPDATE_FIRST_CHECK_S;
    if (up_s >= st->next_check) {
        struct net_status ns;
        const struct upd_blk *b = get_blk();
        struct upd_ctl c;

        net_get_status(&ns);
        if (!ns.configured) {
            st->next_check = up_s + 30;                 /* pas encore de reseau : on reessaie bientot */
            return;
        }
        st->next_check = up_s + UPDATE_CHECK_EVERY_S;
        if (!g_busy && b && pubkey() && upd_ctl_load(b, &c) == E_OK && c.autom >= 1 && !(g_relaunched && c.trial)) {
            int rc;

            g_busy = 1;
            rc = fetch_offer(&c);
            if (rc == 0) {
                if (c.autom == 2 && do_install() == E_OK)
                    ksnprintf(g_notice, sizeof g_notice, "PhoenOS %s installe : tapez reboot pour l'essayer.", g_offer.m.version);
                else
                    ksnprintf(g_notice, sizeof g_notice, "mise a jour disponible : PhoenOS %s (tapez maj installer).", g_offer.m.version);
                g_notice_new = 1;
                DINFO("maj", "%s", g_notice);
            } else if (rc < 0) {
                DINFO("maj", "verification automatique : %s", g_why);
            }
            g_busy = 0;
        }
    }
}

static void maj_task(void *arg)
{
    struct upd_bg st = { 0, 0 };

    (void)arg;
    for (;;) {
        update_bg_step(&st, pit_uptime_ms() / 1000);
        sched_sleep_ms(5000);
    }
}

void update_start(void)
{
    struct task *t;

    if (sched_create("maj", maj_task, 0, 0, 0, &t) == E_OK)
        sched_detach(t);
}
