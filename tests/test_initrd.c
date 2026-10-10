/* Test du vrai initrd.c : archives tar synthétiques (saines et hostiles) et vraie archive GNU tar. */
#include <stdarg.h>
#include "testlib.h"
#include "multiboot.h"
#include "pmm.h"
#include "kheap.h"
#include "panic.h"
#include "fs.h"
#include "initrd.h"

char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
static char fake_ram[40u << 20] __attribute__((aligned(4096)));
u8 test_scancode;

void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;

/* ---- constructeur d'archives ---- */
static u8 tar[1 << 20];
static u32 tlen;

static void octal(u8 *dst, int n, u32 v)
{
    for (int i = n - 2; i >= 0; i--) { dst[i] = (u8)('0' + (v & 7)); v >>= 3; }
    dst[n - 1] = 0;
}

static u8 *add_raw(const char *prefix, const char *name, char type, const void *data, u32 size, u32 declared)
{
    u8 *h = tar + tlen;

    memset(h, 0, 512);
    memcpy(h, name, strlen(name));
    memcpy(h + 345, prefix, strlen(prefix));
    octal(h + 100, 8, 0644);
    octal(h + 124, 12, declared);
    h[156] = (u8)type;
    memcpy(h + 257, "ustar", 5);
    memset(h + 148, ' ', 8);
    { u32 sum = 0; for (int i = 0; i < 512; i++) sum += h[i]; octal(h + 148, 7, sum); h[155] = ' '; }
    memcpy(h + 512, data, size);
    tlen += 512 + ((size + 511) & ~511u);
    return h;
}
static void add(const char *name, char type, const char *data) { add_raw("", name, type, data, (u32)strlen(data), (u32)strlen(data)); }
static void reset(void) { tlen = 0; memset(tar, 0, sizeof tar); fs_init(); }
static void finish(void) { tlen += 1024; }                        /* deux blocs de zéros */

static int content_is(const char *path, const char *text)
{
    struct fs_node *n;

    return fs_lookup(path, &n) == E_OK && n->type == FS_FILE && n->size == strlen(text) && !memcmp(n->data, text, n->size);
}
static int exists(const char *path) { struct fs_node *n; return fs_lookup(path, &n) == E_OK; }

static u8 real[1 << 20];

int test_main(void)
{
    struct initrd_stats st;
    struct fs_stats fst;
    long fd, got = 0;

    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
    kheap_init();

    /* archive saine */
    reset();
    add("./", '5', "");
    add("etc/", '5', "");
    add("etc/motd", '0', "bonjour");
    add("deep/er/file.txt", '0', "profond");                    /* dossiers parents absents */
    add("./hello.txt", '0', "salut");
    add("empty", '0', "");
    add("exactly512", '0', "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    add_raw("long/prefix", "name.txt", '0', "pfx", 3, 3);       /* prefix + name */
    finish();
    CHECK(initrd_load(tar, tlen, &st) == E_OK);
    CHECK(st.files == 6 && st.skipped == 0);
    CHECK(content_is("/etc/motd", "bonjour") && content_is("/deep/er/file.txt", "profond"));
    CHECK(content_is("/hello.txt", "salut") && content_is("/empty", "") && content_is("/long/prefix/name.txt", "pfx"));
    { struct fs_node *n; CHECK(fs_lookup("/exactly512", &n) == E_OK && n->size > 300); }

    /* les données sont copiées : écraser l'archive ne change rien */
    memset(tar, 0xEE, sizeof tar);
    CHECK(content_is("/etc/motd", "bonjour"));

    /* entrées dangereuses ou inutiles : ignorées, pas de crash */
    reset();
    add("../evil", '0', "x");
    add("a/../../evil2", '0', "x");
    add("/abs/ok.txt", '0', "ok");                              /* '/' de tête retiré */
    add("link", '2', "");                                       /* lien symbolique */
    add("dev", '3', "");
    add("good", '0', "g");
    finish();
    CHECK(initrd_load(tar, tlen, &st) == E_OK);
    CHECK(!exists("/evil") && !exists("/evil2") && !exists("/../evil"));
    CHECK(content_is("/abs/ok.txt", "ok") && content_is("/good", "g") && !exists("/link"));
    CHECK(st.files == 2 && st.skipped == 4);

    /* archive corrompue */
    reset();
    add("a", '0', "aaa");
    add("b", '0', "bbb");
    finish();
    tar[0] ^= 1;                                                 /* casse la somme de contrôle du 1er en-tête */
    CHECK(initrd_load(tar, tlen, &st) == E_IO && st.files == 0);

    reset();
    add("a", '0', "aaa");
    add("b", '0', "bbb");
    finish();
    tar[1024 + 4] ^= 1;                                          /* 2e en-tête corrompu : le 1er est chargé */
    CHECK(initrd_load(tar, tlen, &st) == E_IO && st.files == 1 && content_is("/a", "aaa"));

    /* pas du tar du tout */
    reset();
    memset(tar, 'Z', 2048);
    CHECK(initrd_load(tar, 2048, &st) == E_IO);

    /* tronquée : contenu annoncé plus grand que l'archive */
    reset();
    add("a", '0', "aaaaaaaaaa");
    CHECK(initrd_load(tar, 512 + 5, &st) == E_IO && st.files == 0);
    CHECK(initrd_load(tar, 100, &st) == E_IO);                  /* bloc incomplet */
    CHECK(initrd_load(tar, 0, &st) == E_OK && st.files == 0);   /* vide */

    /* taille annoncée gigantesque */
    reset();
    add_raw("", "huge", '0', "x", 1, 0x7FFFFFFFu);
    CHECK(initrd_load(tar, 512 + 512, &st) == E_IO);
    reset();
    { u8 *h = add_raw("", "huge2", '0', "x", 1, 1); h[124] = 0x80; }      /* base 256 */
    CHECK(initrd_load(tar, tlen, &st) == E_IO || st.skipped >= 1);

    /* fichier de plus de FS_FILE_MAX : ignoré */
    reset();
    {
        static u8 bigdata[FS_FILE_MAX + 1];

        add_raw("", "toobig", '0', bigdata, sizeof bigdata, sizeof bigdata);
        add("after", '0', "ok");
        finish();
        CHECK(initrd_load(tar, tlen, &st) == E_OK && st.skipped == 1 && content_is("/after", "ok") && !exists("/toobig"));
    }

    /* sans marqueur de fin : toléré */
    reset();
    add("a", '0', "aaa");
    CHECK(initrd_load(tar, tlen, &st) == E_OK && content_is("/a", "aaa"));

    /* mêmes noms deux fois : le dernier gagne */
    reset();
    add("dup", '0', "un");
    add("dup", '0', "deux");
    finish();
    CHECK(initrd_load(tar, tlen, &st) == E_OK && content_is("/dup", "deux"));

    /* fichier là où un dossier est attendu : ignoré sans crash */
    reset();
    add("f", '0', "x");
    add("f/g", '0', "y");
    finish();
    CHECK(initrd_load(tar, tlen, &st) == E_OK && st.skipped == 1 && content_is("/f", "x"));

    /* st == NULL toléré */
    reset();
    add("a", '0', "x");
    finish();
    CHECK(initrd_load(tar, tlen, NULL) == E_OK && exists("/a"));

    /* vraie archive créée par GNU tar --format=ustar (build/test/real.tar) */
    fd = sys3(5, (long)"real.tar", 0, 0);                       /* open */
    if (fd < 0) {
        out("  real.tar introuvable : test GNU tar ignore\n");
    } else {
        long r;

        while ((r = sys3(3, fd, (long)real + got, (long)(sizeof real - (usize)got))) > 0)
            got += r;
        sys3(6, fd, 0, 0);
        reset();
        CHECK(got > 1024);
        CHECK(initrd_load(real, (u32)got, &st) == E_OK);
        CHECK(content_is("/etc/motd", "Bonjour\n") && content_is("/hello.txt", "salut\n"));
        CHECK(content_is("/docs/a/b/c.txt", "profond\n") && st.skipped == 0);
        fs_get_stats(&fst);
        CHECK(fst.files == 3);
        out("  archive GNU tar reelle chargee : "); outnum(st.files); out(" fichiers\n");
    }

    kheap_check();
    out("  initrd verifie : archives saines, hostiles, corrompues, tronquees, vraie archive GNU tar\n");
    return 0;
}
