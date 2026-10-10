/* faute <type> : provoque volontairement une erreur pour voir le noyau arreter le programme
 * sans tomber lui-meme.  Types : gp (instruction privilegiee), div (division par zero),
 * ud (instruction invalide), io (acces a un port). */
#include "phoenos.h"

int main(int argc, char **argv)
{
    volatile int zero = 0;

    if (argc != 2) {
        puts_fd(2, "usage : faute gp | div | ud | io\n");
        return 1;
    }
    puts("Je vais provoquer l'erreur '"); puts(argv[1]); puts("'...\n");
    if (streq(argv[1], "gp"))
        __asm__ volatile ("cli");
    else if (streq(argv[1], "div"))
        return 10 / zero;
    else if (streq(argv[1], "ud"))
        __asm__ volatile ("ud2");
    else if (streq(argv[1], "io"))
        __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
    else {
        puts_fd(2, "type inconnu\n");
        return 1;
    }
    puts("(ne devrait pas s'afficher)\n");
    return 0;
}
