/*
 * Tests du stockage : crc32, pilote ATA (sur un faux contrôleur IDE), écriture tar,
 * sauvegarde/chargement PhoenFS, coupure de courant simulée, disque abîmé.
 */
#include "fakeata.h"
#include <stdarg.h>
#include "testlib.h"
#include "multiboot.h"
#include "pmm.h"
#include "kheap.h"
#include "panic.h"
#include "fs.h"
#include "initrd.h"
#include "ata.h"
#include "crc32.h"
#include "tarw.h"
#include "persist.h"

u8 test_scancode;
char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
static char fake_ram[40u << 20] __attribute__((aligned(4096)));

void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;

static int file_is(const char *path, const char *text)
{
    struct fs_node *n;

    return fs_lookup(path, &n) == E_OK && n->type == FS_FILE && n->size == strlen(text) && !memcmp(n->data, text, n->size);
}
static u32 fake_now = 1700000000;
static u32 clock(void) { return fake_now; }
static int exists(const char *path) { struct fs_node *n; return fs_lookup(path, &n) == E_OK; }

static void put(const char *path, const char *text)
{
    struct fs_node *f;

    CHECK(fs_create(path, &f) == E_OK && fs_write(f, text, (u32)strlen(text)) == E_OK);
}

/* mémorise l'image tar produite, pour la faire relire par GNU tar */
static u8 dump[1 << 20];
static u32 dumplen;
static err_t dump_emit(const u8 *b, void *ctx) { (void)ctx; memcpy(dump + dumplen, b, 512); dumplen += 512; return E_OK; }

/* démarre « l'ordinateur » : mémoire neuve, disque détecté, système de fichiers vide */
static int first_boot = 1;
static void boot(void)
{
    if (first_boot) {                       /* mémoire : initialisée une seule fois (le système de fichiers garde ses pointeurs) */
        pmm_init(&M.mbi);
        kheap_init();
        first_boot = 0;
    }
    fs_init();                              /* vide le système de fichiers : la RAM est perdue */
    ata_init();
    persist_init();
}

/* « coupure de courant » : le contenu du disque reste, tout le reste est perdu */
static void power_cycle(void)
{
    fake_dead = 0;
    fake_dead_after_writes = -1;
    boot();
}

static void write_file(const char *name, const void *data, u32 len)
{
    long fd = sys3(5, (long)name, 0101 | 01000, 0644);      /* open(O_WRONLY|O_CREAT|O_TRUNC) */

    if (fd >= 0) {
        sys3(4, fd, (long)data, (long)len);
        sys3(6, fd, 0, 0);
    }
}

int test_main(void)
{
    struct initrd_stats st;
    const struct persist_info *pi;
    static u8 sec[512], sec2[512];

    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };

    /* ---- CRC32 ---- */
    CHECK(crc32_update(0, "123456789", 9) == 0xCBF43926u);
    CHECK(crc32_update(0, "", 0) == 0);
    CHECK(crc32_update(crc32_update(0, "12345", 5), "6789", 4) == 0xCBF43926u);     /* en deux morceaux */

    /* ---- détection ATA ---- */
    boot();
    CHECK(ata_count() == 2);                                     /* disque + lecteur de CD */
    CHECK(ata_dev(0) && !ata_dev(0)->atapi && ata_dev(0)->sectors == fake_disk_sectors);
    CHECK(ata_dev(0) && !strcmp(ata_dev(0)->model, "FAKE DISK 9000"));
    CHECK(ata_dev(1) && ata_dev(1)->atapi && ata_dev(1)->channel == 1);
    CHECK(ata_dev(2) == NULL && ata_dev(-1) == NULL);
    CHECK(ata_first_disk() == ata_dev(0));

    /* ---- lecture / écriture de secteurs ---- */
    for (int i = 0; i < 512; i++) sec[i] = (u8)(i * 7 + 3);
    CHECK(ata_write(ata_dev(0), 100, 1, sec) == E_OK);
    CHECK(ata_read(ata_dev(0), 100, 1, sec2) == E_OK && !memcmp(sec, sec2, 512));
    {
        static u8 four[4 * 512], back[4 * 512];

        for (usize i = 0; i < sizeof four; i++) four[i] = (u8)(i ^ (i >> 8));
        CHECK(ata_write(ata_dev(0), 200, 4, four) == E_OK && ata_read(ata_dev(0), 200, 4, back) == E_OK);
        CHECK(!memcmp(four, back, sizeof four));
    }
    CHECK(ata_write(ata_dev(0), fake_disk_sectors - 1, 1, sec) == E_OK);           /* dernier secteur */
    CHECK(ata_read(ata_dev(0), fake_disk_sectors, 1, sec2) == E_INVAL);            /* hors du disque */
    CHECK(ata_read(ata_dev(0), fake_disk_sectors - 1, 2, sec2) == E_INVAL);
    CHECK(ata_read(ata_dev(0), 0, 0, sec2) == E_OK);                               /* 0 secteur : rien à faire */
    CHECK(ata_read(ata_dev(1), 0, 1, sec2) == E_INVAL);                            /* CD : refusé */
    CHECK(ata_read(NULL, 0, 1, sec2) == E_INVAL);
    CHECK(ata_flush(ata_dev(0)) == E_OK && fake_flushes > 0);
    fake_fail_reads = 1;
    CHECK(ata_read(ata_dev(0), 100, 1, sec2) == E_IO);                             /* erreur de disque signalée */
    fake_fail_reads = 0;
    fake_fail_writes = 1;
    CHECK(ata_write(ata_dev(0), 100, 1, sec) == E_IO);
    fake_fail_writes = 0;

    /* bus sans matériel (0xFF) : aucun périphérique, pas de plantage ni de boucle infinie */
    fake_no_hardware = 1;
    ata_init();
    CHECK(ata_count() == 0 && ata_first_disk() == NULL);
    fake_no_hardware = 0;

    /* ---- persist sans disque ---- */
    fake_no_hardware = 1;
    boot();
    CHECK(persist_state() == PERSIST_NO_DISK);
    CHECK(persist_save() == E_NOENT && persist_load(&st) == E_NOENT && persist_format(1) == E_NOENT);
    CHECK(persist_autosave() == E_OK);
    fake_no_hardware = 0;

    /* ---- disque vierge : non formaté, rien n'est écrit sans `format` ---- */
    memset(fake_disk, 0, sizeof fake_disk);
    fake_sector_writes = 0;
    boot();
    CHECK(persist_state() == PERSIST_UNFORMATTED);
    CHECK(persist_save() == E_NOENT && persist_load(&st) == E_NOENT);
    CHECK(fake_sector_writes == 0);                                                /* init n'écrit jamais */
    pi = persist_get_info();
    CHECK(pi->slot_sectors == 16384 && pi->sectors == fake_disk_sectors && !strcmp(pi->model, "FAKE DISK 9000"));

    /* ---- format ---- */
    CHECK(persist_format(0) == E_OK && persist_state() == PERSIST_READY);
    CHECK(pi->generation == 1 && pi->active == 0);
    CHECK(persist_load(&st) == E_OK && st.files == 0);                             /* sauvegarde vide chargeable */

    /* ---- sauvegarde et rechargement ---- */
    fs_set_clock(clock);
    put("/a.txt", "alpha");
    CHECK(fs_mkdir_p("/dir/sub") == E_OK);
    put("/dir/sub/b.txt", "bravo\nligne2\n");
    put("/dir/vide", "");
    {
        static char big[3000];

        for (int i = 0; i < 3000; i++) big[i] = (char)('a' + i % 26);
        big[2999] = 0;
        put("/dir/grand.txt", big);
    }
    CHECK(persist_dirty());
    CHECK(persist_save() == E_OK);
    CHECK(pi->generation == 2 && pi->active == 1 && !persist_dirty());             /* alternance A -> B */
    CHECK(pi->snapshot_bytes == tarw_size());
    CHECK(persist_autosave() == E_OK);                                             /* rien de neuf : pas d'écriture */
    CHECK(pi->generation == 2);

    power_cycle();                                                                 /* « redémarrage » */
    CHECK(persist_state() == PERSIST_READY && pi->generation == 2 && pi->active == 1);
    CHECK(!exists("/a.txt"));                                                      /* RAM vide avant load */
    CHECK(persist_load(&st) == E_OK && st.files == 4 && st.skipped == 0);
    CHECK(file_is("/a.txt", "alpha") && file_is("/dir/sub/b.txt", "bravo\nligne2\n") && file_is("/dir/vide", ""));
    { struct fs_node *n; CHECK(fs_lookup("/dir/grand.txt", &n) == E_OK && n->size == 2999 && n->data[0] == 'a' && n->data[27] == 'b'); }
    persist_mark_clean();
    CHECK(!persist_dirty());
    { struct fs_node *n; CHECK(fs_lookup("/a.txt", &n) == E_OK && n->mtime == 1700000000u);       /* dates conservees */
      CHECK(fs_lookup("/dir", &n) == E_OK && n->mtime == 1700000000u); }

    /* ---- l'image est un vrai tar lisible par GNU tar ---- */
    dumplen = 0;
    CHECK(tarw_write(dump_emit, NULL) == E_OK && dumplen == tarw_size());
    write_file("snapshot.tar", dump, dumplen);

    /* ---- modification, 2e sauvegarde : l'autre emplacement ---- */
    put("/a.txt", "alpha2");                                                       /* fs_create renvoie le fichier : écrase */
    CHECK(persist_dirty() && persist_autosave() == E_OK);
    CHECK(pi->generation == 3 && pi->active == 0);

    /* ---- COUPURE DE COURANT pendant une sauvegarde : l'ancienne reste intacte ---- */
    put("/dir/nouveau.txt", "ne doit pas survivre");
    for (long cut = 0; cut < 12; cut++) {
        u32 gen_before = pi->generation;
        err_t e;

        fake_dead = 0;
        fake_dead_after_writes = cut;                                              /* meurt après `cut` secteurs écrits */
        e = persist_save();
        CHECK(e != E_OK);                                                          /* l'écriture a échoué : signalé */
        power_cycle();
        CHECK(persist_state() == PERSIST_READY);
        CHECK(pi->generation == gen_before);                                       /* toujours la sauvegarde précédente */
        CHECK(persist_load(&st) == E_OK && file_is("/a.txt", "alpha2"));
        CHECK(!exists("/dir/nouveau.txt"));
        put("/dir/nouveau.txt", "ne doit pas survivre");                           /* on recommence */
    }
    /* la sauvegarde réussit ensuite normalement */
    CHECK(persist_save() == E_OK && pi->generation == 4);
    power_cycle();
    CHECK(persist_load(&st) == E_OK && file_is("/dir/nouveau.txt", "ne doit pas survivre"));

    /* coupure pile entre données et en-tête : l'en-tête n'est pas écrit -> ancienne génération */
    put("/c.txt", "c");
    {
        u32 need = tarw_size() / 512 + 1;                                          /* secteurs de données + invalidation */

        fake_dead_after_writes = (long)need;                                       /* tout sauf l'en-tête final */
        CHECK(persist_save() != E_OK);
        power_cycle();
        CHECK(pi->generation == 4);
        CHECK(persist_load(&st) == E_OK && !exists("/c.txt"));
    }

    /* ---- données abîmées : repli sur l'autre emplacement ---- */
    /* (une sauvegarde interrompue ci-dessus a invalidé l'emplacement cible : on en refait une complète) */
    put("/e.txt", "echo");
    CHECK(persist_save() == E_OK);
    {
        u32 gen_new = pi->generation;
        u32 lba = (pi->active ? pi->slot_sectors : 0) + 1 + 2;                     /* 3e secteur de données du courant */

        fake_disk[lba * 512 + 5] ^= 0x40;
        power_cycle();
        CHECK(persist_state() == PERSIST_READY && pi->generation == gen_new);      /* l'en-tête, lui, est valide */
        CHECK(persist_load(&st) == E_OK);                                          /* CRC faux -> bascule sur la précédente */
        CHECK(pi->generation == gen_new - 1 && file_is("/a.txt", "alpha2"));
        CHECK(file_is("/dir/nouveau.txt", "ne doit pas survivre") && !exists("/e.txt"));
    }
    /* en-tête abîmé aussi : l'emplacement est ignoré */
    {
        u32 g = pi->generation;
        u32 lba = pi->active ? pi->slot_sectors : 0;

        fake_disk[lba * 512 + 9] ^= 0xFF;                                          /* casse l'en-tête du courant */
        power_cycle();
        CHECK(persist_state() == PERSIST_READY && pi->generation != g);            /* il reste l'autre emplacement */
    }
    /* les deux emplacements détruits : disque considéré non formaté, pas de plantage */
    memset(fake_disk, 0xAB, 4 * 512);
    memset(fake_disk + (u32)16384 * 512, 0xAB, 4 * 512);
    power_cycle();
    CHECK(persist_state() == PERSIST_UNFORMATTED);
    CHECK(persist_format(0) == E_BUSY);                                            /* contient autre chose : refus */
    CHECK(persist_format(1) == E_OK && persist_state() == PERSIST_READY);

    /* ---- petit disque : emplacements réduits ---- */
    memset(fake_disk, 0, sizeof fake_disk);
    fake_disk_sectors = 1000;
    boot();
    CHECK(persist_state() == PERSIST_UNFORMATTED && pi->slot_sectors == 500);
    CHECK(persist_format(0) == E_OK);
    put("/x", "petit");
    CHECK(persist_save() == E_OK);
    {
        static char blob[600 * 1024];
        struct fs_node *f;

        fs_create("/gros", &f);
        CHECK(fs_write(f, blob, sizeof blob) == E_OK);
        CHECK(persist_save() == E_FBIG);                                            /* ne tient pas dans 250 Kio */
        CHECK(fs_remove("/gros", 0) == E_OK);
    }
    power_cycle();
    CHECK(persist_load(&st) == E_OK && file_is("/x", "petit") && !exists("/gros"));

    /* disque trop petit : ignoré */
    fake_disk_sectors = 100;
    memset(fake_disk, 0, 100 * 512);
    boot();
    CHECK(persist_state() == PERSIST_NO_DISK);
    fake_disk_sectors = 32768;

    /* ---- chemins longs : préfixe tar ---- */
    memset(fake_disk, 0, sizeof fake_disk);
    boot();
    CHECK(persist_format(0) == E_OK);
    {
        char p[FS_PATH_MAX];
        usize n = 0;
        static const char comp[] = "abcdefghijklmnopqrstuvwxyz01234";               /* 31 caractères */

        p[n++] = '/';
        for (int i = 0; i < 7; i++) {
            memcpy(p + n, comp, 31);
            n += 31;
            if (i < 6) p[n++] = '/';
        }
        p[n] = 0;                                                                   /* 7*31 + 6 + 1 = 224 caractères */
        CHECK(fs_mkdir_p(p) == E_OK);
        put("/zz", "z");
        {
            struct fs_node *f;

            p[n++] = '/'; p[n++] = 'f'; p[n] = 0;
            CHECK(fs_create(p, &f) == E_OK && fs_write(f, "profond", 7) == E_OK);
        }
        CHECK(persist_save() == E_OK);
        power_cycle();
        CHECK(persist_load(&st) == E_OK && file_is(p, "profond") && st.skipped == 0);
    }

    out("  stockage verifie : crc32, ATA, tar, PhoenFS, 12 coupures de courant, disque abime, petits disques\n");
    return 0;
}
