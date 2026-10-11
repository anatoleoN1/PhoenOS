/* Test du vrai poe.c : applications .poe emballees par le vrai outil mkpoe, puis abimees de mille facons. */
#include "testlib.h"
#include "poe.h"
#include "elf.h"

u8 test_scancode;

static u8 buf[1 << 16], elf[1 << 16], work[1 << 16];

static long file(const char *path, u8 *dst, usize max)
{
    long fd = sys3(5, (long)path, 0, 0), got = 0, r;

    if (fd < 0)
        return -1;
    while ((r = sys3(3, fd, (long)(dst + got), (long)(max - (usize)got))) > 0)
        got += r;
    sys3(6, fd, 0, 0);
    return got;
}

static void w32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static enum poe_status parse_mut(u32 len, usize at, u32 val, int bytes)
{
    struct poe_info i;

    memcpy(work, buf, len);
    for (int k = 0; k < bytes; k++)
        work[at + (usize)k] = (u8)(val >> (8 * k));
    return poe_parse(work, len, &i);
}

int test_main(void)
{
    struct poe_info info;
    long n = file("user/calc.poe", buf, sizeof buf), en = file("user/calc", elf, sizeof elf);
    u32 len, entry;
    static u8 mem[0x300000];

    CHECK(n > 96 && en > 100);
    len = (u32)n;

    /* ---- une vraie application ---- */
    CHECK(poe_is(buf, len) && !poe_is(buf, 3) && !poe_is(elf, (u32)en));
    CHECK(poe_parse(buf, len, &info) == POE_OK);
    CHECK(!strcmp(info.name, "calc") && !strcmp(info.version, "9.8.7") && !strcmp(info.author, "Equipe Test"));
    CHECK(info.flags == 0 && info.min_api == 1 && info.payload == buf + 96 && info.payload_len == (u32)en);
    CHECK(!memcmp(info.payload, elf, (usize)en));                         /* le programme est intact */
    CHECK(elf_load(info.payload, info.payload_len, mem, 0x400000u, 0x700000u, &entry) == E_OK);   /* et chargeable */

    /* ---- fichiers qui ne sont pas des .poe / sont coupes ---- */
    CHECK(poe_parse(elf, (u32)en, &info) == POE_NOT_POE);
    CHECK(poe_parse(buf, 0, &info) == POE_NOT_POE && poe_parse(buf, 3, &info) == POE_NOT_POE);
    CHECK(poe_parse(buf, 4, &info) == POE_TRUNCATED && poe_parse(buf, 95, &info) == POE_TRUNCATED);
    CHECK(poe_parse(buf, len - 1, &info) == POE_TRUNCATED);                /* un octet de moins dans le programme */
    CHECK(poe_parse(buf, 96, &info) == POE_TRUNCATED);                     /* en-tete seul */
    CHECK(parse_mut(len, 0, 'X', 1) == POE_NOT_POE);
    CHECK(parse_mut(len, 3, 2, 1) == POE_NOT_POE);                         /* signature de version 2 */

    /* ---- en-tete incoherent ---- */
    CHECK(parse_mut(len, 4, 2, 2) == POE_BAD_HEADER);                      /* format inconnu */
    CHECK(parse_mut(len, 4, 0, 2) == POE_BAD_HEADER);
    CHECK(parse_mut(len, 6, 4, 2) == POE_BAD_HEADER);                      /* drapeau inconnu */
    CHECK(parse_mut(len, 6, 0x8000, 2) == POE_BAD_HEADER);
    CHECK(parse_mut(len, 12, 95, 4) == POE_BAD_HEADER);                    /* programme dans l'en-tete */
    CHECK(parse_mut(len, 12, 0, 4) == POE_BAD_HEADER);
    CHECK(parse_mut(len, 16, 0, 4) == POE_BAD_HEADER);                     /* programme vide */
    CHECK(parse_mut(len, 12, len + 1, 4) == POE_TRUNCATED);                /* commence apres la fin */
    CHECK(parse_mut(len, 16, (u32)en + 1, 4) == POE_TRUNCATED);            /* depasse la fin */
    CHECK(parse_mut(len, 12, 0xFFFFFFF0u, 4) == POE_TRUNCATED);            /* debordement d'entier */
    CHECK(parse_mut(len, 16, 0xFFFFFFFFu, 4) == POE_TRUNCATED);
    CHECK(parse_mut(len, 24, 0, 1) == POE_BAD_HEADER);                     /* nom vide */
    CHECK(parse_mut(len, 24, 0x07, 1) == POE_BAD_HEADER);                  /* caractere de controle */
    CHECK(parse_mut(len, 24, 0xE9, 1) == POE_BAD_HEADER);                  /* pas ASCII */
    CHECK(parse_mut(len, 30, 'x', 1) == POE_BAD_HEADER);                   /* bribe de texte apres le 0 */
    CHECK(parse_mut(len, 56, 0, 1) == POE_BAD_HEADER);                     /* version vide */
    CHECK(parse_mut(len, 72, 0, 1) == POE_BAD_HEADER);                     /* auteur vide */
    memcpy(work, buf, len); memset(work + 24, 'n', 32);
    CHECK(poe_parse(work, len, &info) == POE_BAD_HEADER);                  /* nom sans 0 final */
    memcpy(work, buf, len); memset(work + 72, 'a', 24);
    CHECK(poe_parse(work, len, &info) == POE_BAD_HEADER);                  /* auteur sans 0 final */

    /* ---- programme abime, exigences ---- */
    CHECK(parse_mut(len, 96 + 100, 0xEE, 1) == POE_BAD_CRC || elf[100] == 0xEE);
    memcpy(work, buf, len); work[96 + 100] ^= 1;
    CHECK(poe_parse(work, len, &info) == POE_BAD_CRC && !strcmp(info.name, "calc"));   /* infos lisibles quand meme */
    CHECK(parse_mut(len, 20, 0, 4) == POE_BAD_CRC);
    CHECK(parse_mut(len, 8, POE_API_LEVEL + 1, 4) == POE_NEEDS_NEWER);                     /* API trop recente */
    CHECK(parse_mut(len, 8, 0xFFFFFFFFu, 4) == POE_NEEDS_NEWER);
    CHECK(parse_mut(len, 8, 0, 4) == POE_OK);                              /* API 0 : tout va bien */
    CHECK(parse_mut(len, 6, POE_NEEDS_GUI, 2) == POE_NEEDS_FEATURE);
    CHECK(parse_mut(len, 6, POE_NEEDS_NET, 2) == POE_NEEDS_FEATURE);
    CHECK(parse_mut(len, 6, POE_NEEDS_GUI | POE_NEEDS_NET, 2) == POE_NEEDS_FEATURE);

    /* ---- applications creees avec -g et -r par mkpoe ---- */
    n = file("user/gui.poe", work, sizeof work);
    CHECK(n > 96 && poe_parse(work, (u32)n, &info) == POE_NEEDS_FEATURE && info.flags == POE_NEEDS_GUI);
    n = file("user/net.poe", work, sizeof work);
    CHECK(n > 96 && poe_parse(work, (u32)n, &info) == POE_NEEDS_FEATURE && info.flags == POE_NEEDS_NET);

    /* ---- messages ---- */
    for (int s = POE_OK; s <= POE_NEEDS_FEATURE; s++)
        CHECK(strlen(poe_status_text((enum poe_status)s)) > 3);

    /* ---- robustesse : 20000 en-tetes abimes au hasard, jamais de plantage ni de lecture hors du fichier ---- */
    {
        u32 x = 777;
        int ok = 0;

        for (int i = 0; i < 20000; i++) {
            u32 l = len;

            memcpy(work, buf, len);
            for (int k = 0; k < 3; k++) {
                x = x * 1103515245u + 12345u;
                work[(x >> 8) % 96] = (u8)(x >> 16);
            }
            x = x * 1103515245u + 12345u;
            if ((x & 7) == 0)
                l = (x >> 8) % (len + 1);
            if (poe_parse(work, l, &info) == POE_OK) {
                ok++;
                CHECK(info.payload >= work + 96 && info.payload_len > 0 && info.payload + info.payload_len <= work + l);
            }
        }
        CHECK(ok > 0);
    }

    /* ---- format 2 (0.8.0) : description et bibliotheques, cree par mkpoe -d -l puis par poe_build ---- */
    {
        static u8 v2[1 << 16], o2[1 << 16];
        struct poe_spec sp;
        const char *why = NULL;
        u32 n2;
        long m = file("user/app2.poe", v2, sizeof v2);

        CHECK(m > 208 && poe_parse(v2, (u32)m, &info) == POE_OK && info.format == 2);
        CHECK(!strcmp(info.name, "app2") && !strcmp(info.description, "Une application complete") && info.nneeds == 2);
        CHECK(!strcmp(info.needs[0], "maths") && !strcmp(info.needs[1], "texte") && info.min_api == 2);
        CHECK(info.payload == v2 + 208 && info.payload_len == (u32)en && !memcmp(info.payload, elf, (usize)en));
        CHECK(poe_parse(buf, len, &info) == POE_OK && info.format == 1 && info.nneeds == 0 && info.description[0] == 0);   /* l'ancien format se lit toujours */
        for (u32 cut = 0; cut < 208; cut += 7)
            CHECK(poe_parse(v2, cut, &info) == (cut >= 4 ? POE_TRUNCATED : POE_NOT_POE));
        CHECK(poe_parse(v2, 207, &info) == POE_TRUNCATED);
        /* cases de bibliotheques abimees */
        memcpy(work, v2, (usize)m); work[144] = 0;
        CHECK(poe_parse(work, (u32)m, &info) == POE_BAD_HEADER);                       /* un trou avant la suite */
        memcpy(work, v2, (usize)m); memcpy(work + 160, "maths", 6);
        CHECK(poe_parse(work, (u32)m, &info) == POE_BAD_HEADER);                       /* doublon */
        memcpy(work, v2, (usize)m); work[144] = 'M';
        CHECK(poe_parse(work, (u32)m, &info) == POE_BAD_HEADER);                       /* nom invalide */
        memcpy(work, v2, (usize)m); memset(work + 144, 'a', 16);
        CHECK(poe_parse(work, (u32)m, &info) == POE_BAD_HEADER);                       /* sans 0 final */
        memcpy(work, v2, (usize)m); work[96] = 7;
        CHECK(poe_parse(work, (u32)m, &info) == POE_BAD_HEADER);                       /* description : caractere de controle */
        memcpy(work, v2, (usize)m); memset(work + 96, 'd', 48);
        CHECK(poe_parse(work, (u32)m, &info) == POE_BAD_HEADER);                       /* description sans 0 final */
        memcpy(work, v2, (usize)m); work[12] = 100; work[13] = 0;
        CHECK(poe_parse(work, (u32)m, &info) == POE_BAD_HEADER);                       /* programme dans l'en-tete de 208 octets */
        memcpy(work, v2, (usize)m); work[4] = 3;
        CHECK(poe_parse(work, (u32)m, &info) == POE_BAD_HEADER);                       /* format 3 inconnu */
        /* poe_build : les memes regles que mkpoe */
        memset(&sp, 0, sizeof sp);
        sp.name = "x"; sp.version = "1"; sp.author = "a";
        n2 = poe_build(&sp, elf, (u32)en, o2, sizeof o2, &why);
        CHECK(n2 == 96 + (u32)en && poe_parse(o2, n2, &info) == POE_OK && info.format == 1 && info.min_api == 1);
        sp.description = "d"; sp.needs[0] = "m"; sp.nneeds = 1;
        n2 = poe_build(&sp, elf, (u32)en, o2, sizeof o2, &why);
        CHECK(n2 == 208 + (u32)en && poe_parse(o2, n2, &info) == POE_OK && info.format == 2 && info.nneeds == 1 && info.min_api == 2);
        sp.min_api = 1;
        n2 = poe_build(&sp, elf, (u32)en, o2, sizeof o2, &why);
        CHECK(n2 && poe_parse(o2, n2, &info) == POE_OK && info.min_api == 1);          /* niveau d'API force */
        sp.min_api = 0; sp.needs[0] = "Maj";
        CHECK(poe_build(&sp, elf, (u32)en, o2, sizeof o2, &why) == 0 && why);
        sp.needs[0] = "m"; sp.needs[1] = "m"; sp.nneeds = 2;
        CHECK(poe_build(&sp, elf, (u32)en, o2, sizeof o2, &why) == 0);                 /* doublon */
        sp.nneeds = 5;
        CHECK(poe_build(&sp, elf, (u32)en, o2, sizeof o2, &why) == 0);
        sp.nneeds = 1; sp.name = "";
        CHECK(poe_build(&sp, elf, (u32)en, o2, sizeof o2, &why) == 0);                 /* nom vide */
        sp.name = "x"; sp.flags = 0x80;
        CHECK(poe_build(&sp, elf, (u32)en, o2, sizeof o2, &why) == 0);                 /* drapeau inconnu */
        sp.flags = 0;
        CHECK(poe_build(&sp, elf, (u32)en, o2, 100, &why) == 0);                       /* tampon trop petit */
        CHECK(poe_build(&sp, (const u8 *)"pas un elf, vraiment pas, du tout du tout du tout du tout", 60, o2, sizeof o2, &why) == 0);
        {   /* 20000 en-tetes de format 2 abimes : jamais de plantage */
            u32 x = 4242;

            for (int i = 0; i < 20000; i++) {
                memcpy(work, v2, (usize)m);
                for (int k = 0; k < 3; k++) {
                    x = x * 1103515245u + 12345u;
                    work[(x >> 8) % 208] = (u8)(x >> 16);
                }
                if (poe_parse(work, (u32)m, &info) == POE_OK)
                    CHECK(info.payload >= work + 208 && info.payload + info.payload_len <= work + m && info.nneeds <= POE_MAX_NEEDS);
            }
        }
    }

    out("  format .poe verifie : vraie application, 35 corruptions d'en-tete, debordements, CRC, exigences, 20000 en-tetes aleatoires, format 2 (description, bibliotheques), poe_build\n");
    return 0;
}
