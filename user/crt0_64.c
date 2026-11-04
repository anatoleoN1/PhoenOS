/* PhoenOS - crt0_64.c : point d'entree des programmes 64 bits (perf64) ; appelle main(argc, argv) puis exit.
 * Pile a l'entree : [rsp] = argc (8 octets), puis argv[0..argc-1] (pointeurs de 8 octets) et un pointeur nul. */
#include "phoenos.h"

extern int main(int argc, char **argv);

void __attribute__((noreturn, used)) start_c(unsigned long *sp)
{
    exit(main((int)sp[0], (char **)(sp + 1)));
}

__asm__(".global _start\n"
        "_start:\n"
        "    xorl %ebp, %ebp\n"
        "    movq %rsp, %rdi\n"
        "    andq $-16, %rsp\n"
        "    call start_c\n");
