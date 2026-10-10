/*
 * mkpoe : emballe un programme ELF dans une application .poe (voir src/poe.h).
 *   mkpoe -n nom -v version -a auteur [-g] [-r] [-A niveau_api] entree.elf sortie.poe
 *   -g : l'application exige l'interface graphique   -r : elle exige le reseau
 * Outil de la machine de developpement (compile par le Makefile), pas de PhoenOS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "poe.h"
#include "crc32.h"

static void w16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void w32(unsigned char *p, unsigned v) { w16(p, v & 0xFFFF); w16(p + 2, v >> 16); }

static int put_text(unsigned char *dst, size_t size, const char *s, const char *what)
{
    size_t n = strlen(s);

    if (n == 0 || n >= size) {
        fprintf(stderr, "mkpoe : %s doit faire de 1 a %zu caracteres\n", what, size - 1);
        return 0;
    }
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) {
            fprintf(stderr, "mkpoe : %s : caracteres ASCII imprimables uniquement\n", what);
            return 0;
        }
    memcpy(dst, s, n);
    return 1;
}

int main(int argc, char **argv)
{
    const char *name = NULL, *version = NULL, *author = NULL, *in = NULL, *outp = NULL;
    unsigned flags = 0, api = POE_API_LEVEL;
    unsigned char hdr[POE_HEADER_SIZE] = { 0 };
    unsigned char *elf;
    long size;
    FILE *f;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) version = argv[++i];
        else if (!strcmp(argv[i], "-a") && i + 1 < argc) author = argv[++i];
        else if (!strcmp(argv[i], "-A") && i + 1 < argc) api = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-g")) flags |= POE_NEEDS_GUI;
        else if (!strcmp(argv[i], "-r")) flags |= POE_NEEDS_NET;
        else if (argv[i][0] == '-') { fprintf(stderr, "mkpoe : option inconnue %s\n", argv[i]); return 2; }
        else if (!in) in = argv[i];
        else if (!outp) outp = argv[i];
        else return 2;
    }
    if (!name || !version || !author || !in || !outp) {
        fprintf(stderr, "usage : mkpoe -n nom -v version -a auteur [-g] [-r] [-A api] entree.elf sortie.poe\n");
        return 2;
    }
    f = fopen(in, "rb");
    if (!f) { perror(in); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 52 || size > (4 << 20)) { fprintf(stderr, "mkpoe : %s : taille invalide\n", in); fclose(f); return 1; }
    elf = malloc((size_t)size);
    if (!elf || fread(elf, 1, (size_t)size, f) != (size_t)size) { fprintf(stderr, "mkpoe : lecture impossible\n"); fclose(f); return 1; }
    fclose(f);
    if (memcmp(elf, "\x7f" "ELF", 4)) { fprintf(stderr, "mkpoe : %s n'est pas un fichier ELF\n", in); return 1; }

    memcpy(hdr, "POE\1", 4);
    w16(hdr + 4, POE_FORMAT);
    w16(hdr + 6, flags);
    w32(hdr + 8, api);
    w32(hdr + 12, POE_HEADER_SIZE);
    w32(hdr + 16, (unsigned)size);
    w32(hdr + 20, crc32_update(0, elf, (size_t)size));
    if (!put_text(hdr + 24, 32, name, "le nom") || !put_text(hdr + 56, 16, version, "la version") ||
        !put_text(hdr + 72, 24, author, "l'auteur"))
        return 1;
    f = fopen(outp, "wb");
    if (!f || fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr || fwrite(elf, 1, (size_t)size, f) != (size_t)size || fclose(f)) {
        fprintf(stderr, "mkpoe : ecriture de %s impossible\n", outp);
        return 1;
    }
    printf("%s : %s %s (%ld octets de programme)\n", outp, name, version, size);
    return 0;
}
