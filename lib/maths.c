/* PhoenOS - maths.psl : petite bibliotheque de calcul (exemple de bibliotheque partagee). Base 0x700000. */
typedef unsigned int uint;

static uint appels;                         /* donnee PROPRE a chaque programme qui utilise la bibliotheque */
const char maths_nom[] = "maths 1.0";       /* constante : dans le code partage */

uint maths_pgcd(uint a, uint b)
{
    appels++;
    while (b) {
        uint t = a % b;

        a = b;
        b = t;
    }
    return a;
}

uint maths_racine(uint n)                   /* racine carree entiere */
{
    uint r = 0;

    appels++;
    while ((r + 1) * (r + 1) <= n && r < 65535)
        r++;
    return r;
}

uint maths_fibo(uint n)                     /* n <= 47 */
{
    uint a = 0, b = 1;

    appels++;
    for (uint i = 0; i < n && i < 47; i++) {
        uint t = a + b;

        a = b;
        b = t;
    }
    return a;
}

uint maths_appels(void)                     /* combien de fois CE programme a appele la bibliotheque */
{
    return appels;
}
