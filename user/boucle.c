/* boucle [secondes] : occupe le processeur sans arret (calcul pur, aucun appel systeme) pendant N secondes
 * et affiche sa progression. Prouve la preemption : meme en train de "boucler", un programme laisse
 * la main aux autres et au shell. Ctrl+C ou kill l'arrete. */
#include "phoenos.h"

int main(int argc, char **argv)
{
    int secs = 3;
    uint start;
    volatile uint x = 0;

    if (argc > 1)
        secs = atoi(argv[1]);
    if (secs < 1 || secs > 600) {
        puts_fd(2, "usage : boucle [secondes]   (1 a 600)\n");
        return 1;
    }
    start = uptime_ms();
    for (int s = 1; s <= secs; s++) {
        while (uptime_ms() - start < (uint)s * 1000u)
            for (int i = 0; i < 200000; i++)
                x++;                            /* calcul pur : seule la minuterie peut nous interrompre */
        puts("boucle "); putu((uint)getpid()); puts(" : "); putu((uint)s); puts(" s\n");
    }
    return 0;
}
