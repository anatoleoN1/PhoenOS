/*
 * notes : carnet de notes dans /notes.txt.
 *   notes <texte...>   ajoute une ligne          notes            les affiche, numerotees
 *   notes -v           vide le carnet (supprime /notes.txt)
 * Pour garder le carnet apres un redemarrage : save (voir man save).
 */
#include "phoenos.h"

#ifndef FILE_PATH
#define FILE_PATH "/notes.txt"           /* les tests de la machine de developpement le remplacent (-DFILE_PATH=...) */
#endif

static int show(void)
{
    char buf[256];
    int fd = open(FILE_PATH), n;
    uint line = 1;
    int at_start = 1;

    if (fd < 0) {
        puts("Le carnet est vide. Ajoutez une note : notes ma premiere note\n");
        return 0;
    }
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (int i = 0; i < n; i++) {
            if (at_start) {
                if (line < 10) puts(" ");
                putu(line++); puts("  ");
                at_start = 0;
            }
            write(1, buf + i, 1);
            if (buf[i] == '\n')
                at_start = 1;
        }
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    int fd;

    if (argc == 1)
        return show();
    if (argc == 2 && streq(argv[1], "-v")) {
        if (unlink(FILE_PATH) < 0)
            puts("Le carnet est deja vide.\n");
        else
            puts("Carnet vide.\n");
        return 0;
    }
    fd = open_flags(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) {
        puts_fd(2, "notes : impossible d'ecrire " FILE_PATH "\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            write(fd, " ", 1);
        write(fd, argv[i], slen(argv[i]));
    }
    write(fd, "\n", 1);
    close(fd);
    puts("Note ajoutee.\n");
    return 0;
}
