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
    CHECK(parse_mut(len, 8, 2, 4) == POE_NEEDS_NEWER);                     /* API trop recente */
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

    out("  format .poe verifie : vraie application, 35 corruptions d'en-tete, debordements, CRC, exigences, 20000 en-tetes aleatoires\n");
    return 0;
}
