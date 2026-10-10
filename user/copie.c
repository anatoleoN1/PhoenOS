/* copie <source> <destination> : copie un fichier (open/read/write). Ecrase la destination. */
#include "phoenos.h"

int main(int argc, char **argv)
{
    char buf[512];
    int in, out, n;
    uint total = 0;

    if (argc != 3) {
        puts_fd(2, "usage : copie <source> <destination>\n");
        return 1;
    }
    if (streq(argv[1], argv[2])) {
        puts_fd(2, "copie : la source et la destination sont identiques\n");
        return 1;
    }
    in = open(argv[1]);
    if (in < 0) {
        puts_fd(2, "copie : "); puts_fd(2, argv[1]); puts_fd(2, " : impossible d'ouvrir\n");
        return 1;
    }
    out = open_flags(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) {
        puts_fd(2, "copie : "); puts_fd(2, argv[2]); puts_fd(2, " : impossible d'ecrire\n");
        close(in);
        return 1;
    }
    while ((n = read(in, buf, sizeof buf)) > 0) {
        if (write(out, buf, (uint)n) != n) {
            puts_fd(2, "copie : ecriture interrompue (disque de RAM plein ?)\n");
            close(in); close(out);
            return 1;
        }
        total += (uint)n;
    }
    close(in);
    close(out);
    puts(argv[1]); puts(" -> "); puts(argv[2]); puts(" : "); putu(total); puts(" octet(s)\n");
    return 0;
}
