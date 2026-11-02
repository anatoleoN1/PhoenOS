/*
 * Test du stockage des mises a jour (updstore.c), du choix de demarrage / retour arriere (updboot.c : upd_boot_select,
 * upd_rollback, upd_confirm) et de la relance (upd_relaunch) avec une FAUSSE memoire physique et un FAUX disque
 * (coupures de courant simulees a chaque ecriture).
 */
#include "testlib.h"
#include "updstore.h"
#include "updman.h"
#include "updboot.h"
#include "updconf.h"
#include "multiboot.h"
#include "crypto.h"
#include "upd_vectors.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

/* ---------- faux disque ---------- */
#define DISK_SECTORS 65536u
static u8 disk[DISK_SECTORS * 512];
static long dead_after = -1;            /* coupure de courant apres N secteurs ecrits ; -1 : jamais */
static int dead;
static u32 writes;
static int fail_reads;

static err_t d_rd(void *c, u32 lba, u32 n, void *buf)
{
    (void)c;
    if (fail_reads || lba + n > DISK_SECTORS) return E_IO;
    memcpy(buf, disk + (usize)lba * 512, (usize)n * 512);
    return E_OK;
}
static err_t d_wr(void *c, u32 lba, u32 n, const void *buf)
{
    (void)c;
    if (lba + n > DISK_SECTORS) return E_IO;
    for (u32 i = 0; i < n; i++) {
        if (dead) return E_IO;
        if (dead_after >= 0 && writes >= (u32)dead_after) { dead = 1; return E_IO; }
        memcpy(disk + (usize)(lba + i) * 512, (const u8 *)buf + (usize)i * 512, 512);
        writes++;
    }
    return E_OK;
}
static err_t d_fl(void *c) { (void)c; return dead ? E_IO : E_OK; }
static struct upd_blk BLK = { DISK_SECTORS, 0, d_rd, d_wr, d_fl };
static struct upd_blk SMALL = { 60000, 0, d_rd, d_wr, d_fl };

static void disk_reset(void) { memset(disk, 0, sizeof disk); dead = 0; dead_after = -1; writes = 0; fail_reads = 0; }

/* ---------- fausse memoire physique ---------- */
#define RAM_BYTES (64u << 20)
static u8 ram_array[RAM_BYTES];
u8 *upd_sim_ram = ram_array;

/* ---------- aides ---------- */
static const u8 *KEY = V_PUBKEY;

/* Ecrit dans l'emplacement `slot` une mise a jour complete et signee (jeu de vecteurs choisi). */
static err_t put_slot(int slot, const u8 *kern, u32 klen, const u8 *ini, u32 ilen, const u8 *man, u32 mlen, const u8 *sig, u32 build)
{
    static u8 pad[512];
    u32 ks = (klen + 511) / 512, is = (ilen + 511) / 512;
    err_t e = upd_slot_invalidate(&BLK, slot);

    if (e != E_OK) return e;
    for (u32 i = 0; i < ks; i++) {
        u32 n = klen - i * 512 < 512 ? klen - i * 512 : 512;
        memset(pad, 0, 512); memcpy(pad, kern + i * 512, n);
        if ((e = upd_slot_write(&BLK, slot, UPD_KERNEL_OFF + i, 1, pad)) != E_OK) return e;
    }
    for (u32 i = 0; i < is; i++) {
        u32 n = ilen - i * 512 < 512 ? ilen - i * 512 : 512;
        memset(pad, 0, 512); memcpy(pad, ini + i * 512, n);
        if ((e = upd_slot_write(&BLK, slot, UPD_INITRD_OFF + i, 1, pad)) != E_OK) return e;
    }
    return upd_slot_commit(&BLK, slot, build, klen, ilen, (const char *)man, mlen, sig);
}
static err_t put_good(int slot) { return put_slot(slot, V_KERNEL, V_KERNEL_LEN, V_INITRD, V_INITRD_LEN, V_MAN, V_MAN_LEN, V_SIG, 10005); }

static void ctl_set(i32 active, u32 tries, u32 trial, u32 min_build, u32 autom)
{
    struct upd_ctl c;
    CHECK(upd_ctl_load(&BLK, &c) == E_OK);
    c.active = active; c.tries = tries; c.trial = trial; c.min_build = min_build; c.autom = autom;
    CHECK(upd_ctl_store(&BLK, &c) == E_OK);
}

static void fresh(void) { disk_reset(); CHECK(upd_area_prepare(&BLK, 0) == E_OK); }

static struct upd_boot_env env(u32 running, int relaunched)
{
    struct upd_boot_env e;
    e.blk = &BLK; e.pubkey = KEY; e.running_build = running; e.running_edition = "perf32"; e.relaunched = relaunched;
    return e;
}

static struct upd_boot_plan plan;

static void test_store(void)
{
    struct upd_ctl c, c2;
    struct upd_slot s;

    disk_reset();
    CHECK(upd_area_check(&BLK) == E_OK && upd_area_check(&SMALL) == E_NOSYS && upd_area_check(0) == E_INVAL);
    CHECK(upd_ctl_load(&BLK, &c) == E_NOENT);                               /* disque vierge */
    CHECK(upd_ctl_load(&SMALL, &c) == E_NOSYS);
    CHECK(upd_area_prepare(&SMALL, 0) == E_NOSYS);
    CHECK(upd_area_prepare(&BLK, 0) == E_OK);
    CHECK(upd_ctl_load(&BLK, &c) == E_OK && c.active == -1 && c.tries == 0 && c.trial == 0 && c.min_build == 0 && c.autom == 1);
    CHECK(upd_slot_load(&BLK, 0, &s) == E_NOENT && upd_slot_load(&BLK, 1, &s) == E_NOENT);
    CHECK(upd_area_prepare(&BLK, 0) == E_OK);                              /* idempotent */
    c.min_build = 77; c.active = 1; c.trial = 1; c.tries = 2; c.autom = 2;
    CHECK(upd_ctl_store(&BLK, &c) == E_OK);
    CHECK(upd_ctl_load(&BLK, &c2) == E_OK && c2.gen == c.gen && c2.min_build == 77 && c2.active == 1 && c2.trial == 1 && c2.tries == 2 && c2.autom == 2);
    CHECK(upd_area_prepare(&BLK, 0) == E_OK && upd_ctl_load(&BLK, &c2) == E_OK && c2.min_build == 77);   /* ne remet rien a zero */

    /* deux copies, la plus recente valide gagne ; si elle est abimee, l'ancienne la remplace */
    c.min_build = 88;
    CHECK(upd_ctl_store(&BLK, &c) == E_OK);
    CHECK(upd_ctl_load(&BLK, &c2) == E_OK && c2.min_build == 88);
    {
        u32 newest = (c.gen & 1u);                                           /* copie qui vient d'etre ecrite */
        disk[(usize)(UPD_BASE_LBA + newest) * 512 + 20] ^= 0xFF;
        CHECK(upd_ctl_load(&BLK, &c2) == E_OK && c2.min_build == 77);       /* retour a la generation precedente */
        disk[(usize)(UPD_BASE_LBA + (1 - newest)) * 512 + 20] ^= 0xFF;
        CHECK(upd_ctl_load(&BLK, &c2) == E_INVAL);                          /* les deux abimees : on ne devine pas */
        CHECK(upd_area_prepare(&BLK, 0) == E_BUSY);                         /* et on n'ecrase pas sans force */
        CHECK(upd_area_prepare(&BLK, 1) == E_OK && upd_ctl_load(&BLK, &c2) == E_OK && c2.min_build == 0);
    }
    /* valeurs absurdes refusees */
    c2.active = 2;
    CHECK(upd_ctl_store(&BLK, &c2) == E_INVAL);
    c2.active = -1; c2.autom = 3;
    CHECK(upd_ctl_store(&BLK, &c2) == E_INVAL);

    /* zone deja occupee par autre chose : on n'y touche pas */
    disk_reset();
    disk[(usize)UPD_BASE_LBA * 512 + 100] = 0x42;
    CHECK(upd_ctl_load(&BLK, &c) == E_INVAL && upd_area_prepare(&BLK, 0) == E_BUSY && disk[(usize)UPD_BASE_LBA * 512 + 100] == 0x42);

    /* emplacements */
    fresh();
    CHECK(put_good(0) == E_OK && upd_slot_load(&BLK, 0, &s) == E_OK);
    CHECK(s.build == 10005 && s.kernel_bytes == V_KERNEL_LEN && s.initrd_bytes == V_INITRD_LEN && s.man_len == V_MAN_LEN);
    CHECK(!memcmp(s.man, V_MAN, V_MAN_LEN) && !memcmp(s.sig, V_SIG, 64) && s.man[V_MAN_LEN] == 0);
    CHECK(upd_slot_load(&BLK, 1, &s) == E_NOENT);                           /* l'autre n'est pas touche */
    {
        u8 b2[512];
        CHECK(upd_slot_read(&BLK, 0, UPD_KERNEL_OFF, 1, b2) == E_OK && !memcmp(b2, V_KERNEL, 512));
        CHECK(upd_slot_read(&BLK, 0, UPD_INITRD_OFF, 1, b2) == E_OK && !memcmp(b2, V_INITRD, 512));
        /* bornes */
        CHECK(upd_slot_write(&BLK, 0, 0, 1, b2) == E_INVAL);                                  /* l'en-tete n'est pas accessible */
        CHECK(upd_slot_write(&BLK, 0, UPD_MAN_OFF, 1, b2) == E_INVAL);
        CHECK(upd_slot_write(&BLK, 2, UPD_KERNEL_OFF, 1, b2) == E_INVAL && upd_slot_write(&BLK, -1, UPD_KERNEL_OFF, 1, b2) == E_INVAL);
        CHECK(upd_slot_write(&BLK, 0, UPD_KERNEL_OFF, 0, b2) == E_INVAL);
        CHECK(upd_slot_write(&BLK, 0, UPD_KERNEL_OFF + UPD_KERNEL_MAX, 1, b2) == E_OK);       /* = 1er secteur de l'initrd... */
        CHECK(upd_slot_write(&BLK, 0, UPD_INITRD_OFF - 1, 2, b2) == E_INVAL);                 /* enjambe deux zones */
        CHECK(upd_slot_write(&BLK, 0, UPD_KERNEL_OFF, UPD_KERNEL_MAX + 1, b2) == E_INVAL);    /* depasse la zone du noyau */
        CHECK(upd_slot_write(&BLK, 0, UPD_SLOT_SECTORS - 1, 1, b2) == E_OK);
        CHECK(upd_slot_write(&BLK, 0, UPD_SLOT_SECTORS, 1, b2) == E_INVAL);
        CHECK(upd_slot_write(&BLK, 0, UPD_SLOT_SECTORS - 1, 2, b2) == E_INVAL);
        CHECK(upd_slot_write(&BLK, 0, 0xFFFFFFFFu, 2, b2) == E_INVAL);
    }
    /* en-tete abime : emplacement vide */
    disk[(usize)(UPD_SLOT0_LBA) * 512 + 9] ^= 1;
    CHECK(upd_slot_load(&BLK, 0, &s) == E_NOENT);
    CHECK(put_good(1) == E_OK && upd_slot_load(&BLK, 1, &s) == E_OK);
    CHECK(upd_slot_invalidate(&BLK, 1) == E_OK && upd_slot_load(&BLK, 1, &s) == E_NOENT);
    /* commit refuse les valeurs absurdes */
    CHECK(upd_slot_commit(&BLK, 0, 1, 0, 10, "x", 1, V_SIG) == E_INVAL);
    CHECK(upd_slot_commit(&BLK, 0, 1, UPD_KERNEL_MAX_BYTES + 1, 10, "x", 1, V_SIG) == E_INVAL);
    CHECK(upd_slot_commit(&BLK, 0, 1, 10, UPD_INITRD_MAX_BYTES + 1, "x", 1, V_SIG) == E_INVAL);
    CHECK(upd_slot_commit(&BLK, 0, 1, 10, 10, "x", UPD_MAN_MAX + 1, V_SIG) == E_INVAL);
    CHECK(upd_slot_commit(&BLK, 0, 1, 10, 10, "x", 0, V_SIG) == E_INVAL);

    /* la zone ne deborde pas sur PhoenFS (secteurs < 32768) */
    fresh();
    for (u32 i = 0; i < UPD_BASE_LBA * 512; i++) if (disk[i]) { CHECK(0); break; }
}

static void test_select(void)
{
    struct upd_boot_env e;
    struct upd_ctl c;
    struct upd_slot s;
    struct upd_manifest m;
    int now = 99;

    /* sans cle / zone / etat : on continue sur la version du CD */
    fresh(); put_good(0); ctl_set(0, 0, 1, 0, 1);
    e = env(10004, 0); e.pubkey = NULL;
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE);
    e = env(10004, 0); e.blk = &SMALL;
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE);
    disk_reset();
    e = env(10004, 0);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE);
    fresh();
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && plan.slot == -1);   /* rien d'installe */
    e = env(10004, 1);
    put_good(0); ctl_set(0, 0, 1, 0, 1);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE);                       /* deja relance : jamais de boucle */
    CHECK(upd_ctl_load(&BLK, &c) == E_OK && c.tries == 0);

    /* nouvelle version a l'essai : relance, essais comptes, puis retour arriere au 3e demarrage */
    fresh(); put_good(0); ctl_set(0, 0, 1, 0, 1);
    e = env(10004, 0);
    CHECK(upd_boot_select(&e, &plan) == UPB_RELAUNCH && plan.slot == 0 && plan.m.build == 10005 && !strcmp(plan.m.version, "0.10.5"));
    CHECK(upd_ctl_load(&BLK, &c) == E_OK && c.tries == 1 && c.active == 0 && c.trial == 1);
    CHECK(upd_boot_select(&e, &plan) == UPB_RELAUNCH);
    CHECK(upd_ctl_load(&BLK, &c) == E_OK && c.tries == 2);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && plan.rolled_back);   /* UPDATE_MAX_TRIES atteint */
    CHECK(upd_ctl_load(&BLK, &c) == E_OK && c.active == -1 && c.tries == 0 && c.trial == 0);
    CHECK(upd_slot_load(&BLK, 0, &s) == E_NOENT);                            /* la mauvaise version est condamnee */
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && !plan.rolled_back);  /* et on ne la relance plus */

    /* confirmation : remet les essais a zero et releve le build confirme */
    fresh(); put_good(0); ctl_set(0, 0, 1, 0, 1);
    e = env(10004, 0);
    CHECK(upd_boot_select(&e, &plan) == UPB_RELAUNCH);
    CHECK(upd_confirm(&BLK, 10005) == E_OK);
    CHECK(upd_ctl_load(&BLK, &c) == E_OK && c.tries == 0 && c.trial == 0 && c.min_build == 10005 && c.active == 0);
    { u32 w = writes; CHECK(upd_confirm(&BLK, 10005) == E_OK && writes == w); }   /* rien a ecrire : on n'ecrit pas */
    /* une version confirmee qui plante ensuite : essais comptes aussi (trial = 0) */
    CHECK(upd_boot_select(&e, &plan) == UPB_RELAUNCH);
    CHECK(upd_boot_select(&e, &plan) == UPB_RELAUNCH);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && plan.rolled_back);

    /* le CD est aussi recent (ou plus) : on garde le CD */
    fresh(); put_good(0); ctl_set(0, 0, 1, 0, 1);
    e = env(10005, 0);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && !plan.rolled_back);
    e = env(10009, 0);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE);
    CHECK(upd_ctl_load(&BLK, &c) == E_OK && c.tries == 0 && c.active == 0);  /* l'emplacement reste (pas condamne) */

    /* signature d'une autre cle, manifeste modifie, edition etrangere, tailles incoherentes : retour arriere */
    fresh();
    CHECK(put_slot(0, V_KERNEL, V_KERNEL_LEN, V_INITRD, V_INITRD_LEN, V_MAN, V_MAN_LEN, V_SIG_OTHER, 10005) == E_OK);
    ctl_set(0, 0, 1, 0, 1); e = env(10004, 0);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && plan.rolled_back);
    CHECK(upd_ctl_load(&BLK, &c) == E_OK && c.active == -1);

    fresh();
    CHECK(put_slot(0, V_KERNEL_X64, V_KERNEL_X64_LEN, V_INITRD, V_INITRD_LEN, V_MAN_X64, V_MAN_X64_LEN, V_SIG_X64, 10005) == E_OK);
    ctl_set(0, 0, 1, 0, 1);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && plan.rolled_back);   /* edition perf64 dans un noyau perf32 */
    CHECK(upd_slot_verify(&BLK, 0, KEY, "perf64", &s, &m) == -1);            /* (invalide maintenant : en-tete annule) */

    fresh();
    CHECK(put_slot(0, V_KERNEL, V_KERNEL_LEN, V_INITRD, V_INITRD_LEN, V_MAN, V_MAN_LEN, V_SIG, 10099) == E_OK);   /* build de l'en-tete != manifeste */
    ctl_set(0, 0, 1, 0, 1);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && plan.rolled_back);
    fresh();
    CHECK(put_slot(0, V_KERNEL, V_KERNEL_LEN - 4, V_INITRD, V_INITRD_LEN, V_MAN, V_MAN_LEN, V_SIG, 10005) == E_OK);   /* taille != manifeste */
    ctl_set(0, 0, 1, 0, 1);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && plan.rolled_back);

    /* anti-retour-arriere : build plus ancien que le plus haut confirme */
    fresh(); put_good(0); ctl_set(0, 0, 0, 10006, 1);
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE && plan.rolled_back);

    /* retour arriere : revient a l'autre emplacement s'il est valide, en baissant le plafond (retour voulu) */
    fresh();
    CHECK(put_slot(0, V_KERNEL_NEW, V_KERNEL_NEW_LEN, V_INITRD, V_INITRD_LEN, V_MAN_NEW, V_MAN_NEW_LEN, V_SIG_NEW, 10006) == E_OK);
    put_good(1);
    ctl_set(0, 0, 0, 10006, 1);
    CHECK(upd_ctl_load(&BLK, &c) == E_OK);
    CHECK(upd_rollback(&BLK, &c, KEY, "perf32", &now) == E_OK && now == 1);
    CHECK(c.active == 1 && c.min_build == 10005 && c.trial == 0 && c.tries == 0);
    CHECK(upd_slot_load(&BLK, 0, &s) == E_NOENT && upd_slot_load(&BLK, 1, &s) == E_OK);
    CHECK(upd_rollback(&BLK, &c, KEY, "perf32", &now) == E_OK && now == -1);   /* encore un : retour au CD */
    CHECK(upd_rollback(&BLK, &c, KEY, "perf32", &now) == E_NOENT);             /* rien a annuler */

    /* disque en panne pendant le choix : on garde le CD sans rien condamner */
    fresh(); put_good(0); ctl_set(0, 0, 1, 0, 1);
    e = env(10004, 0);
    dead_after = (long)writes;                                                /* plus aucune ecriture possible */
    CHECK(upd_boot_select(&e, &plan) == UPB_CONTINUE);
    dead = 0; dead_after = -1;
    CHECK(upd_ctl_load(&BLK, &c) == E_OK && c.active == 0 && c.tries == 0);
    CHECK(upd_slot_load(&BLK, 0, &s) == E_OK);
}

/* ---------- relance ---------- */
static struct relaunch_args got_args;
static int jumped;
static void *got_stub;
static u32 mbi_at = 0x10000;

static void fake_jump(void *stub, const struct relaunch_args *a)
{
    jumped++;
    got_stub = stub;
    got_args = *a;
    /* ce que fait relaunch.S : copie, bss a zero */
    memmove(upd_sim_ram + a->dst, upd_sim_ram + a->src, a->copy_len);
    memset(upd_sim_ram + a->zero_dst, 0, a->zero_len);
}

static u32 W32(u32 off, u32 v) { memcpy(upd_sim_ram + off, &v, 4); return off + 4; }

/* carte memoire : [0, 0x9fc00) et [1 Mio, top) disponibles ; infos GRUB vers 0x10000 ; ancien module vers 0x1d0000 */
static void sim_setup(u32 top, int with_hole)
{
    u32 mm = 0x10200, o = mm;
    struct multiboot_info *mb = (struct multiboot_info *)(upd_sim_ram + mbi_at);

    memset(upd_sim_ram + 0x10000, 0, 0x2000);
    memset(mb, 0, sizeof *mb);
    mb->flags = MB_FLAG_MEM | MB_FLAG_MODS | MB_FLAG_MMAP | (1u << 2);
    mb->mem_lower = 639; mb->mem_upper = (top - 0x100000) / 1024;
    o = W32(o, 20); o = W32(o, 0); o = W32(o, 0); o = W32(o, 0x9fc00); o = W32(o, 0); o = W32(o, 1);
    if (with_hole) {                                                  /* trou de 4 Mio a partir de 8 Mio (periphyeriques) */
        o = W32(o, 20); o = W32(o, 0x100000); o = W32(o, 0); o = W32(o, 0x700000); o = W32(o, 0); o = W32(o, 1);
        o = W32(o, 20); o = W32(o, 0x900000); o = W32(o, 0); o = W32(o, top - 0x900000); o = W32(o, 0); o = W32(o, 1);
    } else {
        o = W32(o, 20); o = W32(o, 0x100000); o = W32(o, 0); o = W32(o, top - 0x100000); o = W32(o, 0); o = W32(o, 1);
    }
    o = W32(o, 20); o = W32(o, 0xFEC00000u); o = W32(o, 0); o = W32(o, 0x1000); o = W32(o, 0); o = W32(o, 2);
    mb->mmap_addr = mm; mb->mmap_length = o - mm;
    {   /* un module (l'ancien initrd) */
        u32 mod = 0x10100;
        W32(mod, 0x1d0000); W32(mod + 4, 0x210000); W32(mod + 8, 0); W32(mod + 12, 0);
        mb->mods_count = 1; mb->mods_addr = mod;
    }
    memset(upd_sim_ram + 0x1d0000, 0xA5, 0x40000);                    /* l'ancien module */
    memset(upd_sim_ram + 0x100000, 0x5A, 0x80000);                    /* l'ancien noyau */
}

static void prep_plan(void)
{
    fresh(); put_good(0); ctl_set(0, 0, 1, 0, 1);
    CHECK(upd_boot_select(&(struct upd_boot_env){ &BLK, KEY, 10004, "perf32", 0 }, &plan) == UPB_RELAUNCH);
}

static u32 rd(u32 off) { u32 v; memcpy(&v, upd_sim_ram + off, 4); return v; }

static void test_relaunch(void)
{
    static const u8 stub[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    int r;

    /* cas normal */
    prep_plan(); sim_setup(64u << 20, 0); jumped = 0;
    r = upd_relaunch(&BLK, &plan, mbi_at, 0x180000, stub, sizeof stub, fake_jump);
    CHECK(r == UPB_OK && jumped == 1);
    CHECK(got_args.dst == 0x100000 && got_args.copy_len == V_KERNEL_LEN - 32 && got_args.entry == 0x100010);
    CHECK(got_args.zero_dst == 0x100000 + V_KERNEL_LEN - 32 && got_args.zero_len == 20480 - (V_KERNEL_LEN - 32));
    CHECK(got_args.magic == 0x2BADB002u && (got_args.mbi & 3) == 0 && (got_args.sp & 3) == 0);
    CHECK(got_args.src > 0x180000 + 0x10000 && got_args.src + got_args.copy_len <= (64u << 20));       /* hors du noyau, dans la RAM */
    CHECK(!memcmp(upd_sim_ram + 0x100000, V_KERNEL + 32, V_KERNEL_LEN - 32));                            /* noyau copie a sa place */
    {
        u32 z = 0;
        for (u32 i = V_KERNEL_LEN - 32; i < 20480; i++) z |= upd_sim_ram[0x100000 + i];
        CHECK(z == 0);                                                                                    /* .bss a zero */
    }
    CHECK(!memcmp(got_stub, stub, 16));                                                                   /* code de copie recopie en tete de la zone */
    CHECK((u8 *)got_stub + 576 == upd_sim_ram + got_args.mbi);
    {   /* les infos Multiboot du noyau lance */
        u32 mb = got_args.mbi, flags = rd(mb), mods = rd(mb + 24), name = rd(mb + 64), ma = rd(mb + 48), ml = rd(mb + 44);

        CHECK((flags & MB_FLAG_MEM) && (flags & MB_FLAG_MODS) && (flags & MB_FLAG_MMAP) && (flags & (1u << 9)) && !(flags & (1u << 2)));
        CHECK(rd(mb + 4) == 639 && rd(mb + 8) == ((64u << 20) - 0x100000) / 1024);
        CHECK(rd(mb + 20) == 1 && !strcmp((char *)upd_sim_ram + name, UPD_RELAUNCH_NAME));
        CHECK(ml == 3 * 24 && !memcmp(upd_sim_ram + ma, upd_sim_ram + 0x10200, ml));
        CHECK(rd(mods + 4) - rd(mods) == V_INITRD_LEN && !memcmp(upd_sim_ram + rd(mods), V_INITRD, V_INITRD_LEN));
        CHECK((rd(mods) & 0xFFF) == 0 && rd(mods) > got_args.src);
        CHECK(rd(mods + 8) == 0);
    }
    /* la zone de preparation est tout en haut de la memoire disponible */
    CHECK(rd(got_args.mbi + 24) + 20000 > (64u << 20) - 0x10000);
    CHECK(!(rd(got_args.mbi) & (1u << 12)));                                                              /* pas d'ecran donne : pas de champs d'ecran */

    /* perf64 : les infos d'ecran du noyau actuel (flag 12, octets 88..115) sont reprises pour le noyau lance */
    prep_plan(); sim_setup(64u << 20, 0); jumped = 0;
    {
        struct multiboot_info *mb = (struct multiboot_info *)(upd_sim_ram + mbi_at);

        mb->flags |= 1u << 12;
        for (int i = 0; i < 28; i++)
            upd_sim_ram[mbi_at + 88 + i] = (u8)(0x40 + i);
        upd_sim_ram[mbi_at + 116] = 0xEE;                                                                 /* hors zone : pas copie */
    }
    r = upd_relaunch(&BLK, &plan, mbi_at, 0x180000, stub, sizeof stub, fake_jump);
    CHECK(r == UPB_OK && (rd(got_args.mbi) & (1u << 12)));
    CHECK(!memcmp(upd_sim_ram + got_args.mbi + 88, upd_sim_ram + mbi_at + 88, 28) && upd_sim_ram[got_args.mbi + 116] == 0);

    /* trou dans la memoire : la preparation est dans une zone "disponible", jamais dans le trou */
    prep_plan(); sim_setup(64u << 20, 1); jumped = 0;
    r = upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump);
    CHECK(r == UPB_OK && got_args.src >= 0x900000);

    /* echecs AVANT de toucher a la memoire du noyau (aucun saut, noyau intact) */
    prep_plan(); sim_setup(64u << 20, 0); jumped = 0;
    disk[(usize)(UPD_SLOT0_LBA + UPD_KERNEL_OFF) * 512 + 500] ^= 1;                    /* un octet du noyau change sur le disque */
    r = upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump);
    CHECK(r == UPB_E_HASH && jumped == 0 && upd_sim_ram[0x100000] == 0x5A && upd_sim_ram[0x13FFFF] == 0x5A);
    prep_plan(); sim_setup(64u << 20, 0);
    disk[(usize)(UPD_SLOT0_LBA + UPD_INITRD_OFF) * 512 + 5] ^= 1;
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump) == UPB_E_HASH && jumped == 0);
    prep_plan(); sim_setup(64u << 20, 0);
    disk[(usize)(UPD_SLOT0_LBA + UPD_KERNEL_OFF) * 512 + 0] = 'X';                     /* en-tete PBI1 abime */
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump) == UPB_E_IMAGE && jumped == 0);
    prep_plan(); sim_setup(64u << 20, 0); fail_reads = 1;
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump) == UPB_E_DISK && jumped == 0);
    fail_reads = 0;
    prep_plan(); sim_setup(0x194000, 0);                                               /* trop peu de RAM au-dessus du noyau : rien ne rentre */
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump) == UPB_E_MEM && jumped == 0);
    prep_plan(); sim_setup(64u << 20, 0);
    ((struct multiboot_info *)(upd_sim_ram + mbi_at))->flags &= ~(u32)MB_FLAG_MMAP;     /* pas de carte memoire */
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump) == UPB_E_MEM);
    prep_plan(); sim_setup(64u << 20, 0);
    ((struct multiboot_info *)(upd_sim_ram + mbi_at))->mmap_length = 5000;              /* carte trop grande */
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump) == UPB_E_MEM);
    prep_plan(); sim_setup(64u << 20, 0);
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, stub, 600, fake_jump) == UPB_E_MEM);   /* code de copie trop gros */
    /* l'ancien noyau finit tres haut : la preparation doit rester au-dessus, sinon refus */
    prep_plan(); sim_setup(64u << 20, 0); jumped = 0;
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, (64u << 20) - 4096, 0, 0, fake_jump) == UPB_E_MEM && jumped == 0);
    /* la zone choisie recouvre les infos de GRUB (memoire minuscule) : refus plutot que d'ecraser */
    prep_plan(); sim_setup(64u << 20, 0);
    {
        struct multiboot_info *mb = (struct multiboot_info *)(upd_sim_ram + mbi_at);
        mb->mods_addr = (64u << 20) - 0x3000;                                              /* le tableau des modules est tout en haut */
    }
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump) == UPB_E_MEM && jumped == 0);
    /* l'adresse de chargement du noyau n'est pas dans la memoire disponible */
    prep_plan(); sim_setup(64u << 20, 0);
    W32(0x10200 + 24 + 20, 2);                                                           /* la plage de 1 Mio est "reservee" */
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump) == UPB_E_IMAGE && jumped == 0);
    prep_plan(); sim_setup(0x140000, 0);                                                 /* 256 Kio de RAM : pas de place pour preparer */
    CHECK(upd_relaunch(&BLK, &plan, mbi_at, 0x180000, 0, 0, fake_jump) == UPB_E_MEM && jumped == 0);
}

int test_main(void)
{
    test_store();
    test_select();
    test_relaunch();
    out("  Mises a jour (stockage, choix de demarrage, retour arriere, relance) verifies : etat sur deux copies, emplacements, essais, signatures, anti-retour-arriere, memoire simulee\n");
    return 0;
}
