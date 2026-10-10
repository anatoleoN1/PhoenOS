/* primes [N] : affiche les nombres premiers jusqu'a N (50 par defaut, 100000 au plus). */
#include "phoenos.h"

int main(int argc, char **argv)
{
    uint n = 50, count = 0;

    if (argc > 1)
        n = (uint)atoi(argv[1]);
    if (n > 100000) {
        puts_fd(2, "primes : N doit etre au plus 100000\n");
        return 1;
    }
    for (uint i = 2; i <= n; i++) {
        int prime = 1;

        for (uint d = 2; d * d <= i; d++)
            if (i % d == 0) { prime = 0; break; }
        if (prime) {
            if (count) puts(" ");
            putu(i);
            count++;
        }
    }
    puts("\n"); putu(count); puts(" nombre(s) premier(s)\n");
    return 0;
}
