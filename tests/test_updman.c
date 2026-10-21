/*
 * Test du manifeste de mise a jour (updman.c) : lecture stricte (chaque defaut est refuse), numeros de version,
 * signatures Ed25519 fabriquees par l'outil Python (tools/phoenos_sign.py) et verifiees par le noyau.
 */
#include "testlib.h"
#include "updman.h"
#include "upd_vectors.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

static char buf[2048];

/* copie V_MAN dans buf avec remplacement de la 1re occurrence de `from` par `to` ; renvoie la longueur */
static u32 mutate(const char *from, const char *to)
{
    u32 n = V_MAN_LEN, fl = (u32)strlen(from), tl = (u32)strlen(to), o = 0;
    const char *m = (const char *)V_MAN;
    u32 at = n;

    for (u32 i = 0; i + fl <= n; i++)
        if (!memcmp(m + i, from, fl)) { at = i; break; }
    if (at == n)
        return 0xFFFFFFFFu;
    memcpy(buf, m, at); o = at;
    memcpy(buf + o, to, tl); o += tl;
    memcpy(buf + o, m + at + fl, n - at - fl); o += n - at - fl;
    buf[o] = 0;
    return o;
}

#define EXPECT(from, to, code) do { u32 l_ = mutate(from, to); struct upd_manifest mm_; \
    CHECK(l_ != 0xFFFFFFFFu && upd_man_parse(buf, l_, &mm_) == (code)); } while (0)

static struct upd_manifest M;

int test_main(void)
{
    const char *good = (const char *)V_MAN;

    /* ---- numeros de version ---- */
    CHECK(upd_build_of("0.10.4") == 10004u);
    CHECK(upd_build_of("1.0.0") == 1000000u);
    CHECK(upd_build_of("12.345.678") == 12345678u);
    CHECK(upd_build_of("0.0.1") == 1u);
    CHECK(upd_build_of("0.10") == 0 && upd_build_of("0.10.4.1") == 0 && upd_build_of("") == 0 && upd_build_of("a.b.c") == 0);
    CHECK(upd_build_of("01.2.3") == 0 && upd_build_of("1.02.3") == 0 && upd_build_of("1.2.03") == 0);
    CHECK(upd_build_of("100.0.0") == 0 && upd_build_of("1.1000.0") == 0 && upd_build_of("1.0.1000") == 0);
    CHECK(upd_build_of("1.2.3 ") == 0 && upd_build_of(" 1.2.3") == 0 && upd_build_of("-1.2.3") == 0);
    CHECK(upd_edition_known("perf32") && upd_edition_known("perf64") && upd_edition_known("pui64"));
    CHECK(!upd_edition_known("perf") && !upd_edition_known("PERF32") && !upd_edition_known("") && !upd_edition_known("perf32x"));

    /* ---- manifeste correct ---- */
    CHECK(upd_man_parse(good, V_MAN_LEN, &M) == UPD_M_OK);
    CHECK(!strcmp(M.edition, "perf32") && !strcmp(M.version, "0.10.5") && M.build == 10005u);
    CHECK(!strcmp(M.kname, "phoenos-perf32-0.10.5.kernel") && M.ksize == V_KERNEL_LEN && !strcmp(M.iname, "phoenos-perf32-0.10.5.initrd"));
    CHECK(M.isize == V_INITRD_LEN && !strcmp(M.notes, "essai"));
    {
        u8 h[32];

        extern void sha256(u8 out[32], const void *d, u32 n);
        sha256(h, V_KERNEL, V_KERNEL_LEN);
        CHECK(!memcmp(h, M.khash, 32));
        sha256(h, V_INITRD, V_INITRD_LEN);
        CHECK(!memcmp(h, M.ihash, 32));
    }
    CHECK(upd_man_parse((const char *)V_MAN_NONOTES, V_MAN_NONOTES_LEN, &M) == UPD_M_OK && M.notes[0] == 0);

    /* ---- defauts : chacun doit etre refuse ---- */
    CHECK(upd_man_parse(good, 0, &M) == UPD_M_SIZE);
    CHECK(upd_man_parse(good, UPD_MAN_MAX + 1, &M) == UPD_M_SIZE);
    CHECK(upd_man_parse(good, V_MAN_LEN - 1, &M) != UPD_M_OK);                       /* pas de \n final */
    CHECK(upd_man_parse(good, V_MAN_LEN - 8, &M) != UPD_M_OK);                       /* coupe en plein milieu */
    EXPECT("PHOENOS-MANIFEST 1", "PHOENOS-MANIFEST 2", UPD_M_FORMAT);
    EXPECT("PHOENOS-MANIFEST 1", "phoenos-manifest 1", UPD_M_FORMAT);
    EXPECT("edition perf32", "edition perf33", UPD_M_EDITION);
    EXPECT("edition perf32", "edition PERF32", UPD_M_EDITION);
    EXPECT("edition perf32", "edition perf32 ", UPD_M_EDITION);
    EXPECT("edition perf32", "editions perf32", UPD_M_FORMAT);
    EXPECT("version 0.10.5", "version 0.10", UPD_M_VERSION);
    EXPECT("version 0.10.5", "version 0.010.5", UPD_M_VERSION);
    EXPECT("version 0.10.5", "version 0.10.6", UPD_M_BUILD);
    EXPECT("build 10005", "build 10004", UPD_M_BUILD);
    EXPECT("build 10005", "build 010005", UPD_M_BUILD);
    EXPECT("build 10005", "build 1x005", UPD_M_BUILD);
    EXPECT("build 10005\n", "build 10005\r\n", UPD_M_FORMAT);
    EXPECT("\nkernel ", "\nkernels ", UPD_M_FORMAT);
    EXPECT("phoenos-perf32-0.10.5.kernel", "../etc/passwd", UPD_M_FILE);
    EXPECT("phoenos-perf32-0.10.5.kernel", "a/b", UPD_M_FILE);
    EXPECT("phoenos-perf32-0.10.5.kernel", ".cache", UPD_M_FILE);
    EXPECT("phoenos-perf32-0.10.5.kernel", "x y", UPD_M_FILE);
    EXPECT("phoenos-perf32-0.10.5.kernel 4128", "phoenos-perf32-0.10.5.kernel 0", UPD_M_FILE);
    EXPECT("phoenos-perf32-0.10.5.kernel 4128", "phoenos-perf32-0.10.5.kernel 99999999", UPD_M_FILE);
    EXPECT("phoenos-perf32-0.10.5.kernel 4128", "phoenos-perf32-0.10.5.kernel -4128", UPD_M_FILE);
    EXPECT("phoenos-perf32-0.10.5.kernel 4128", "phoenos-perf32-0.10.5.kernel 04128", UPD_M_FILE);
    EXPECT("phoenos-perf32-0.10.5.kernel 4128", "phoenos-perf32-0.10.5.kernel", UPD_M_FILE);
    EXPECT("\ninitrd ", "\ninitrd  ", UPD_M_FILE);
    {   /* hash : majuscule, trop court, caractere non hexadecimal */
        char hx[3] = { 0, 0, 0 };
        u32 l;

        l = mutate("notes essai", "notes essai");                                      /* copie telle quelle */
        CHECK(l == V_MAN_LEN);
        /* 1er hash : on change le 1er caractere hexadecimal apres "kernel <nom> <taille> " */
        for (u32 i = 0; i + 4 < l; i++)
            if (!memcmp(buf + i, "4128 ", 5)) { hx[0] = buf[i + 5]; buf[i + 5] = (char)(hx[0] >= 'a' ? hx[0] - 32 : 'A'); break; }
        CHECK(upd_man_parse(buf, l, &M) == UPD_M_HASH);
        for (u32 i = 0; i + 4 < l; i++)
            if (!memcmp(buf + i, "4128 ", 5)) { buf[i + 5] = 'g'; break; }
        CHECK(upd_man_parse(buf, l, &M) == UPD_M_HASH);
        for (u32 i = 0; i + 4 < l; i++)
            if (!memcmp(buf + i, "4128 ", 5)) { buf[i + 5] = hx[0]; buf[i + 6] = '\n'; break; }   /* ligne coupee : hash trop court */
        CHECK(upd_man_parse(buf, l, &M) != UPD_M_OK);
    }
    EXPECT("notes essai", "notes essai\nplus", UPD_M_FORMAT);                         /* ligne en trop */
    EXPECT("notes essai", "autre essai", UPD_M_FORMAT);
    EXPECT("notes essai", "notes ess\x01i", UPD_M_FORMAT);                             /* caractere de controle */
    EXPECT("notes essai", "notes ess\xc3\xa9i", UPD_M_FORMAT);                         /* non ASCII */
    EXPECT("notes essai\n", "notes essai\n\n", UPD_M_FORMAT);                          /* ligne vide a la fin */
    {   /* notes de 80 caracteres : permises ; 81 : refusees */
        char n80[100], n81[100];
        u32 l;

        memcpy(n80, "notes ", 6); memcpy(n81, "notes ", 6);
        for (int i = 0; i < 80; i++) n80[6 + i] = 'x';
        n80[86] = 0;
        for (int i = 0; i < 81; i++) n81[6 + i] = 'x';
        n81[87] = 0;
        l = mutate("notes essai", n80);
        CHECK(upd_man_parse(buf, l, &M) == UPD_M_OK);
        l = mutate("notes essai", n81);
        CHECK(upd_man_parse(buf, l, &M) == UPD_M_FORMAT);
    }
    {   /* octet nul cache */
        u32 l = mutate("notes essai", "notes essai");
        buf[l - 3] = 0;
        CHECK(upd_man_parse(buf, l, &M) == UPD_M_FORMAT);
    }

    /* ---- signatures ---- */
    CHECK(upd_man_verify(V_PUBKEY, good, V_MAN_LEN, V_SIG));
    CHECK(upd_man_verify(V_PUBKEY, (const char *)V_MAN_NONOTES, V_MAN_NONOTES_LEN, V_SIG_NONOTES));
    CHECK(!upd_man_verify(V_PUBKEY, good, V_MAN_LEN, V_SIG_NONOTES));                /* signature d'un autre manifeste */
    CHECK(!upd_man_verify(V_PUBKEY, good, V_MAN_LEN, V_SIG_OTHER));                  /* signe par une autre cle */
    for (u32 i = 0; i < V_MAN_LEN; i += 7) {                                         /* un octet change n'importe ou */
        memcpy(buf, good, V_MAN_LEN);
        buf[i] ^= 1;
        CHECK(!upd_man_verify(V_PUBKEY, buf, V_MAN_LEN, V_SIG));
    }
    CHECK(!upd_man_verify(V_PUBKEY, good, V_MAN_LEN - 1, V_SIG));                    /* manifeste tronque */
    for (u32 i = 0; i < 64; i += 5) {                                                /* signature abimee */
        u8 s[64];

        memcpy(s, V_SIG, 64);
        s[i] ^= 0x80;
        CHECK(!upd_man_verify(V_PUBKEY, good, V_MAN_LEN, s));
    }
    {   /* signature "malleable" : S + L doit etre refusee */
        u8 s[64];
        static const u8 Lb[32] = { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10 };
        u32 c = 0;

        memcpy(s, V_SIG, 64);
        for (int i = 0; i < 32; i++) { c += (u32)s[32 + i] + Lb[i]; s[32 + i] = (u8)c; c >>= 8; }
        CHECK(!upd_man_verify(V_PUBKEY, good, V_MAN_LEN, s));
    }
    {   /* mauvaise cle publique */
        u8 pk[32];

        memcpy(pk, V_PUBKEY, 32);
        pk[3] ^= 4;
        CHECK(!upd_man_verify(pk, good, V_MAN_LEN, V_SIG));
        memset(pk, 0, 32);
        CHECK(!upd_man_verify(pk, good, V_MAN_LEN, V_SIG));
    }
    /* les autres manifestes se lisent aussi */
    CHECK(upd_man_parse((const char *)V_MAN_X64, V_MAN_X64_LEN, &M) == UPD_M_OK && !strcmp(M.edition, "perf64"));
    CHECK(upd_man_parse((const char *)V_MAN_OLD, V_MAN_OLD_LEN, &M) == UPD_M_OK && M.build == 10002u);
    CHECK(upd_man_verify(V_PUBKEY, (const char *)V_MAN_OLD, V_MAN_OLD_LEN, V_SIG_OLD));

    out("  Manifeste verifie : lecture stricte (40 defauts refuses), versions, signatures Ed25519 (octets changes, S+L, autre cle)\n");
    return 0;
}
