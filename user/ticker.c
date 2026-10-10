/* ticker <nom> [fois] [ms] : affiche "nom 1/N", "nom 2/N"... une fois toutes les <ms> millisecondes.
 * Lance plusieurs ticker avec & pour voir plusieurs programmes tourner en meme temps. */
#include "phoenos.h"

int main(int argc, char **argv)
{
    int n = 5, ms = 500;

    if (argc < 2) {
        puts_fd(2, "usage : ticker <nom> [fois] [ms]\n");
        return 1;
    }
    if (argc > 2)
        n = atoi(argv[2]);
    if (argc > 3)
        ms = atoi(argv[3]);
    if (n < 1 || n > 1000 || ms < 0 || ms > 60000) {
        puts_fd(2, "ticker : fois entre 1 et 1000, ms entre 0 et 60000\n");
        return 1;
    }
    log_msg(2, "demarre");
    for (int i = 1; i <= n; i++) {
        puts(argv[1]); puts(" "); putu((uint)i); puts("/"); putu((uint)n); puts("\n");
        if (i < n)
            sleep_ms((uint)ms);
    }
    log_msg(2, "termine");
    return 0;
}
