/* compte <fichier>... : compte lignes et octets de fichiers, avec open/read/close. */
#include "phoenos.h"

int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2) {
        puts_fd(2, "usage : compte <fichier>...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        char buf[256];
        uint lines = 0, bytes = 0;
        int n, fd = open(argv[i]);

        if (fd < 0) {
            puts_fd(2, "compte : "); puts_fd(2, argv[i]); puts_fd(2, " : impossible d'ouvrir\n");
            status = 1;
            continue;
        }
        while ((n = read(fd, buf, sizeof buf)) > 0) {
            bytes += (uint)n;
            for (int k = 0; k < n; k++)
                if (buf[k] == '\n')
                    lines++;
        }
        close(fd);
        putu(lines); puts(" ligne(s), "); putu(bytes); puts(" octet(s) : "); puts(argv[i]); puts("\n");
    }
    return status;
}
