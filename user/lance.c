/* PhoenOS - lance.c : lance un programme comme ENFANT, attend sa fin et dit comment il s'est termine. */
#include "libc.h"

int main(int argc, char **argv)
{
    int pid, code = 0, t0 = (int)uptime_ms();

    if (argc < 2) {
        puts_fd(2, "usage : lance <programme> [arguments...]\n");
        return 1;
    }
    pid = spawn(argv[1], argv + 1);                     /* argv[1] devient argv[0] de l'enfant */
    if (pid < 0) {
        printf("lance : impossible de lancer %s (%s)\n", argv[1], err_text(pid));
        return 1;
    }
    printf("lance : %s lance (numero %d), j'attends...\n", argv[1], pid);
    if (waitpid(pid, &code, 0) != pid) {
        puts_fd(2, "lance : attente impossible\n");
        return 1;
    }
    printf("lance : %s (numero %d) termine apres %d ms, code de sortie %d\n", argv[1], pid, (int)uptime_ms() - t0, code);
    return code;
}
