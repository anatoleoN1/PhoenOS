/* PhoenOS - tas.c : demonstration du tas (malloc / free / realloc, voir user/libc.h). */
#include "libc.h"

struct noeud { int valeur; struct noeud *suivant; char reserve[24]; };

static void taille(const char *etape)
{
    printf("  %-34s : tas pris au noyau %3u Kio, utilise %3u Kio\n", etape, heap_bytes_from_kernel() / 1024,
           (heap_bytes_in_use() + 1023) / 1024);
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 1000;
    struct noeud *tete = NULL;
    void **blocs;
    int *t, somme = 0, erreurs = 0;

    if (n < 1 || n > 20000) {
        puts_fd(2, "usage : tas [nombre de blocs, 1 a 20000]\n");
        return 1;
    }
    printf("tas : %d blocs de 32 octets, puis liberation\n", n);
    taille("au depart");

    for (int i = 0; i < n; i++) {                       /* liste chainee : n allocations */
        struct noeud *x = malloc(sizeof *x);

        if (!x) { puts_fd(2, "tas : memoire epuisee\n"); return 1; }
        x->valeur = i;
        x->suivant = tete;
        tete = x;
    }
    taille("apres les allocations");
    for (struct noeud *x = tete; x; x = x->suivant)
        somme += x->valeur;
    printf("  somme des valeurs : %d (attendu %d)\n", somme, n * (n - 1) / 2);
    if (somme != n * (n - 1) / 2) erreurs++;

    while (tete) {                                      /* tout liberer : les blocs se fusionnent */
        struct noeud *s = tete->suivant;

        free(tete);
        tete = s;
    }
    taille("apres liberation (tas rendu)");

    blocs = calloc((uint)n, sizeof(void *));            /* calloc : memoire a zero */
    if (!blocs) return 1;
    for (int i = 0; i < n; i++) {
        blocs[i] = malloc(48);
        memset(blocs[i], i & 0xFF, 48);
    }
    for (int i = 0; i < n; i += 2) { free(blocs[i]); blocs[i] = NULL; }     /* trous */
    for (int i = 0; i < n; i += 2) blocs[i] = malloc(16);                   /* reutilisent les trous */
    taille("trous reutilises");
    for (int i = 1; i < n; i += 2)
        for (int k = 0; k < 48; k++)
            if (((uchar *)blocs[i])[k] != (uchar)(i & 0xFF)) { erreurs++; break; }   /* aucun bloc abime */
    for (int i = 0; i < n; i++) free(blocs[i]);
    free(blocs);

    t = malloc(4 * sizeof(int));                        /* realloc : le tableau grandit, son contenu est garde */
    for (int i = 0; i < 4; i++) t[i] = (i * 7919) % 101;
    for (int cap = 4; cap < 8192; cap *= 2) {
        t = realloc(t, (uint)cap * 2 * sizeof(int));
        for (int i = cap; i < cap * 2; i++) t[i] = (i * 7919) % 101;
    }
    qsort(t, 8192, sizeof(int), cmp_int);
    for (int i = 1; i < 8192; i++)
        if (t[i - 1] > t[i]) { erreurs++; break; }
    printf("  tableau de 8192 entiers agrandi par realloc puis trie : min %d, max %d\n", t[0], t[8191]);
    free(t);
    taille("a la fin");

    if (erreurs) { printf("tas : %d erreur(s)\n", erreurs); return 1; }
    puts("tas : tout est correct\n");
    return 0;
}
