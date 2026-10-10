/* saisie : demande un nom (lu sur l'entree standard : clavier ou tube) et dit bonjour. */
#include "phoenos.h"

int main(void)
{
    char buf[64];
    int n;

    puts("Quel est votre nom ? ");
    n = read(0, buf, sizeof buf - 1);
    if (n <= 0) {
        puts("\n(rien recu)\n");
        return 1;
    }
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        n--;
    buf[n] = 0;
    puts("Bonjour, "); puts(buf); puts(" !\n");
    return 0;
}
