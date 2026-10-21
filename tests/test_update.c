/*
 * Test de bout en bout des mises a jour (update.c) : commande `maj` contre un FAUX GitHub (redirections vers un faux CDN),
 * un FAUX disque (coupure de courant a CHAQUE secteur) et le VRAI code de manifeste, signature, stockage, client HTTP
 * et choix de demarrage. Manifestes signes par la cle de TEST (tests/gen_upd_vectors.py).
 */
#include "testlib.h"
#include "update.h"
#include "updstore.h"
#include "updman.h"
#include "updboot.h"
#include "updconf.h"
#include "version.h"
#include "kprintf.h"
#include "tlssock.h"
#include "ata.h"
#include "net.h"
#include "persist.h"
#include "sched.h"
#include "multiboot.h"
#include "upd_vectors.h"

u8 test_scancode;
u8 *upd_sim_ram;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }
void vga_putc(char c) { (void)c; }
void serial_putc(char c) { (void)c; }
u32 pit_uptime_ms(void) { return 0; }

static int fails;
#define CK(c, msg) do { if (!(c)) { out("  ECHEC : " msg "\n"); fails++; } } while (0)

/* ---------- faux disque ATA ---------- */
#define DISK_SECTORS 65536u
static u8 disk[DISK_SECTORS * 512];
static u8 snap[DISK_SECTORS * 512];
static struct ata_dev DEV = { 1, 0, 0, 0, 0, 0, DISK_SECTORS, "FAUX" };
static int have_disk = 1;
static long dead_after = -1;
static int dead;
static u32 writes;
void ata_init(void) {}
const struct ata_dev *ata_first_disk(void) { return have_disk ? &DEV : 0; }
err_t ata_read(const struct ata_dev *d, u32 lba, u32 n, void *buf)
{
    if (lba + n > d->sectors) return E_INVAL;
    memcpy(buf, disk + (usize)lba * 512, (usize)n * 512);
    return E_OK;
}
err_t ata_write(const struct ata_dev *d, u32 lba, u32 n, const void *buf)
{
    if (lba + n > d->sectors) return E_INVAL;
    for (u32 i = 0; i < n; i++) {
        if (dead) return E_IO;
        if (dead_after >= 0 && writes >= (u32)dead_after) { dead = 1; return E_IO; }
        memcpy(disk + (usize)(lba + i) * 512, (const u8 *)buf + (usize)i * 512, 512);
        writes++;
    }
    return E_OK;
}
err_t ata_flush(const struct ata_dev *d) { (void)d; return dead ? E_IO : E_OK; }

/* ---------- reste du noyau ---------- */
static int persist_ready = 1, net_up = 1, tls_err;
int persist_state(void) { return persist_ready ? PERSIST_READY : PERSIST_UNFORMATTED; }
void net_get_status(struct net_status *s) { memset(s, 0, sizeof *s); s->present = 1; s->configured = net_up; }
err_t net_resolve(const char *name, u32 *ip) { (void)name; *ip = 0x0A000101u; return E_OK; }
err_t sched_create(const char *n, void (*e)(void *), void *a, void (*c)(void *), void *o, struct task **t)
{ (void)n; (void)e; (void)a; (void)c; (void)o; (void)t; return E_OK; }
void sched_detach(struct task *t) { (void)t; }
void sched_sleep_ms(u32 ms) { (void)ms; }
char kernel_end[16];
char relaunch_stub_start[4], relaunch_stub_end[4];
const char *tlss_last_error(void) { return "certificat expire"; }

/* ---------- faux GitHub ---------- */
struct route { const char *host; char path[300]; const u8 *resp; u32 len; };
static struct route routes[16];
static int nroutes;
static u8 pool[80000];
static u32 pool_n;
static int conn_open;
static char host_now[64];
static char req[2048];
static u32 req_len, cur_pos, cur_len;
static const u8 *cur;
#define CDN "release-assets.githubusercontent.com"

static void scpy(char *d, const char *s) { while ((*d++ = *s++)) ; }
static void scat(char *d, const char *s) { while (*d) d++; scpy(d, s); }

err_t tlss_connect(u32 owner, const char *host, u32 ip, u16 port, int *h)
{
    (void)owner; (void)ip; (void)port;
    if (tls_err == 1) return E_CERT;
    if (tls_err == 2) return E_BUSY;
    if (conn_open) return E_BUSY;
    conn_open = 1; *h = 0;
    scpy(host_now, host);
    req_len = 0; cur = 0; cur_pos = cur_len = 0;
    return E_OK;
}
i32 tlss_send(u32 owner, int h, const u8 *d, u32 n)
{
    (void)owner; (void)h;
    if (req_len + n >= sizeof req) return -1;
    memcpy(req + req_len, d, n); req_len += n; req[req_len] = 0;
    if (req_len >= 4 && !memcmp(req + req_len - 4, "\r\n\r\n", 4)) {
        static const u8 nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        char path[300];
        u32 i = 4, k = 0;

        while (req[i] && req[i] != ' ' && k < sizeof path - 1) path[k++] = req[i++];
        path[k] = 0;
        cur = nf; cur_len = sizeof nf - 1; cur_pos = 0;
        for (int r = 0; r < nroutes; r++)
            if (!strcmp(routes[r].host, host_now) && !strcmp(routes[r].path, path)) {
                cur = routes[r].resp; cur_len = routes[r].len;
            }
    }
    return (i32)n;
}
i32 tlss_recv(u32 owner, int h, u8 *b, u32 cap)
{
    u32 n;
    (void)owner; (void)h;
    if (!cur || cur_pos >= cur_len) return 0;
    n = cur_len - cur_pos;
    if (n > cap) n = cap;
    if (n > 1400) n = 1400;
    memcpy(b, cur + cur_pos, n); cur_pos += n;
    return (i32)n;
}
err_t tlss_close(u32 owner, int h) { (void)owner; (void)h; conn_open = 0; return E_OK; }

static u32 put(u8 *d, const char *s) { u32 n = 0; while (s[n]) { d[n] = (u8)s[n]; n++; } return n; }
static u32 putnum(u8 *d, u32 v) { char t[12]; int n = 0; u32 k = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) d[k++] = (u8)t[--n]; return k; }
static void add_route(const char *host, const char *path, const u8 *resp, u32 len)
{
    struct route *r = &routes[nroutes++];
    r->host = host; scpy(r->path, path); r->resp = resp; r->len = len;
}
/* github.com/<path> -> 302 -> CDN/<cdnpath> -> 200 corps */
static void serve(const char *path, const char *cdnpath, const u8 *body, u32 len)
{
    u8 *b = pool + pool_n; u32 n = 0;
    char loc[200];

    scpy(loc, "https://" CDN); scat(loc, cdnpath);
    n += put(b + n, "HTTP/1.1 302 Found\r\nLocation: "); n += put(b + n, loc); n += put(b + n, "\r\nContent-Length: 0\r\n\r\n");
    pool_n += n; add_route(UPDATE_HOST, path, b, n);
    b = pool + pool_n; n = 0;
    n += put(b + n, "HTTP/1.1 200 OK\r\nContent-Length: "); n += putnum(b + n, len); n += put(b + n, "\r\n\r\n");
    memcpy(b + n, body, len); n += len;
    pool_n += n; add_route(CDN, cdnpath, b, n);
}

static u8 kcopy[8192], icopy[20000];
static u8 mcopy[1024];
/* Publie une release : manifeste + signature "latest", fichiers sous /releases/download/<ver>/ */
static void publish(const char *ver, const u8 *man, u32 ml, const u8 *sig, const u8 *k, u32 kl, const u8 *ini, u32 il, const char *ed)
{
    char p[300], n1[80], n2[80];

    nroutes = 0; pool_n = 0;
    scpy(p, "/" UPDATE_REPO "/releases/latest/download/manifest-" PHOENOS_EDITION ".txt");
    serve(p, "/gh/m?sig=a1b2&x=%2F", man, ml);
    scat(p, ".sig");
    serve(p, "/gh/s?sig=a1b2", sig, 64);
    scpy(n1, "phoenos-"); scat(n1, ed); scat(n1, "-"); scat(n1, ver); scpy(n2, n1);
    scat(n1, ".kernel"); scat(n2, ".initrd");
    scpy(p, "/" UPDATE_REPO "/releases/download/"); scat(p, ver); scat(p, "/"); scat(p, n1);
    serve(p, "/gh/k", k, kl);
    scpy(p, "/" UPDATE_REPO "/releases/download/"); scat(p, ver); scat(p, "/"); scat(p, n2);
    serve(p, "/gh/i", ini, il);
}
static void pub_good(void)
{
    publish("0.10.5", V_MAN, V_MAN_LEN, V_SIG, V_KERNEL, V_KERNEL_LEN, V_INITRD, V_INITRD_LEN, "perf32");
}

/* ---------- sortie de la commande ---------- */
static char outbuf[8192];
static u32 outn;
static void cap(char c, void *x) { (void)x; if (outn < sizeof outbuf - 1) { outbuf[outn++] = c; outbuf[outn] = 0; } }
static int has(const char *needle)
{
    u32 nl = (u32)strlen(needle);
    for (u32 i = 0; i + nl <= outn; i++) if (!memcmp(outbuf + i, needle, nl)) return 1;
    return 0;
}
static err_t maj(const char *a, const char *b)
{
    char *argv[3] = { "maj", (char *)a, (char *)b };
    err_t e;
    outn = 0; outbuf[0] = 0;
    kprintf_redirect(cap, 0);
    e = update_command(a ? (b ? 3 : 2) : 1, argv);
    kprintf_redirect(0, 0);
    return e;
}

static void world(void)
{
    memset(disk, 0, sizeof disk);
    dead = 0; dead_after = -1; writes = 0; have_disk = 1; persist_ready = 1; net_up = 1; tls_err = 0; conn_open = 0;
    update_reset();
    pub_good();
}
static void prepared(void) { world(); (void)maj("preparer", 0); }

static int area_same(void) { return !memcmp(disk + (usize)UPD_BASE_LBA * 512, snap + (usize)UPD_BASE_LBA * 512, (usize)UPD_AREA_SECTORS * 512); }
static void save(void) { memcpy(snap, disk, sizeof disk); }

static struct upd_blk BLK = { DISK_SECTORS, 0, 0, 0, 0 };
static err_t d_rd(void *c, u32 l, u32 n, void *b) { (void)c; return ata_read(&DEV, l, n, b); }
static err_t d_wr(void *c, u32 l, u32 n, const void *b) { (void)c; return ata_write(&DEV, l, n, b); }
static err_t d_fl(void *c) { (void)c; return ata_flush(&DEV); }
static void blk_init(void) { BLK.rd = d_rd; BLK.wr = d_wr; BLK.flush = d_fl; }

static int ctl(struct upd_ctl *c) { return upd_ctl_load(&BLK, c) == E_OK; }

/* Invariant de securite : le controle est lisible et si un emplacement est actif, il est valide. */
static int safe_state(void)
{
    struct upd_ctl c;
    struct upd_slot s;
    struct upd_manifest m;

    if (upd_ctl_load(&BLK, &c) != E_OK) return 0;
    if (c.active < 0) return 1;
    return upd_slot_verify(&BLK, c.active, V_PUBKEY, PHOENOS_EDITION, &s, &m) == 0;
}

int test_main(void)
{
    struct upd_ctl c;
    struct upd_slot s;
    struct upd_manifest m;
    err_t e;

    blk_init();
    out("== commande maj\n");
    world();
    e = maj(0, 0);
    CK(e == E_OK && has("non preparee"), "etat avant preparation");
    e = maj("installer", 0);
    CK(e == E_REPORTED && has("maj preparer"), "installer avant preparation refuse");
    persist_ready = 0;
    e = maj("preparer", 0);
    CK(e == E_REPORTED && has("pas un disque PhoenOS"), "preparer sans PhoenFS refuse");
    persist_ready = 1;
    e = maj("preparer", 0);
    CK(e == E_OK && has("prete"), "preparer");
    CK(ctl(&c) && c.active == -1 && c.trial == 0, "etat vierge");
    e = maj("verifier", 0);
    CK(e == E_OK && has("Mise a jour disponible") && has("0.10.5") && has("essai"), "verifier trouve 0.10.5 et les notes");

    save();
    e = maj("installer", 0);
    CK(e == E_OK && has("est installe"), "installer");
    CK(ctl(&c) && c.active == 0 && c.trial == 1 && c.tries == 0, "bascule sur l'emplacement A a l'essai");
    CK(upd_slot_verify(&BLK, 0, V_PUBKEY, PHOENOS_EDITION, &s, &m) == 0 && m.build == 10005, "emplacement A valide");
    {
        static u8 buf[8192];
        CK(upd_slot_read(&BLK, 0, UPD_KERNEL_OFF, (V_KERNEL_LEN + 511) / 512, buf) == E_OK && !memcmp(buf, V_KERNEL, V_KERNEL_LEN),
              "noyau ecrit identique");
    }
    e = maj("installer", 0);
    CK(e == E_OK && has("deja installe"), "reinstaller : deja installe");
    e = maj(0, 0);
    CK(has("<- actif") && has("(a l'essai)"), "etat montre l'emplacement actif a l'essai");
    e = maj("retour", 0);
    CK(e == E_OK && has("version du CD") && ctl(&c) && c.active == -1, "retour arriere");
    e = maj("retour", 0);
    CK(has("Rien a annuler"), "retour sans rien");

    out("== refus\n");
    prepared(); save();
    memcpy(mcopy, V_MAN, V_MAN_LEN); mcopy[100] ^= 1;
    publish("0.10.5", mcopy, V_MAN_LEN, V_SIG, V_KERNEL, V_KERNEL_LEN, V_INITRD, V_INITRD_LEN, "perf32");
    e = maj("installer", 0);
    CK(e == E_REPORTED && has("SIGNATURE INVALIDE") && area_same(), "manifeste altere : refuse, disque intact");
    publish("0.10.5", V_MAN, V_MAN_LEN, V_SIG_OTHER, V_KERNEL, V_KERNEL_LEN, V_INITRD, V_INITRD_LEN, "perf32");
    e = maj("installer", 0);
    CK(e == E_REPORTED && has("SIGNATURE INVALIDE") && area_same(), "autre cle : refuse");
    memcpy(kcopy, V_KERNEL, V_KERNEL_LEN); kcopy[2000] ^= 0x10;
    publish("0.10.5", V_MAN, V_MAN_LEN, V_SIG, kcopy, V_KERNEL_LEN, V_INITRD, V_INITRD_LEN, "perf32");
    e = maj("installer", 0);
    CK(e == E_REPORTED && has("empreinte SHA-256") && has("rien n'a ete modifie") && safe_state(), "noyau altere : refuse");
    CK(ctl(&c) && c.active == -1, "noyau altere : version active inchangee");
    memcpy(icopy, V_INITRD, V_INITRD_LEN); icopy[19999] ^= 1;
    publish("0.10.5", V_MAN, V_MAN_LEN, V_SIG, V_KERNEL, V_KERNEL_LEN, icopy, V_INITRD_LEN, "perf32");
    e = maj("installer", 0);
    CK(e == E_REPORTED && has("empreinte SHA-256") && safe_state(), "initrd altere : refuse");
    publish("0.10.5", V_MAN, V_MAN_LEN, V_SIG, V_KERNEL, V_KERNEL_LEN - 100, V_INITRD, V_INITRD_LEN, "perf32");
    e = maj("installer", 0);
    CK(e == E_REPORTED && safe_state() && ctl(&c) && c.active == -1, "fichier tronque : refuse");
    publish("0.10.5", V_MAN_X64, V_MAN_X64_LEN, V_SIG_X64, V_KERNEL_X64, V_KERNEL_X64_LEN, V_INITRD, V_INITRD_LEN, "perf64");
    save();
    e = maj("installer", 0);
    CK(e == E_REPORTED && has("edition perf64, pas perf32") && area_same(), "manifeste d'une autre edition : refuse");
    publish("0.10.7", V_MAN_BADIMG, V_MAN_BADIMG_LEN, V_SIG_BADIMG, V_KERNEL_X64, V_KERNEL_X64_LEN, V_INITRD, V_INITRD_LEN, "perf32");
    e = maj("installer", 0);
    CK(e == E_REPORTED && has("image du noyau invalide") && safe_state() && ctl(&c) && c.active == -1, "image d'une autre edition : refusee");
    publish("0.10.2", V_MAN_OLD, V_MAN_OLD_LEN, V_SIG_OLD, V_KERNEL, V_KERNEL_LEN, V_INITRD, V_INITRD_LEN, "perf32");
    save();
    e = maj("installer", 0);
    CK(e == E_OK && has("deja installe") == 0 && has("a jour") && area_same(), "version plus ancienne : rien a faire");
    publish("0.10.4", V_MAN_SAME, V_MAN_SAME_LEN, V_SIG_SAME, V_KERNEL, V_KERNEL_LEN, V_INITRD, V_INITRD_LEN, "perf32");
    e = maj("verifier", 0);
    CK(e == E_OK && has("Deja a jour"), "meme version : a jour");
    pub_good();
    (void)ctl(&c); c.min_build = 20000; (void)upd_ctl_store(&BLK, &c);
    e = maj("installer", 0);
    CK(e == E_REPORTED && has("retour en arriere"), "anti retour arriere (min_build)");
    nroutes = 0;
    e = maj("verifier", 0);
    CK(e == E_REPORTED && has("pas de mise a jour publiee"), "rien de publie (404)");
    pub_good();
    tls_err = 1;
    e = maj("verifier", 0);
    CK(e == E_REPORTED && has("certificat expire"), "certificat refuse : message clair");
    tls_err = 2;
    e = maj("verifier", 0);
    CK(e == E_REPORTED && has("utilisee par un programme"), "connexion TLS occupee");
    tls_err = 0;
    e = maj("bidule", 0);
    CK(e == E_REPORTED && has("usage"), "usage");

    out("== coupure de courant a chaque ecriture\n");
    {
        u32 total, cut, bad = 0, installed = 0;

        prepared();
        memcpy(snap, disk, sizeof disk);
        writes = 0;
        e = maj("installer", 0);
        total = writes;
        CK(e == E_OK && total > 40, "installation complete : nombre d'ecritures");
        for (cut = 0; cut <= total; cut += (cut < 12 || cut + 12 > total) ? 1 : 3) {
            memcpy(disk, snap, sizeof disk);
            dead = 0; writes = 0; dead_after = (long)cut; pub_good(); update_reset();
            (void)maj("installer", 0);
            dead = 0; dead_after = -1;
            if (!safe_state()) bad++;
            if (ctl(&c) && c.active == 0) installed++;
        }
        CK(bad == 0, "coupure : jamais d'emplacement actif invalide");
        CK(installed >= 1, "coupure : l'installation aboutit quand le courant ne coupe pas");
        /* reprise apres coupure : une nouvelle installation reussit */
        memcpy(disk, snap, sizeof disk);
        dead = 0; writes = 0; dead_after = (long)(total / 2); pub_good(); update_reset();
        (void)maj("installer", 0);
        dead = 0; dead_after = -1; update_reset();
        e = maj("installer", 0);
        CK(e == E_OK && ctl(&c) && c.active == 0 && safe_state(), "reprise apres coupure");
    }

    out("== mode automatique et tache de fond\n");
    prepared();
    e = maj("auto", "installer");
    CK(e == E_OK && ctl(&c) && c.autom == 2, "maj auto installer");
    e = maj("auto", "n'importe quoi");
    CK(e == E_REPORTED && ctl(&c) && c.autom == 2, "maj auto invalide");
    {
        struct upd_bg st = { 0, 0 };
        update_bg_step(&st, 10);
        CK(update_take_notice() == 0 && ctl(&c) && c.active == -1, "avant le premier controle : rien");
        net_up = 0;
        update_bg_step(&st, UPDATE_FIRST_CHECK_S);
        CK(update_take_notice() == 0 && st.next_check == UPDATE_FIRST_CHECK_S + 30, "sans reseau : on reessaie dans 30 s");
        net_up = 1;
        update_bg_step(&st, UPDATE_FIRST_CHECK_S + 30);
        {
            const char *n = update_take_notice();
            CK(n && !memcmp(n, "PhoenOS 0.10.5 installe", 23), "installation automatique + avis");
            CK(update_take_notice() == 0, "l'avis n'est donne qu'une fois");
        }
        CK(ctl(&c) && c.active == 0 && c.trial == 1, "installe automatiquement a l'essai");
    }
    prepared();
    (void)maj("auto", "verifier");
    {
        struct upd_bg st = { 0, 0 };
        update_bg_step(&st, UPDATE_FIRST_CHECK_S);
        {
            const char *n = update_take_notice();
            CK(n && !memcmp(n, "mise a jour disponible", 22), "mode verifier : avis seulement");
        }
        CK(ctl(&c) && c.active == -1, "mode verifier : rien installe");
    }
    prepared();
    CK(ctl(&c) && c.autom == 1, "par defaut : verifier seulement");
    (void)maj("auto", "non");
    {
        struct upd_bg st = { 0, 0 };
        update_bg_step(&st, UPDATE_FIRST_CHECK_S);
        { const char *nn = update_take_notice(); CK(nn == 0, "mode non : aucun avis"); }
        CK(st.next_check == UPDATE_FIRST_CHECK_S + UPDATE_CHECK_EVERY_S, "mode non : prochain controle dans 6 h");
    }

    out("== version installee lancee par l'etape 0\n");
    prepared();
    (void)maj("installer", 0);
    {
        static u8 mbi[128];
        static const char nm[] = UPD_RELAUNCH_NAME;
        struct upd_bg st = { 0, 0 };
        u32 p = (u32)(usize)nm;

        memset(mbi, 0, sizeof mbi);
        mbi[0] = 0x00; mbi[1] = 0x02;                        /* flags bit 9 */
        mbi[64] = (u8)p; mbi[65] = (u8)(p >> 8); mbi[66] = (u8)(p >> 16); mbi[67] = (u8)(p >> 24);
        update_stage0((const struct multiboot_info *)mbi);
        CK(update_relaunched() == 1, "etape 0 reconnait un noyau relance");
        e = maj("installer", 0);
        CK(e == E_REPORTED && has("a l'essai"), "pas d'installation pendant l'essai");
        update_bg_step(&st, UPDATE_CONFIRM_S - 1);
        CK(ctl(&c) && c.trial == 1, "pas encore confirme");
        update_bg_step(&st, UPDATE_CONFIRM_S);
        CK(ctl(&c) && c.trial == 0 && c.tries == 0 && c.min_build == PHOENOS_BUILD, "confirme apres UPDATE_CONFIRM_S");
    }

    if (fails) { out("  => "); outnum((u32)fails); out(" echec(s)\n"); }
    return fails;
}
