/* PhoenOS - utilib : utilise les bibliotheques partagees maths et texte (declarees dans l'en-tete de utilib.poe). */
#include "libc.h"

typedef uint (*fn2)(uint, uint);
typedef uint (*fn1)(uint);
typedef uint (*fn0)(void);
typedef void (*fns)(char *);
typedef uint (*fnc)(const char *);

int main(int argc, char **argv)
{
    fn2 pgcd = (fn2)psl_sym("maths", "maths_pgcd");
    fn1 racine = (fn1)psl_sym("maths", "maths_racine");
    fn1 fibo = (fn1)psl_sym("maths", "maths_fibo");
    fn0 appels = (fn0)psl_sym("maths", "maths_appels");
    fns majus = (fns)psl_sym("texte", "texte_maj");
    fns inv = (fns)psl_sym("texte", "texte_inverser");
    fnc mots = (fnc)psl_sym("texte", "texte_mots");
    const char *nom = (const char *)psl_sym("maths", "maths_nom");
    char phrase[64];
    uint a = argc > 1 ? (uint)atoi(argv[1]) : 84, b = argc > 2 ? (uint)atoi(argv[2]) : 36;

    if (!pgcd || !racine || !fibo || !appels || !majus || !inv || !mots || !nom) {
        printf("utilib : bibliotheques introuvables (maths : %s, texte : %s)\n", err_text(psl_addr("maths", 0)),
               err_text(psl_addr("texte", 0)));
        return 1;
    }
    printf("bibliotheque %s (code a l'adresse %p)\n", nom, (void *)pgcd);
    printf("pgcd(%u, %u) = %u\n", a, b, pgcd(a, b));
    printf("racine(%u) = %u\n", a * b, racine(a * b));
    printf("fibo(30) = %u\n", fibo(30));
    snprintf(phrase, sizeof phrase, "PhoenOS aime les bibliotheques");
    printf("mots : %u\n", mots(phrase));
    majus(phrase);
    printf("%s\n", phrase);
    inv(phrase);
    printf("%s\n", phrase);
    printf("appels a maths par CE programme : %u\n", appels());
    return 0;
}
