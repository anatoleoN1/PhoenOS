/*
 * mkpoe : emballe un programme ELF dans une application .poe (voir src/poe.h).
 *   mkpoe -n nom -v version -a auteur [-d description] [-l bibliotheque]... [-g] [-r] [-A api] entree.elf sortie.poe
 *   -g : l'application exige l'interface graphique   -r : elle exige le reseau
 *   -d : phrase de presentation (format 2)           -l : bibliotheque .psl necessaire (format 2, 4 au plus)
 * Outil de la machine de developpement (compile par le Makefile), pas de PhoenOS.
 * Utilise le meme code (poe_build) que la commande poepack de PhoenOS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "poe.h"

int main(int argc, char **argv)
{
    struct poe_spec sp;
    const char *in = NULL, *outp = NULL, *why = NULL;
    unsigned char *elf, *buf;
    long size;
    unsigned n;
    FILE *f;

    memset(&sp, 0, sizeof sp);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) sp.name = argv[++i];
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) sp.version = argv[++i];
        else if (!strcmp(argv[i], "-a") && i + 1 < argc) sp.author = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) sp.description = argv[++i];
        else if (!strcmp(argv[i], "-A") && i + 1 < argc) sp.min_api = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) {
            if (sp.nneeds >= POE_MAX_NEEDS) { fprintf(stderr, "mkpoe : %d bibliotheques au plus\n", POE_MAX_NEEDS); return 2; }
            sp.needs[sp.nneeds++] = argv[++i];
        }
        else if (!strcmp(argv[i], "-g")) sp.flags |= POE_NEEDS_GUI;
        else if (!strcmp(argv[i], "-r")) sp.flags |= POE_NEEDS_NET;
        else if (argv[i][0] == '-') { fprintf(stderr, "mkpoe : option inconnue %s\n", argv[i]); return 2; }
        else if (!in) in = argv[i];
        else if (!outp) outp = argv[i];
        else return 2;
    }
    if (!sp.name || !sp.version || !sp.author || !in || !outp) {
        fprintf(stderr, "usage : mkpoe -n nom -v version -a auteur [-d texte] [-l lib]... [-g] [-r] [-A api] entree.elf sortie.poe\n");
        return 2;
    }
    f = fopen(in, "rb");
    if (!f) { perror(in); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 52 || size > (4 << 20)) { fprintf(stderr, "mkpoe : %s : taille invalide\n", in); fclose(f); return 1; }
    elf = malloc((size_t)size);
    buf = malloc((size_t)size + POE2_HEADER_SIZE);
    if (!elf || !buf || fread(elf, 1, (size_t)size, f) != (size_t)size) { fprintf(stderr, "mkpoe : lecture impossible\n"); fclose(f); return 1; }
    fclose(f);
    if (memcmp(elf, "\x7f" "ELF", 4)) { fprintf(stderr, "mkpoe : %s n'est pas un fichier ELF\n", in); return 1; }

    n = poe_build(&sp, elf, (unsigned)size, buf, (unsigned)size + POE2_HEADER_SIZE, &why);
    if (!n) { fprintf(stderr, "mkpoe : %s\n", why ? why : "echec"); return 1; }
    f = fopen(outp, "wb");
    if (!f || fwrite(buf, 1, n, f) != n || fclose(f)) {
        fprintf(stderr, "mkpoe : ecriture de %s impossible\n", outp);
        return 1;
    }
    printf("%s : %s %s (%ld octets de programme)\n", outp, sp.name, sp.version, size);
    return 0;
}
