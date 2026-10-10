/* hello : premier programme utilisateur. Affiche ses arguments et son numero de processus. */
#include "phoenos.h"

int main(int argc, char **argv)
{
    puts("Bonjour depuis un programme utilisateur (ring 3) !\n");
    puts("  processus numero "); putu((uint)getpid()); puts("\n");
    puts("  arguments : "); putu((uint)argc); puts("\n");
    for (int i = 0; i < argc; i++) {
        puts("    ["); putu((uint)i); puts("] "); puts(argv[i]); puts("\n");
    }
    return 0;
}
