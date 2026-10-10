/* Test du vrai fs.c : chemins, création, écriture, suppression, déplacement, copie, limites. */
#include <stdarg.h>
#include "testlib.h"
#include "multiboot.h"
#include "pmm.h"
#include "kheap.h"
#include "panic.h"
#include "fs.h"

char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
static char fake_ram[40u << 20] __attribute__((aligned(4096)));
u8 test_scancode;

void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;

static u32 fake_now = 1000;
static u32 clock(void) { return fake_now; }

static int is(const char *path, int type)
{
    struct fs_node *n;

    return fs_lookup(path, &n) == E_OK && n->type == type;
}

static int content_is(const char *path, const char *text)
{
    struct fs_node *n;

    return fs_lookup(path, &n) == E_OK && n->type == FS_FILE && n->size == strlen(text) &&
           !memcmp(n->data, text, n->size);
}

static int cwd_is(const char *want)
{
    char b[FS_PATH_MAX];

    return fs_path_of(fs_cwd(), b, sizeof b) == E_OK && !strcmp(b, want);
}

int test_main(void)
{
    struct fs_node *n, *f;
    struct fs_stats st;
    char big[FS_PATH_MAX + 40], path[FS_PATH_MAX];

    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
    kheap_init();
    fs_set_clock(clock);
    fs_init();
    fs_init();                                                  /* idempotent */
    CHECK(fs_root()->mtime == 1000);

    CHECK(fs_root() == fs_cwd() && cwd_is("/"));
    fs_get_stats(&st);
    CHECK(st.files == 0 && st.dirs == 1 && st.bytes == 0);

    /* création et chemins */
    CHECK(fs_mkdir("/a") == E_OK && fs_mkdir("/a") == E_EXIST);
    CHECK(fs_mkdir("/x/y") == E_NOENT);
    CHECK(fs_mkdir_p("/p/q/r") == E_OK && fs_mkdir_p("/p/q/r") == E_OK && is("/p/q/r", FS_DIR));
    CHECK(fs_create("/a/f.txt", &f) == E_OK && f->type == FS_FILE && f->size == 0);
    { struct fs_node *g; CHECK(fs_create("/a/f.txt", &g) == E_OK && g == f); }   /* existant : renvoyé */
    CHECK(fs_create("/a", &n) == E_ISDIR);
    CHECK(fs_create("/a/f.txt/z", &n) == E_NOTDIR);
    CHECK(fs_lookup("/a/f.txt/z", &n) == E_NOTDIR);
    CHECK(fs_lookup("/nope", &n) == E_NOENT);
    CHECK(fs_lookup("/a/f.txt/", &n) == E_NOTDIR);               /* "fichier/" invalide */
    CHECK(fs_mkdir("") != E_OK);
    CHECK(fs_mkdir("/a/0123456789012345678901234567890123") == E_NAMETOOLONG);   /* 34 caractères */

    /* ., .., barres finales, chemins relatifs */
    CHECK(fs_chdir("/a") == E_OK && cwd_is("/a"));
    CHECK(is("f.txt", FS_FILE) && is("./f.txt", FS_FILE) && is("../a/f.txt", FS_FILE) && is("/a/", FS_DIR));
    CHECK(fs_chdir("..") == E_OK && cwd_is("/") && fs_chdir("..") == E_OK && cwd_is("/"));   /* .. de la racine */
    CHECK(fs_chdir("/a/f.txt") == E_NOTDIR && fs_chdir("/nope") == E_NOENT);
    CHECK(fs_chdir("/p/q/r/../..") == E_OK && cwd_is("/p"));
    CHECK(fs_chdir("//a///") == E_OK && cwd_is("/a"));

    /* écriture */
    CHECK(fs_write(f, "hello", 5) == E_OK && content_is("/a/f.txt", "hello"));
    CHECK(fs_append(f, " world", 6) == E_OK && content_is("/a/f.txt", "hello world"));
    CHECK(fs_write(f, "x", 1) == E_OK && content_is("/a/f.txt", "x"));
    CHECK(fs_write(f, "", 0) == E_OK && f->size == 0);
    CHECK(fs_append(f, f->data ? (const char *)f->data : "", 0) == E_OK);
    CHECK(fs_write(f, "abcdef", 6) == E_OK && fs_append(f, f->data, f->size) == E_OK && content_is("/a/f.txt", "abcdefabcdef"));   /* auto-référence */
    for (int i = 0; i < 100; i++)
        CHECK(fs_append(f, "0123456789", 10) == E_OK);
    CHECK(f->size == 12 + 1000);
    {
        static char blob[FS_FILE_MAX + 1];

        CHECK(fs_write(f, blob, FS_FILE_MAX) == E_OK && f->size == FS_FILE_MAX);
        CHECK(fs_write(f, blob, FS_FILE_MAX + 1) == E_FBIG && f->size == FS_FILE_MAX);   /* échec : contenu intact */
        CHECK(fs_append(f, "z", 1) == E_FBIG && f->size == FS_FILE_MAX);
        CHECK(fs_write(f, "abc", 3) == E_OK);
    }

    /* copie */
    CHECK(fs_copy("/a/f.txt", "/a/g.txt") == E_OK && content_is("/a/g.txt", "abc"));
    CHECK(fs_write(f, "new", 3) == E_OK && content_is("/a/g.txt", "abc"));          /* copie indépendante */
    CHECK(fs_copy("/a/f.txt", "/a/g.txt") == E_OK && content_is("/a/g.txt", "new")); /* écrase */
    CHECK(fs_copy("/a/f.txt", "/p") == E_OK && content_is("/p/f.txt", "new"));      /* dans un dossier */
    CHECK(fs_copy("/nope", "/z") == E_NOENT && fs_copy("/a", "/z") == E_ISDIR);
    CHECK(fs_copy("/a/f.txt", "/nodir/z") == E_NOENT);
    CHECK(fs_copy("/a/f.txt", "/a/f.txt") == E_INVAL && content_is("/a/f.txt", "new"));   /* sur lui-meme : refuse, sans perte */

    /* déplacement / renommage */
    CHECK(fs_rename("/a/g.txt", "/a/h.txt") == E_OK && !is("/a/g.txt", FS_FILE) && content_is("/a/h.txt", "new"));
    CHECK(fs_rename("/a/h.txt", "/p/q") == E_OK && content_is("/p/q/h.txt", "new"));   /* vers un dossier */
    CHECK(fs_rename("/a/f.txt", "/p/q/h.txt") == E_EXIST);
    CHECK(fs_rename("/p", "/p/q/r") == E_INVAL);                                         /* dans lui-même */
    CHECK(fs_rename("/p", "/p") != E_OK || is("/p", FS_DIR));
    CHECK(fs_rename("/nope", "/z") == E_NOENT);
    CHECK(fs_rename("/", "/a") != E_OK);
    CHECK(fs_rename("/p/q", "/a") == E_OK && is("/a/q/r", FS_DIR) && content_is("/a/q/h.txt", "new"));   /* dossier entier */

    /* tri alphabétique */
    CHECK(fs_create("/a/b1", &n) == E_OK && fs_create("/a/a1", &n) == E_OK && fs_create("/a/c1", &n) == E_OK);
    fs_lookup("/a", &n);
    {
        const char *prev = "";
        int ok = 1;

        for (struct fs_node *c = n->child; c; c = c->next) {
            if (strcmp(prev, c->name) >= 0) ok = 0;
            prev = c->name;
        }
        CHECK(ok);
    }

    /* suppression */
    CHECK(fs_remove("/a/b1", 0) == E_OK && !is("/a/b1", FS_FILE));
    CHECK(fs_remove("/a/b1", 0) == E_NOENT);
    CHECK(fs_remove("/a/q", 0) == E_ISDIR);
    CHECK(fs_remove("/a/q", FS_RM_DIR_OK) == E_NOTEMPTY);
    CHECK(fs_remove("/", FS_RM_DIR_OK | FS_RM_RECURSIVE) == E_BUSY);
    CHECK(fs_chdir("/a/q/r") == E_OK);
    CHECK(fs_remove("/a", FS_RM_DIR_OK | FS_RM_RECURSIVE) == E_BUSY);                    /* contient le dossier courant */
    CHECK(fs_remove("/a/q", FS_RM_DIR_OK | FS_RM_RECURSIVE) == E_BUSY);
    CHECK(fs_chdir("/") == E_OK);
    CHECK(fs_remove("/a/q/r", FS_RM_DIR_OK) == E_OK);
    CHECK(fs_remove("/a", FS_RM_DIR_OK | FS_RM_RECURSIVE) == E_OK && !is("/a", FS_DIR));

    /* mémoire : tout supprimer rend le tas comme au départ */
    CHECK(fs_remove("/p", FS_RM_DIR_OK | FS_RM_RECURSIVE) == E_OK);
    fs_get_stats(&st);
    CHECK(st.files == 0 && st.dirs == 1 && st.bytes == 0);
    {
        struct kheap_stats hs;

        kheap_get_stats(&hs);
        CHECK(hs.used_blocks == 1);                              /* seule la racine reste allouée */
        kheap_check();
    }

    /* limites : profondeur, chemins, nombre d'éléments */
    path[0] = 0;
    {
        int depth_ok = 1;
        usize n2 = 0;

        for (int i = 0; i < FS_DEPTH_MAX; i++) {
            path[n2++] = '/'; path[n2++] = 'd'; path[n2] = 0;
            if (fs_mkdir_p(path) != E_OK) depth_ok = 0;
        }
        CHECK(depth_ok);
        path[n2++] = '/'; path[n2++] = 'd'; path[n2] = 0;
        CHECK(fs_mkdir_p(path) != E_OK);                         /* un niveau de trop */
    }
    CHECK(fs_remove("/d", FS_RM_DIR_OK | FS_RM_RECURSIVE) == E_OK);
    memset(big, 'a', sizeof big - 1); big[0] = '/'; big[sizeof big - 1] = 0;
    CHECK(fs_mkdir(big) == E_NAMETOOLONG || fs_mkdir(big) != E_OK);
    {
        int r = E_OK, made = 0;
        char nm[16];

        for (int i = 0; i < FS_NODES_MAX + 10 && r == E_OK; i++) {
            nm[0] = '/'; nm[1] = 'n';
            nm[2] = (char)('0' + i / 1000); nm[3] = (char)('0' + i / 100 % 10);
            nm[4] = (char)('0' + i / 10 % 10); nm[5] = (char)('0' + i % 10); nm[6] = 0;
            r = fs_create(nm, &n);
            if (r == E_OK) made++;
        }
        CHECK(r != E_OK && made <= FS_NODES_MAX && made >= FS_NODES_MAX - 2);
        CHECK(fs_remove("/", FS_RM_DIR_OK | FS_RM_RECURSIVE) == E_BUSY);
    }

    /* chemin absolu d'un élément profond, tampon trop petit */
    fs_init();
    fs_mkdir_p("/aa/bb");
    fs_lookup("/aa/bb", &n);
    CHECK(fs_path_of(n, path, sizeof path) == E_OK && !strcmp(path, "/aa/bb"));
    CHECK(fs_path_of(n, path, 4) == E_NAMETOOLONG);
    CHECK(fs_path_of(fs_root(), path, 2) == E_OK && !strcmp(path, "/"));

    /* ---- dates de modification ---- */
    fs_init();
    fake_now = 5000;
    CHECK(fs_mkdir("/d") == E_OK && fs_lookup("/d", &n) == E_OK && n->mtime == 5000 && fs_root()->mtime == 5000);
    fake_now = 6000;
    CHECK(fs_create("/d/f", &f) == E_OK && f->mtime == 6000 && n->mtime == 6000);    /* le dossier est daté aussi */
    fake_now = 7000;
    CHECK(fs_write(f, "abc", 3) == E_OK && f->mtime == 7000 && n->mtime == 6000);
    fake_now = 8000;
    CHECK(fs_append(f, "d", 1) == E_OK && f->mtime == 8000);
    fake_now = 8500;
    CHECK(fs_append(f, "", 0) == E_OK && f->mtime == 8000);                         /* rien ajouté : pas de nouvelle date */
    fake_now = 9000;
    CHECK(fs_copy("/d/f", "/d/g") == E_OK && fs_lookup("/d/g", &n) == E_OK && n->mtime == 9000);
    fake_now = 9500;
    CHECK(fs_remove("/d/g", 0) == E_OK && fs_lookup("/d", &n) == E_OK && n->mtime == 9500);
    fs_set_mtime(f, 1234);
    CHECK(f->mtime == 1234);
    fs_set_mtime(NULL, 5);                                                          /* sans effet, sans plantage */
    fs_set_clock(NULL);                                                             /* sans horloge : tout est daté 0 */
    fs_init();
    CHECK(fs_mkdir("/z") == E_OK && fs_lookup("/z", &n) == E_OK && n->mtime == 0);
    fs_set_clock(clock);

    out("  systeme de fichiers verifie : chemins, ecriture, copie, deplacement, suppression, limites\n");
    return 0;
}
