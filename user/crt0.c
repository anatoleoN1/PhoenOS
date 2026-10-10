/* PhoenOS - crt0.c : point d'entree des programmes ; appelle main(argc, argv) puis exit. */
#include "phoenos.h"

extern int main(int argc, char **argv);

void __attribute__((noreturn, used)) start_c(uint *sp)
{
    exit(main((int)sp[0], (char **)(sp + 1)));
}

__asm__(".global _start\n"
        "_start:\n"
        "    xorl %ebp, %ebp\n"
        "    movl %esp, %eax\n"
        "    andl $-16, %esp\n"
        "    subl $12, %esp\n"
        "    pushl %eax\n"
        "    call start_c\n");
