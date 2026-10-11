/*
 * mkpsl : emballe un ELF lie pour la zone des bibliotheques en bibliotheque partagee .psl (voir src/psl.h).
 *   mkpsl -n nom -v version -a auteur [-d description] -e symbole [-e symbole]... entree.elf sortie.psl
 *   -e : symbole a exporter (cherche dans la table des symboles de l'ELF, qui ne doit donc pas etre "strip")
 * Outil de la machine de developpement. Utilise le meme code (psl_build) que la commande pslpack de PhoenOS,
 * qui verifie le resultat (plage dans la zone des bibliotheques, exports dans la plage...).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psl.h"

int main(int argc, char **argv)
{
    static struct psl_spec sp;
    const char *in = NULL, *outp = NULL, *why = NULL;
    unsigned char *elf, *buf;
    long size;
    unsigned n;
    FILE *f;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) sp.name = argv[++i];
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) sp.version = argv[++i];
        else if (!strcmp(argv[i], "-a") && i + 1 < argc) sp.author = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) sp.description = argv[++i];
        else if (!strcmp(argv[i], "-e") && i + 1 < argc) {
            if (sp.nexports >= PSL_MAX_EXPORTS) { fprintf(stderr, "mkpsl : %d exports au plus\n", PSL_MAX_EXPORTS); return 2; }
            sp.exports[sp.nexports++].name = argv[++i];
        }
        else if (argv[i][0] == '-') { fprintf(stderr, "mkpsl : option inconnue %s\n", argv[i]); return 2; }
        else if (!in) in = argv[i];
        else if (!outp) outp = argv[i];
        else return 2;
    }
    if (!sp.name || !sp.version || !sp.author || !in || !outp) {
        fprintf(stderr, "usage : mkpsl -n nom -v version -a auteur [-d texte] -e symbole... entree.elf sortie.psl\n");
        return 2;
    }
    f = fopen(in, "rb");
    if (!f) { perror(in); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 52 || size > (long)PSL_MAX_IMAGE) { fprintf(stderr, "mkpsl : %s : taille invalide\n", in); fclose(f); return 1; }
    elf = malloc((size_t)size);
    buf = malloc((size_t)size + PSL_HEADER_SIZE);
    if (!elf || !buf || fread(elf, 1, (size_t)size, f) != (size_t)size) { fprintf(stderr, "mkpsl : lecture impossible\n"); fclose(f); return 1; }
    fclose(f);
    n = psl_build(&sp, elf, (unsigned)size, buf, (unsigned)size + PSL_HEADER_SIZE, &why);
    if (!n) { fprintf(stderr, "mkpsl : %s : %s\n", in, why ? why : "echec"); return 1; }
    f = fopen(outp, "wb");
    if (!f || fwrite(buf, 1, n, f) != n || fclose(f)) { fprintf(stderr, "mkpsl : ecriture de %s impossible\n", outp); return 1; }
    printf("%s : %s %s (%d export%s, %ld octets)\n", outp, sp.name, sp.version, sp.nexports, sp.nexports > 1 ? "s" : "", size);
    return 0;
}
