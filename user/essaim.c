/* PhoenOS - essaim.c : lance plusieurs enfants en meme temps et les attend dans l'ordre de leur FIN. */
#include "libc.h"

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 4, fois = argc > 2 ? atoi(argv[2]) : 3, base = argc > 3 ? atoi(argv[3]) : 100;
    int pids[8], lances = 0, t0 = (int)uptime_ms();
    char noms[8][4], sfois[12], sms[12];

    if (n < 1 || n > 8 || fois < 1 || fois > 100 || base < 10 || base > 5000) {
        puts_fd(2, "usage : essaim [enfants 1-8] [fois 1-100] [ms 10-5000]\n");
        return 1;
    }
    snprintf(sfois, sizeof sfois, "%d", fois);
    for (int i = 0; i < n; i++) {
        char *av[5];

        snprintf(noms[i], sizeof noms[i], "E%d", i + 1);
        snprintf(sms, sizeof sms, "%d", base * (n - i));     /* le dernier lance est le plus rapide */
        av[0] = "ticker"; av[1] = noms[i]; av[2] = sfois; av[3] = sms; av[4] = 0;
        pids[i] = spawn("ticker", av);
        if (pids[i] < 0) {
            printf("essaim : lancement de %s impossible (%s)\n", noms[i], err_text(pids[i]));
            break;
        }
        lances++;
    }
    printf("essaim : %d enfant(s) lance(s)\n", lances);
    for (int reste = lances; reste > 0; reste--) {
        int code = 0, p = waitpid(-1, &code, 0);             /* le premier qui finit */

        if (p < 0) { printf("essaim : attente impossible (%s)\n", err_text(p)); return 1; }
        for (int i = 0; i < lances; i++)
            if (pids[i] == p)
                printf("essaim : %s (numero %d) fini, code %d, apres %d ms\n", noms[i], p, code, (int)uptime_ms() - t0);
    }
    puts("essaim : tous les enfants sont termines\n");
    return 0;
}
